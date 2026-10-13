#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "server/repo/repositories.h"

namespace zc::server {

class RunService {
public:
    explicit RunService(std::shared_ptr<RunRepository> runs) : runs_(std::move(runs)) {}

    nlohmann::json create(int64_t userId, const nlohmann::json &input);
    nlohmann::json importBatch(int64_t userId, const nlohmann::json &input);
    nlohmann::json list(int64_t userId, int limit, const std::string &cursor);
    void remove(int64_t userId, int64_t runId);
    nlohmann::json stats(int64_t userId);
    nlohmann::json leaderboard(int limit);

    static nlohmann::json runJson(const Run &r);
    static std::string encodeCursor(const RunCursor &c);
    static std::optional<RunCursor> decodeCursor(const std::string &s);
    // WCA average of n: drop the best and worst time, mean of the rest.
    static std::optional<int64_t> averageOf(const std::vector<int64_t> &times, size_t n);
    // Current and longest daily streak from distinct days (YYYY-MM-DD, newest first).
    static std::pair<int, int> streaks(const std::vector<std::string> &days, const std::string &today);

private:
    Run parseRun(int64_t userId, const nlohmann::json &input) const;

    std::shared_ptr<RunRepository> runs_;
    // Leaderboard is read-heavy and changes slowly: cache it briefly.
    std::mutex boardMu_;
    std::chrono::steady_clock::time_point boardAt_{};
    int boardLimit_ = 0;
    nlohmann::json board_;
};

}  // namespace zc::server
