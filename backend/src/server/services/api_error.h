#pragma once

#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace zc::server {

// An error with an HTTP status and a stable machine-readable code. Services
// throw these; the HTTP layer turns them into the standard error envelope:
//   {"error": {"code": "...", "message": "...", "details": ..., "requestId": "..."}}
struct ApiError : std::runtime_error {
    ApiError(int status, std::string code, const std::string &message, nlohmann::json details = nullptr,
             int retryAfterSeconds = 0)
            : std::runtime_error(message), status(status), code(std::move(code)), details(std::move(details)),
              retryAfterSeconds(retryAfterSeconds) {}

    int status;
    std::string code;
    nlohmann::json details;
    int retryAfterSeconds;
};

}  // namespace zc::server
