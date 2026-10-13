#include "server/repo/pg_pool.h"

#include "server/infra/logger.h"
#include "server/infra/metrics.h"
#include "server/repo/repositories.h"

namespace zc::server {

PgPool::PgPool(std::string url, int size) : url_(std::move(url)), size_(size < 1 ? 1 : size) {
    // Fail fast at start-up if the database is unreachable or misconfigured.
    PGconn *c = connect();
    idle_.push_back(c);
    created_ = 1;
}

PgPool::~PgPool() {
    for (PGconn *c: idle_) PQfinish(c);
}

PGconn *PgPool::connect() {
    PGconn *c = PQconnectdb(url_.c_str());
    if (PQstatus(c) != CONNECTION_OK) {
        std::string err = PQerrorMessage(c);
        PQfinish(c);
        throw StorageUnavailable("cannot connect to database: " + err);
    }
    return c;
}

PgPool::Lease PgPool::acquire(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mu_);
    auto start = std::chrono::steady_clock::now();
    while (idle_.empty()) {
        if (created_ < size_) {
            ++created_;
            lock.unlock();
            try {
                return Lease(this, connect());
            } catch (...) {
                std::lock_guard<std::mutex> relock(mu_);
                --created_;
                throw;
            }
        }
        if (cv_.wait_until(lock, start + timeout) == std::cv_status::timeout && idle_.empty()) {
            Metrics::instance().inc("db_pool_timeouts_total");
            throw StorageUnavailable("database pool exhausted");
        }
    }
    PGconn *c = idle_.back();
    idle_.pop_back();
    lock.unlock();
    Metrics::instance().observe("db_pool_wait_seconds",
                                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    if (PQstatus(c) != CONNECTION_OK) {
        log().warn("resetting broken database connection");
        PQreset(c);
    }
    return Lease(this, c);
}

void PgPool::release(PGconn *c) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        idle_.push_back(c);
    }
    cv_.notify_one();
}

PgResult PgPool::execOn(PGconn *conn, const std::string &sql, const std::vector<std::optional<std::string>> &params) {
    std::vector<const char *> values;
    values.reserve(params.size());
    for (const auto &p: params) values.push_back(p ? p->c_str() : nullptr);
    auto start = std::chrono::steady_clock::now();
    // Statements with parameters use the extended protocol (values travel apart
    // from the SQL). Parameterless statements are our own constants, such as
    // multi-statement migrations, which only the simple protocol accepts.
    PGresult *r = params.empty()
                  ? PQexec(conn, sql.c_str())
                  : PQexecParams(conn, sql.c_str(), static_cast<int>(params.size()), nullptr, values.data(), nullptr,
                                 nullptr, 0);
    Metrics::instance().observe("db_query_duration_seconds",
                                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    ExecStatusType st = PQresultStatus(r);
    if (st == PGRES_COMMAND_OK || st == PGRES_TUPLES_OK) return PgResult(r);

    const char *state = PQresultErrorField(r, PG_DIAG_SQLSTATE);
    std::string err = PQresultErrorMessage(r);
    std::string sqlstate = state ? state : "";
    PQclear(r);
    Metrics::instance().inc("db_errors_total", {{"sqlstate", sqlstate.empty() ? "none" : sqlstate}});
    if (sqlstate == "23505") throw DuplicateError(err);
    if (PQstatus(conn) != CONNECTION_OK) throw StorageUnavailable(err);
    throw std::runtime_error("database error: " + err);
}

PgResult PgPool::exec(const std::string &sql, const std::vector<std::optional<std::string>> &params) {
    Lease lease = acquire();
    try {
        return execOn(lease.get(), sql, params);
    } catch (const StorageUnavailable &) {
        // One retry on a fresh connection covers dropped idle connections
        // (common with serverless Postgres that suspends when idle).
        PQreset(lease.get());
        return execOn(lease.get(), sql, params);
    }
}

bool PgPool::ping() {
    try {
        exec("SELECT 1");
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

}  // namespace zc::server
