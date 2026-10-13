#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace zc::server {

// HS256 JSON Web Tokens. Stateless sessions: any instance can verify a token
// with the shared secret, so the API scales horizontally without a session store.
class Jwt {
public:
    Jwt(std::string secret, std::string issuer) : secret_(std::move(secret)), issuer_(std::move(issuer)) {}

    std::string sign(const nlohmann::json &claims, int64_t ttlSeconds, int64_t nowSeconds) const;

    // Verifies signature (constant time), algorithm, issuer and expiry. Returns the
    // claims, or nullopt with the reason in `error`.
    std::optional<nlohmann::json> verify(const std::string &token, int64_t nowSeconds, std::string *error = nullptr) const;

private:
    std::string secret_;
    std::string issuer_;
};

}  // namespace zc::server
