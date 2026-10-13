#include "server/services/auth_service.h"

#include <algorithm>
#include <cctype>
#include <regex>

#include "server/infra/crypto.h"
#include "server/infra/logger.h"
#include "server/infra/metrics.h"
#include "server/services/api_error.h"

namespace zc::server {

using nlohmann::json;

namespace {

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

}  // namespace

std::string normalizeEmail(const std::string &email) {
    std::string e = trim(email);
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

AuthService::AuthService(const Config &config, std::shared_ptr<UserRepository> users)
        : config_(config), users_(std::move(users)), jwt_(config.jwtSecret, "zen-cube"),
          hashing_("auth", config.maxConcurrentAuth) {
    // Used to spend the same time on unknown emails as on wrong passwords.
    dummyHash_ = crypto::hashPassword(crypto::randomHex(16), config.passwordHashIterations);
}

json AuthService::userJson(const User &u) {
    return {{"id", u.id}, {"email", u.email}, {"displayName", u.displayName}, {"createdAt", isoTimestampMs(u.createdAtMs)}};
}

std::string AuthService::issueToken(const User &u) const {
    return jwt_.sign({{"sub", std::to_string(u.id)}, {"name", u.displayName}},
                     static_cast<int64_t>(config_.jwtTtlHours) * 3600, nowEpochMs() / 1000);
}

Bulkhead::Permit AuthService::hashingPermit() {
    auto p = hashing_.tryAcquire(config_.queueTimeout);
    if (!p) {
        Metrics::instance().inc("bulkhead_rejections_total", {{"pool", "auth"}});
        throw ApiError(503, "BUSY", "Too many sign-ins right now. Please try again in a moment.", nullptr, 2);
    }
    return p;
}

AuthResult AuthService::registerUser(const std::string &rawEmail, const std::string &password,
                                     const std::string &rawName) {
    std::string email = normalizeEmail(rawEmail);
    std::string name = trim(rawName);
    static const std::regex emailRe(R"(^[^@\s]+@[^@\s]+\.[^@\s]+$)");
    json fields = json::object();
    if (email.size() > 254 || !std::regex_match(email, emailRe)) fields["email"] = "Enter a valid email address.";
    if (password.size() < 8) fields["password"] = "Use at least 8 characters.";
    if (password.size() > 128) fields["password"] = "Use at most 128 characters.";
    if (name.size() < 2 || name.size() > 32) fields["displayName"] = "Use 2 to 32 characters.";
    if (std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 0x20; }))
        fields["displayName"] = "Display name contains invalid characters.";
    if (!fields.empty()) throw ApiError(400, "VALIDATION_FAILED", "Please fix the highlighted fields.", {{"fields", fields}});

    std::string hash;
    {
        auto permit = hashingPermit();
        hash = crypto::hashPassword(password, config_.passwordHashIterations);
    }
    try {
        User u = users_->create(email, name, hash);
        Metrics::instance().inc("auth_events_total", {{"event", "register"}});
        return {issueToken(u), u};
    } catch (const DuplicateError &) {
        throw ApiError(409, "EMAIL_TAKEN", "An account with this email already exists. Try signing in.");
    }
}

AuthResult AuthService::login(const std::string &rawEmail, const std::string &password) {
    std::string email = normalizeEmail(rawEmail);
    auto user = users_->findByEmail(email);
    bool ok;
    {
        auto permit = hashingPermit();
        // Always run one PBKDF2 verification so response time does not reveal
        // whether the email is registered (user enumeration).
        ok = crypto::verifyPassword(password, user ? user->passwordHash : dummyHash_) && user.has_value();
    }
    if (!ok) {
        Metrics::instance().inc("auth_events_total", {{"event", "login_failed"}});
        throw ApiError(401, "INVALID_CREDENTIALS", "Email or password is incorrect.");
    }
    if (crypto::needsRehash(user->passwordHash, config_.passwordHashIterations)) {
        users_->updatePasswordHash(user->id, crypto::hashPassword(password, config_.passwordHashIterations));
    }
    Metrics::instance().inc("auth_events_total", {{"event", "login"}});
    return {issueToken(*user), *user};
}

int64_t AuthService::authenticate(const std::string &header) const {
    const std::string prefix = "Bearer ";
    if (header.compare(0, prefix.size(), prefix) != 0) {
        throw ApiError(401, "UNAUTHENTICATED", "Sign in to use this feature.");
    }
    std::string why;
    auto claims = jwt_.verify(header.substr(prefix.size()), nowEpochMs() / 1000, &why);
    if (!claims) throw ApiError(401, "UNAUTHENTICATED", "Your session is invalid or has expired. Please sign in again.");
    try {
        return std::stoll(claims->value("sub", ""));
    } catch (const std::exception &) {
        throw ApiError(401, "UNAUTHENTICATED", "Your session is invalid. Please sign in again.");
    }
}

}  // namespace zc::server
