#pragma once

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "server/config.h"
#include "server/infra/bulkhead.h"
#include "server/infra/jwt.h"
#include "server/repo/repositories.h"

namespace zc::server {

struct AuthResult {
    std::string token;
    User user;
};

class AuthService {
public:
    AuthService(const Config &config, std::shared_ptr<UserRepository> users);

    AuthResult registerUser(const std::string &email, const std::string &password, const std::string &displayName);
    AuthResult login(const std::string &email, const std::string &password);
    // Returns the user id from a "Bearer <jwt>" header, or throws ApiError(401).
    int64_t authenticate(const std::string &authorizationHeader) const;
    std::optional<User> findUser(int64_t id) const { return users_->findById(id); }

    static nlohmann::json userJson(const User &u);

private:
    std::string issueToken(const User &u) const;
    Bulkhead::Permit hashingPermit();

    const Config &config_;
    std::shared_ptr<UserRepository> users_;
    Jwt jwt_;
    Bulkhead hashing_;
    std::string dummyHash_;
};

std::string normalizeEmail(const std::string &email);

}  // namespace zc::server
