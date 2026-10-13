#pragma once

#include <mutex>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace zc::server {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

// Structured JSON-lines logger on stdout: one object per line, so log platforms
// (Render, Datadog, Loki) can index fields like request_id and duration_ms.
class Logger {
public:
    static Logger &instance();

    void setLevel(std::string_view level);
    bool enabled(LogLevel l) const { return l >= level_; }

    void log(LogLevel level, std::string_view msg, nlohmann::json fields = nlohmann::json::object());
    void debug(std::string_view msg, nlohmann::json f = nlohmann::json::object()) { log(LogLevel::Debug, msg, std::move(f)); }
    void info(std::string_view msg, nlohmann::json f = nlohmann::json::object()) { log(LogLevel::Info, msg, std::move(f)); }
    void warn(std::string_view msg, nlohmann::json f = nlohmann::json::object()) { log(LogLevel::Warn, msg, std::move(f)); }
    void error(std::string_view msg, nlohmann::json f = nlohmann::json::object()) { log(LogLevel::Error, msg, std::move(f)); }

private:
    LogLevel level_ = LogLevel::Info;
    std::mutex mu_;
};

inline Logger &log() { return Logger::instance(); }

std::string isoTimestampMs(int64_t epochMs);
int64_t nowEpochMs();

}  // namespace zc::server
