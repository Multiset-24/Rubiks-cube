#include "server/infra/logger.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>

namespace zc::server {

Logger &Logger::instance() {
    static Logger l;
    return l;
}

void Logger::setLevel(std::string_view level) {
    if (level == "debug") level_ = LogLevel::Debug;
    else if (level == "warn") level_ = LogLevel::Warn;
    else if (level == "error") level_ = LogLevel::Error;
    else level_ = LogLevel::Info;
}

int64_t nowEpochMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string isoTimestampMs(int64_t epochMs) {
    std::time_t secs = static_cast<std::time_t>(epochMs / 1000);
    std::tm tm{};
    gmtime_r(&secs, &tm);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(epochMs % 1000));
    return buf;
}

void Logger::log(LogLevel level, std::string_view msg, nlohmann::json fields) {
    if (!enabled(level)) return;
    static const char *names[] = {"debug", "info", "warn", "error"};
    nlohmann::json line = nlohmann::json::object();
    line["ts"] = isoTimestampMs(nowEpochMs());
    line["level"] = names[static_cast<int>(level)];
    line["msg"] = msg;
    if (fields.is_object()) line.update(fields);
    std::string out = line.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    std::lock_guard<std::mutex> lock(mu_);
    std::cout << out << '\n' << std::flush;
}

}  // namespace zc::server
