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
    RunService(std::shared_ptr<RunRepository> runs, int maxRunsPerUser)
            : runs_(std::move(runs)), maxRunsPerUser_(maxRunsPerUser) {}

    // Leaderboard times are self-reported; ignore anything faster than this.
    static constexpr int64_t kLeaderboardMinMs = 3000;

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
    void checkQuota(int64_t userId, size_t adding);

    std::shared_ptr<RunRepository> runs_;
    int maxRunsPerUser_;
    // The leaderboard is read-heavy and changes slowly, so it is cached for 30 s.
    // The lock is never held during the database query.
    std::mutex boardMu_;
    std::chrono::steady_clock::time_point boardAt_{};
    bool boardRefreshing_ = false;
    nlohmann::json boardEntries_;  // top 50; requests slice it
};

}  // namespace zc::server
