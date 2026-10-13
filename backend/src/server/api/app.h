#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "server/config.h"
#include "server/infra/rate_limiter.h"
#include "server/repo/repositories.h"
#include "server/services/auth_service.h"
#include "server/services/run_service.h"
#include "server/services/solve_service.h"

namespace zc::server {

// Account features need the database; solving does not. The two are published
// separately so a database outage only affects accounts.
struct AccountServices {
    std::shared_ptr<AuthService> auth;
    std::shared_ptr<RunService> runs;
    Storage storage;
};

// HTTP transport layer: routes, middleware and the error envelope. Holds no
// business logic; it parses requests, calls services and serialises results.
class App {
public:
    App(const Config &config, std::atomic<bool> *shutdown);

    void attach(httplib::Server &server);
    void setSolver(std::shared_ptr<SolveService> solver);
    void setAccounts(std::shared_ptr<AccountServices> accounts);
    void setStartupError(const std::string &error);
    bool ready() const;  // the solver is up

private:
    using Handler = std::function<void(const httplib::Request &, httplib::Response &)>;

    void get(httplib::Server &s, const std::string &pattern, Handler h);
    void post(httplib::Server &s, const std::string &pattern, Handler h);
    void del(httplib::Server &s, const std::string &pattern, Handler h);
    Handler wrap(const std::string &pattern, Handler h);

    std::shared_ptr<SolveService> solver() const;        // throws 503 while warming up
    std::shared_ptr<AccountServices> accounts() const;   // throws 503 until the database is connected
    int64_t requireUser(const httplib::Request &req) const;
    void limit(RateLimiter &limiter, const httplib::Request &req, const char *pool);
    void limitUser(int64_t userId);  // per-account write limit for runs
    std::string clientIp(const httplib::Request &req) const;
    bool originAllowed(const std::string &origin) const;

    void handleSolve(const httplib::Request &req, httplib::Response &res, const nlohmann::json &body);

    const Config &config_;
    std::atomic<bool> *shutdown_;
    mutable std::mutex servicesMu_;
    std::shared_ptr<SolveService> solver_;
    std::shared_ptr<AccountServices> accounts_;
    std::string startupError_;
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();

    RateLimiter globalLimiter_;
    RateLimiter solveLimiter_;
    RateLimiter authLimiter_;
    RateLimiter runWriteLimiter_;  // keyed by user id
};

}  // namespace zc::server
