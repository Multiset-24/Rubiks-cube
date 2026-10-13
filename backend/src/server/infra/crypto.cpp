#include "server/infra/crypto.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <stdexcept>
#include <vector>

namespace zc::server::crypto {

std::string randomBytes(size_t n) {
    std::string out(n, '\0');
    if (RAND_bytes(reinterpret_cast<unsigned char *>(out.data()), static_cast<int>(n)) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }
    return out;
}

std::string randomHex(size_t bytes) {
    static const char *hex = "0123456789abcdef";
    std::string raw = randomBytes(bytes), out;
    for (unsigned char c: raw) {
        out += hex[c >> 4];
        out += hex[c & 15];
    }
    return out;
}

std::string base64UrlEncode(std::string_view data) {
    static const char *tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    size_t i = 0;
    while (i + 2 < data.size()) {
        uint32_t v = (static_cast<uint8_t>(data[i]) << 16) | (static_cast<uint8_t>(data[i + 1]) << 8) |
                     static_cast<uint8_t>(data[i + 2]);
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
        out += tbl[v & 63];
        i += 3;
    }
    if (i + 1 == data.size()) {
        uint32_t v = static_cast<uint8_t>(data[i]) << 16;
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
    } else if (i + 2 == data.size()) {
        uint32_t v = (static_cast<uint8_t>(data[i]) << 16) | (static_cast<uint8_t>(data[i + 1]) << 8);
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
    }
    return out;
}

std::optional<std::string> base64UrlDecode(std::string_view data) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '-') return 62;
        if (c == '_') return 63;
        return -1;
    };
    while (!data.empty() && data.back() == '=') data.remove_suffix(1);
    if (data.size() % 4 == 1) return std::nullopt;
    std::string out;
    uint32_t buf = 0;
    int bits = 0;
    for (char c: data) {
        int v = val(c);
        if (v < 0) return std::nullopt;
        buf = (buf << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buf >> bits) & 0xFF);
        }
    }
    return out;
}

std::string hmacSha256(std::string_view key, std::string_view data) {
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char *>(data.data()), data.size(), out, &len);
    return std::string(reinterpret_cast<char *>(out), len);
}

std::string sha256Hex(std::string_view data) {
    unsigned char out[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(data.data()), data.size(), out);
    static const char *hex = "0123456789abcdef";
    std::string s;
    for (unsigned char c: out) {
        s += hex[c >> 4];
        s += hex[c & 15];
    }
    return s;
}

bool constantTimeEquals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

namespace {

std::string pbkdf2(std::string_view password, std::string_view salt, int iterations) {
    std::string out(32, '\0');
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                          reinterpret_cast<const unsigned char *>(salt.data()), static_cast<int>(salt.size()),
                          iterations, EVP_sha256(), 32, reinterpret_cast<unsigned char *>(out.data())) != 1) {
        throw std::runtime_error("PBKDF2 failed");
    }
    return out;
}

struct Parsed {
    int iterations;
    std::string salt;
    std::string hash;
};

std::optional<Parsed> parse(std::string_view encoded) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    for (size_t i = 0; i <= encoded.size(); ++i) {
        if (i == encoded.size() || encoded[i] == '$') {
            parts.push_back(encoded.substr(start, i - start));
            start = i + 1;
        }
    }
    if (parts.size() != 4 || parts[0] != "pbkdf2_sha256") return std::nullopt;
    Parsed p;
    try {
        p.iterations = std::stoi(std::string(parts[1]));
    } catch (const std::exception &) {
        return std::nullopt;
    }
    auto salt = base64UrlDecode(parts[2]);
    auto hash = base64UrlDecode(parts[3]);
    if (!salt || !hash || p.iterations <= 0) return std::nullopt;
    p.salt = *salt;
    p.hash = *hash;
    return p;
}

}  // namespace

std::string hashPassword(std::string_view password, int iterations) {
    std::string salt = randomBytes(16);
    std::string hash = pbkdf2(password, salt, iterations);
    return "pbkdf2_sha256$" + std::to_string(iterations) + "$" + base64UrlEncode(salt) + "$" + base64UrlEncode(hash);
}

bool verifyPassword(std::string_view password, std::string_view encoded) {
    auto p = parse(encoded);
    if (!p) return false;
    return constantTimeEquals(pbkdf2(password, p->salt, p->iterations), p->hash);
}

bool needsRehash(std::string_view encoded, int iterations) {
    auto p = parse(encoded);
    return !p || p->iterations < iterations;
}

}  // namespace zc::server::crypto
