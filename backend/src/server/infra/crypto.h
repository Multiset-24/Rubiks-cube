#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace zc::server::crypto {

std::string randomBytes(size_t n);
std::string randomHex(size_t bytes);

std::string base64UrlEncode(std::string_view data);
std::optional<std::string> base64UrlDecode(std::string_view data);

std::string hmacSha256(std::string_view key, std::string_view data);
std::string sha256Hex(std::string_view data);

// Constant-time comparison: run time does not depend on where the inputs differ,
// so attackers cannot learn a signature or hash byte by byte from timing.
bool constantTimeEquals(std::string_view a, std::string_view b);

// Password hashing with PBKDF2-HMAC-SHA256 and a random 16-byte salt.
// Encoded as "pbkdf2_sha256$<iterations>$<salt b64url>$<hash b64url>", so the
// iteration count can be raised later without invalidating stored hashes.
std::string hashPassword(std::string_view password, int iterations);
bool verifyPassword(std::string_view password, std::string_view encoded);
// True when a stored hash uses fewer iterations than currently configured.
bool needsRehash(std::string_view encoded, int iterations);

}  // namespace zc::server::crypto
