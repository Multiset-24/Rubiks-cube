#include <memory>

#include "server/infra/logger.h"
#include "server/repo/pg_pool.h"
#include "server/repo/repositories.h"

namespace zc::server {

namespace {

// Versioned, append-only migrations. Never edit a shipped migration; add a new one.
const std::vector<std::pair<int, std::string>> kMigrations = {
        {1, R"SQL(
CREATE TABLE users (
    id             BIGSERIAL PRIMARY KEY,
    email          TEXT NOT NULL UNIQUE,
    display_name   TEXT NOT NULL,
    password_hash  TEXT NOT NULL,
    created_at     TIMESTAMPTZ NOT NULL DEFAULT date_trunc('milliseconds', now())
);
CREATE TABLE runs (
    id               BIGSERIAL PRIMARY KEY,
    user_id          BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    kind             TEXT NOT NULL CHECK (kind IN ('tutor', 'fast', 'optimal', 'timer')),
    method           TEXT NOT NULL DEFAULT '',
    facelets         TEXT NOT NULL DEFAULT '',
    scramble         TEXT NOT NULL DEFAULT '',
    move_count       INT NOT NULL DEFAULT 0,
    duration_ms      BIGINT NOT NULL DEFAULT 0,
    completed        BOOLEAN NOT NULL DEFAULT false,
    quiz_correct     INT NOT NULL DEFAULT 0,
    quiz_total       INT NOT NULL DEFAULT 0,
    steps_completed  INT NOT NULL DEFAULT 0,
    total_steps      INT NOT NULL DEFAULT 0,
    client_id        TEXT,
    created_at       TIMESTAMPTZ NOT NULL DEFAULT date_trunc('milliseconds', now()),
    UNIQUE (user_id, client_id)
);
-- History pages: WHERE user_id = ? ORDER BY created_at DESC, id DESC (keyset pagination).
CREATE INDEX runs_user_created_idx ON runs (user_id, created_at DESC, id DESC);
-- Leaderboard: best completed timer single per user.
CREATE INDEX runs_timer_idx ON runs (user_id, duration_ms) WHERE kind = 'timer' AND completed;
)SQL"},
};

constexpr int64_t kMigrationLockKey = 0x5A454E43;  // "ZENC"

const char *kRunColumns =
        "id, user_id, kind, method, facelets, scramble, move_count, duration_ms, completed, quiz_correct, quiz_total, "
        "steps_completed, total_steps, COALESCE(client_id, ''), "
        "(extract(epoch FROM created_at) * 1000)::bigint";

Run runFromRow(const PgResult &r, int row) {
    Run x;
    x.id = r.i64(row, 0);
    x.userId = r.i64(row, 1);
    x.kind = r.str(row, 2);
    x.method = r.str(row, 3);
    x.facelets = r.str(row, 4);
    x.scramble = r.str(row, 5);
    x.moveCount = static_cast<int>(r.i64(row, 6));
    x.durationMs = r.i64(row, 7);
    x.completed = r.boolean(row, 8);
    x.quizCorrect = static_cast<int>(r.i64(row, 9));
    x.quizTotal = static_cast<int>(r.i64(row, 10));
    x.stepsCompleted = static_cast<int>(r.i64(row, 11));
    x.totalSteps = static_cast<int>(r.i64(row, 12));
    x.clientId = r.str(row, 13);
    x.createdAtMs = r.i64(row, 14);
    return x;
}

User userFromRow(const PgResult &r, int row) {
    return User{r.i64(row, 0), r.str(row, 1), r.str(row, 2), r.str(row, 3), r.i64(row, 4)};
}

const char *kUserColumns = "id, email, display_name, password_hash, (extract(epoch FROM created_at) * 1000)::bigint";

std::string s(int64_t v) { return std::to_string(v); }

void migrate(PgPool &pool) {
    auto lease = pool.acquire();
    PGconn *c = lease.get();
    // Session-level advisory lock: if several instances start together, only one
    // runs migrations; the others wait and then see them applied.
    pool.execOn(c, "SELECT pg_advisory_lock($1)", {s(kMigrationLockKey)});
    try {
        pool.execOn(c, "CREATE TABLE IF NOT EXISTS schema_migrations (version INT PRIMARY KEY, "
                       "applied_at TIMESTAMPTZ NOT NULL DEFAULT now())");
        for (const auto &[version, sql]: kMigrations) {
            auto done = pool.execOn(c, "SELECT 1 FROM schema_migrations WHERE version = $1", {s(version)});
            if (done.rows() > 0) continue;
            pool.execOn(c, "BEGIN");
            try {
                pool.execOn(c, sql);
                pool.execOn(c, "INSERT INTO schema_migrations (version) VALUES ($1)", {s(version)});
                pool.execOn(c, "COMMIT");
            } catch (...) {
                pool.execOn(c, "ROLLBACK");
                throw;
            }
            log().info("applied migration", {{"version", version}});
        }
    } catch (...) {
        pool.execOn(c, "SELECT pg_advisory_unlock($1)", {s(kMigrationLockKey)});
        throw;
    }
    pool.execOn(c, "SELECT pg_advisory_unlock($1)", {s(kMigrationLockKey)});
}

class PgUsers : public UserRepository {
public:
    explicit PgUsers(std::shared_ptr<PgPool> pool) : pool_(std::move(pool)) {}

    User create(const std::string &email, const std::string &displayName, const std::string &hash) override {
        auto r = pool_->exec(std::string("INSERT INTO users (email, display_name, password_hash) VALUES ($1, $2, $3) "
                                         "RETURNING ") + kUserColumns,
                             {email, displayName, hash});
        return userFromRow(r, 0);
    }
    std::optional<User> findByEmail(const std::string &email) override {
        auto r = pool_->exec(std::string("SELECT ") + kUserColumns + " FROM users WHERE email = $1", {email});
        if (r.rows() == 0) return std::nullopt;
        return userFromRow(r, 0);
    }
    std::optional<User> findById(int64_t id) override {
        auto r = pool_->exec(std::string("SELECT ") + kUserColumns + " FROM users WHERE id = $1", {s(id)});
        if (r.rows() == 0) return std::nullopt;
        return userFromRow(r, 0);
    }
    void updatePasswordHash(int64_t id, const std::string &hash) override {
        pool_->exec("UPDATE users SET password_hash = $2 WHERE id = $1", {s(id), hash});
    }

private:
    std::shared_ptr<PgPool> pool_;
};

class PgRuns : public RunRepository {
public:
    explicit PgRuns(std::shared_ptr<PgPool> pool) : pool_(std::move(pool)) {}

    Run create(const Run &x) override {
        std::optional<std::string> clientId;
        if (!x.clientId.empty()) clientId = x.clientId;
        std::optional<std::string> createdAt;
        if (x.createdAtMs > 0) createdAt = s(x.createdAtMs);
        auto r = pool_->exec(
                std::string("INSERT INTO runs (user_id, kind, method, facelets, scramble, move_count, duration_ms, "
                            "completed, quiz_correct, quiz_total, steps_completed, total_steps, client_id, created_at) "
                            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, "
                            "COALESCE('epoch'::timestamptz + $14::bigint * interval '1 millisecond', "
                            "date_trunc('milliseconds', now()))) "
                            "ON CONFLICT (user_id, client_id) DO NOTHING RETURNING ") + kRunColumns,
                {s(x.userId), x.kind, x.method, x.facelets, x.scramble, s(x.moveCount), s(x.durationMs),
                 x.completed ? "true" : "false", s(x.quizCorrect), s(x.quizTotal), s(x.stepsCompleted),
                 s(x.totalSteps), clientId, createdAt});
        if (r.rows() > 0) return runFromRow(r, 0);
        // Conflict: this client already created the run; return the stored one.
        auto existing = pool_->exec(std::string("SELECT ") + kRunColumns +
                                    " FROM runs WHERE user_id = $1 AND client_id = $2", {s(x.userId), clientId});
        return runFromRow(existing, 0);
    }

    RunPage list(int64_t userId, int limit, const std::optional<RunCursor> &after) override {
        std::string sql = std::string("SELECT ") + kRunColumns + " FROM runs WHERE user_id = $1";
        std::vector<std::optional<std::string>> params = {s(userId)};
        if (after) {
            // Keyset pagination: stable under concurrent inserts and O(log n) per
            // page via runs_user_created_idx, unlike OFFSET which rescans.
            sql += " AND (created_at, id) < ('epoch'::timestamptz + $2::bigint * interval '1 millisecond', $3::bigint)";
            params.push_back(s(after->createdAtMs));
            params.push_back(s(after->id));
        }
        sql += " ORDER BY created_at DESC, id DESC LIMIT " + std::to_string(limit + 1);
        auto r = pool_->exec(sql, params);
        RunPage page;
        for (int i = 0; i < r.rows() && i < limit; ++i) page.runs.push_back(runFromRow(r, i));
        if (r.rows() > limit) page.next = RunCursor{page.runs.back().createdAtMs, page.runs.back().id};
        return page;
    }

    bool remove(int64_t userId, int64_t runId) override {
        auto r = pool_->exec("DELETE FROM runs WHERE id = $1 AND user_id = $2 RETURNING id", {s(runId), s(userId)});
        return r.rows() > 0;
    }

    int64_t count(int64_t userId) override {
        return pool_->exec("SELECT count(*) FROM runs WHERE user_id = $1", {s(userId)}).i64(0, 0);
    }

    RunTotals totals(int64_t userId) override {
        RunTotals t;
        auto r = pool_->exec(
                "SELECT kind, count(*), count(*) FILTER (WHERE completed), COALESCE(sum(quiz_correct), 0), "
                "COALESCE(sum(quiz_total), 0), COALESCE(sum(move_count) FILTER (WHERE completed AND kind <> 'timer'), 0), "
                "min(duration_ms) FILTER (WHERE kind = 'timer' AND completed AND duration_ms > 0) "
                "FROM runs WHERE user_id = $1 GROUP BY kind ORDER BY kind",
                {s(userId)});
        for (int i = 0; i < r.rows(); ++i) {
            t.byKind.push_back(KindTotals{r.str(i, 0), r.i64(i, 1), r.i64(i, 2)});
            t.quizCorrect += r.i64(i, 3);
            t.quizTotal += r.i64(i, 4);
            t.movesFollowed += r.i64(i, 5);
            if (!r.isNull(i, 6)) t.bestTimerMs = r.i64(i, 6);
        }
        return t;
    }

    std::vector<int64_t> recentTimerTimes(int64_t userId, int n) override {
        auto r = pool_->exec("SELECT duration_ms FROM runs WHERE user_id = $1 AND kind = 'timer' AND completed "
                             "ORDER BY created_at DESC, id DESC LIMIT $2", {s(userId), s(n)});
        std::vector<int64_t> out;
        for (int i = 0; i < r.rows(); ++i) out.push_back(r.i64(i, 0));
        return out;
    }

    std::vector<std::string> activeDays(int64_t userId, int maxDays) override {
        auto r = pool_->exec("SELECT DISTINCT to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD') AS d FROM runs "
                             "WHERE user_id = $1 ORDER BY d DESC LIMIT $2", {s(userId), s(maxDays)});
        std::vector<std::string> out;
        for (int i = 0; i < r.rows(); ++i) out.push_back(r.str(i, 0));
        return out;
    }

    std::vector<LeaderboardEntry> leaderboard(int limit, int64_t minMs) override {
        auto r = pool_->exec(
                "SELECT u.display_name, min(r.duration_ms) AS best, count(*) FROM runs r JOIN users u ON u.id = r.user_id "
                "WHERE r.kind = 'timer' AND r.completed AND r.duration_ms >= $2 AND r.scramble <> '' "
                "GROUP BY u.id, u.display_name ORDER BY best ASC LIMIT $1",
                {s(limit), s(minMs)});
        std::vector<LeaderboardEntry> out;
        for (int i = 0; i < r.rows(); ++i) out.push_back(LeaderboardEntry{r.str(i, 0), r.i64(i, 1), r.i64(i, 2)});
        return out;
    }

private:
    std::shared_ptr<PgPool> pool_;
};

}  // namespace

Storage makePostgresStorage(const std::string &url, int poolSize) {
    auto pool = std::make_shared<PgPool>(url, poolSize);
    migrate(*pool);
    Storage st;
    st.kind = "postgres";
    st.users = std::make_shared<PgUsers>(pool);
    st.runs = std::make_shared<PgRuns>(pool);
    st.healthy = [pool] { return pool->ping(); };
    return st;
}

}  // namespace zc::server
