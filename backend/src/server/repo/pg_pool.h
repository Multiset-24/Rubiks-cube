#pragma once

#include <libpq-fe.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace zc::server {

// RAII owner of a PGresult.
class PgResult {
public:
    explicit PgResult(PGresult *r) : r_(r) {}
    PgResult(PgResult &&o) noexcept : r_(o.r_) { o.r_ = nullptr; }
    PgResult &operator=(PgResult &&) = delete;
    ~PgResult() { if (r_) PQclear(r_); }

    int rows() const { return PQntuples(r_); }
    bool isNull(int row, int col) const { return PQgetisnull(r_, row, col); }
    std::string str(int row, int col) const { return PQgetvalue(r_, row, col); }
    int64_t i64(int row, int col) const { return isNull(row, col) ? 0 : std::stoll(PQgetvalue(r_, row, col)); }
    bool boolean(int row, int col) const { return PQgetvalue(r_, row, col)[0] == 't'; }

private:
    PGresult *r_;
};

// Fixed-size pool of libpq connections. Requests borrow a connection for the
// duration of one query; broken connections are reset transparently. The pool
// size bounds the load we put on the database (Neon/Render free tiers allow
// only a few connections).
class PgPool {
public:
    PgPool(std::string url, int size);
    ~PgPool();

    class Lease {
    public:
        Lease(PgPool *p, PGconn *c) : pool_(p), conn_(c) {}
        Lease(Lease &&o) noexcept : pool_(o.pool_), conn_(o.conn_) { o.conn_ = nullptr; }
        ~Lease() { if (conn_) pool_->release(conn_); }
        PGconn *get() const { return conn_; }

    private:
        PgPool *pool_;
        PGconn *conn_;
    };

    Lease acquire(std::chrono::milliseconds timeout = std::chrono::milliseconds(3000));

    // Runs a parameterised query ($1, $2, ...). Parameters are sent separately
    // from the SQL text, so user input can never change the statement.
    PgResult exec(const std::string &sql, const std::vector<std::optional<std::string>> &params = {});
    PgResult execOn(PGconn *conn, const std::string &sql, const std::vector<std::optional<std::string>> &params = {});

    bool ping();

private:
    void release(PGconn *c);
    PGconn *connect();

    std::string url_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<PGconn *> idle_;
    int size_;
    int created_ = 0;
};

}  // namespace zc::server
