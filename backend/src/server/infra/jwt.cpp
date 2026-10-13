#include "server/infra/jwt.h"

#include "server/infra/crypto.h"

namespace zc::server {

std::string Jwt::sign(const nlohmann::json &claims, int64_t ttlSeconds, int64_t nowSeconds) const {
    nlohmann::json header = {{"alg", "HS256"}, {"typ", "JWT"}};
    nlohmann::json payload = claims;
    payload["iss"] = issuer_;
    payload["iat"] = nowSeconds;
    payload["exp"] = nowSeconds + ttlSeconds;
    std::string signingInput = crypto::base64UrlEncode(header.dump()) + "." + crypto::base64UrlEncode(payload.dump());
    return signingInput + "." + crypto::base64UrlEncode(crypto::hmacSha256(secret_, signingInput));
}

std::optional<nlohmann::json> Jwt::verify(const std::string &token, int64_t nowSeconds, std::string *error) const {
    auto fail = [&](const char *why) -> std::optional<nlohmann::json> {
        if (error) *error = why;
        return std::nullopt;
    };
    size_t a = token.find('.');
    size_t b = a == std::string::npos ? std::string::npos : token.find('.', a + 1);
    if (a == std::string::npos || b == std::string::npos || token.find('.', b + 1) != std::string::npos) {
        return fail("malformed token");
    }
    std::string signingInput = token.substr(0, b);
    auto sig = crypto::base64UrlDecode(token.substr(b + 1));
    if (!sig || !crypto::constantTimeEquals(*sig, crypto::hmacSha256(secret_, signingInput))) {
        return fail("bad signature");
    }
    auto headerJson = crypto::base64UrlDecode(token.substr(0, a));
    auto payloadJson = crypto::base64UrlDecode(token.substr(a + 1, b - a - 1));
    if (!headerJson || !payloadJson) return fail("malformed token");
    nlohmann::json header = nlohmann::json::parse(*headerJson, nullptr, false);
    nlohmann::json payload = nlohmann::json::parse(*payloadJson, nullptr, false);
    if (header.is_discarded() || payload.is_discarded() || !payload.is_object()) return fail("malformed token");
    // Pin the algorithm: never trust the header to choose it ("alg": "none" attacks).
    if (header.value("alg", "") != "HS256") return fail("unsupported algorithm");
    if (payload.value("iss", "") != issuer_) return fail("wrong issuer");
    if (!payload.contains("exp") || !payload["exp"].is_number_integer() || payload["exp"].get<int64_t>() <= nowSeconds) {
        return fail("token expired");
    }
    return payload;
}

}  // namespace zc::server
