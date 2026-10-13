#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "server/repo/models.h"

namespace zc::server {

// Thrown when a unique constraint is violated (e.g. email already registered).
struct DuplicateError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Thrown when the store is unreachable; mapped to HTTP 503.
struct StorageUnavailable : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Storage interfaces used by the services. Implementations: in-memory (local
// dev and tests) and PostgreSQL.
class UserRepository {
public:
    virtual ~UserRepository() = default;
    virtual User create(const std::string &email, const std::string &displayName, const std::string &passwordHash) = 0;
    virtual std::optional<User> findByEmail(const std::string &email) = 0;  // email is normalised (lower case)
    virtual std::optional<User> findById(int64_t id) = 0;
    virtual void updatePasswordHash(int64_t id, const std::string &hash) = 0;
};

class RunRepository {
public:
    virtual ~RunRepository() = default;
    // Idempotent on (userId, clientId): repeating a create returns the stored run.
    virtual Run create(const Run &run) = 0;
    // Newest first, keyset-paginated on (createdAt, id).
    virtual RunPage list(int64_t userId, int limit, const std::optional<RunCursor> &after) = 0;
    virtual bool remove(int64_t userId, int64_t runId) = 0;
    virtual int64_t count(int64_t userId) = 0;
    virtual RunTotals totals(int64_t userId) = 0;
    // Durations of the most recent completed timer runs, newest first.
    virtual std::vector<int64_t> recentTimerTimes(int64_t userId, int n) = 0;
    // Distinct UTC days (YYYY-MM-DD) with at least one run, newest first.
    virtual std::vector<std::string> activeDays(int64_t userId, int maxDays) = 0;
    // Best completed timer single per user, counting only runs with a scramble
    // and at least `minMs` (times are self-reported; this drops obvious junk).
    virtual std::vector<LeaderboardEntry> leaderboard(int limit, int64_t minMs) = 0;
};

struct Storage {
    std::string kind;  // "memory" | "postgres"
    std::shared_ptr<UserRepository> users;
    std::shared_ptr<RunRepository> runs;
    std::function<bool()> healthy;
};

Storage makeMemoryStorage();
// Connects, runs migrations (under an advisory lock) and returns a pooled store.
Storage makePostgresStorage(const std::string &url, int poolSize);

std::string utcDay(int64_t epochMs);

}  // namespace zc::server
