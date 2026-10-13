#include "server/api/app.h"

#include <regex>
#include <set>

#include "server/infra/crypto.h"
#include "server/infra/logger.h"
#include "server/infra/metrics.h"
#include "server/services/api_error.h"

namespace zc::server {

using nlohmann::json;

namespace {

// Per-request context. cpp-httplib serves each request start-to-finish on one
// worker thread, so a thread_local is a safe place for it.
struct RequestContext {
    std::chrono::steady_clock::time_point start;
    std::string requestId;
    std::string route = "unmatched";
    std::string ip;
    std::string errorCode;
};
thread_local RequestContext tlsCtx;

void sendJson(httplib::Response &res, int status, const json &body) {
    res.status = status;
    res.set_content(body.dump(-1, ' ', false, json::error_handler_t::replace), "application/json");
}

void sendError(httplib::Response &res, int status, const std::string &code, const std::string &message,
               const json &details = nullptr, int retryAfter = 0) {
    json err = {{"code", code}, {"message", message}, {"requestId", tlsCtx.requestId}};
    if (!details.is_null()) err["details"] = details;
    if (retryAfter > 0) res.set_header("Retry-After", std::to_string(retryAfter));
    tlsCtx.errorCode = code;
    sendJson(res, status, {{"error", err}});
}

json parseBody(const httplib::Request &req) {
    if (req.body.empty()) return json::object();
    json j = json::parse(req.body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) throw ApiError(400, "BAD_JSON", "Request body must be a JSON object.");
    return j;
}

ColorNames parseColorNames(const json &body) {
    ColorNames names = defaultColorNames();
    if (!body.contains("colorNames")) return names;
    const json &cn = body["colorNames"];
    if (!cn.is_object()) throw ApiError(400, "BAD_REQUEST", "colorNames must map faces (U R F D L B) to names.");
    for (int f = 0; f < kNumFaces; ++f) {
        std::string key(1, kFaceChars[f]);
        if (cn.contains(key) && cn[key].is_string()) {
            std::string n = cn[key].get<std::string>();
            if (n.empty() || n.size() > 24) throw ApiError(400, "BAD_REQUEST", "Colour names must be 1-24 characters.");
            names[static_cast<size_t>(f)] = n;
        }
    }
    return names;
}

std::string requireString(const json &body, const char *key, size_t maxLen = 1000) {
    if (!body.contains(key) || !body[key].is_string()) {
        throw ApiError(400, "BAD_REQUEST", std::string("Missing string field '") + key + "'.");
    }
    std::string v = body[key].get<std::string>();
    if (v.size() > maxLen) throw ApiError(400, "BAD_REQUEST", std::string("Field '") + key + "' is too long.");
    return v;
}

bool validRequestId(const std::string &id) {
    static const std::regex re("^[A-Za-z0-9._-]{1,64}$");
    return std::regex_match(id, re);
}

}  // namespace

App::App(const Config &config, std::atomic<bool> *shutdown)
        : config_(config), shutdown_(shutdown), globalLimiter_(config.rateLimitRps, config.rateLimitBurst),
          solveLimiter_(config.solveRateRps, config.solveRateBurst),
          authLimiter_(config.authRatePerMinute / 60.0, config.authRatePerMinute),
          runWriteLimiter_(config.runWritesPerMinute / 60.0, config.runWritesPerMinute) {
    auto &m = Metrics::instance();
    m.describe("http_requests_total", "counter", "HTTP requests by route template, method and status.");
    m.describe("http_request_duration_seconds", "histogram", "HTTP request latency by route template.");
    m.describe("solver_duration_seconds", "histogram", "Time spent inside a solver.");
    m.describe("solve_cache_requests_total", "counter", "Solve cache lookups by result (hit/miss).");
    m.describe("solver_fallbacks_total", "counter", "Optimal searches that fell back to the fast solver.");
    m.describe("rate_limited_total", "counter", "Requests rejected by a rate limiter.");
    m.describe("bulkhead_rejections_total", "counter", "Requests shed because a bulkhead was full.");
    m.set("app_info", 1, {{"version", config.version}});
}

void App::setSolver(std::shared_ptr<SolveService> s) {
    std::lock_guard<std::mutex> lock(servicesMu_);
    solver_ = std::move(s);
}

void App::setAccounts(std::shared_ptr<AccountServices> a) {
    std::lock_guard<std::mutex> lock(servicesMu_);
    accounts_ = std::move(a);
}

void App::setStartupError(const std::string &error) {
    std::lock_guard<std::mutex> lock(servicesMu_);
    startupError_ = error;
}

bool App::ready() const {
    std::lock_guard<std::mutex> lock(servicesMu_);
    return solver_ != nullptr;
}

std::shared_ptr<SolveService> App::solver() const {
    std::lock_guard<std::mutex> lock(servicesMu_);
    if (!solver_) {
        throw ApiError(503, "WARMING_UP", "The server is starting up. Please retry in a few seconds.", nullptr, 3);
    }
    return solver_;
}

std::shared_ptr<AccountServices> App::accounts() const {
    std::lock_guard<std::mutex> lock(servicesMu_);
    if (!accounts_) {
        throw ApiError(503, "STORAGE_UNAVAILABLE",
                       "Accounts are temporarily unavailable. Solving still works; your runs are kept on this device.",
                       nullptr, 10);
    }
    return accounts_;
}

std::string App::clientIp(const httplib::Request &req) const {
    if (config_.trustedProxyHops <= 0) return req.remote_addr;
    // Each proxy appends the address it received the request from, so only the
    // last `hops` entries are trustworthy; anything to their left came from the
    // client and could be forged to dodge rate limits.
    std::vector<std::string> entries;
    std::string xff = req.get_header_value("X-Forwarded-For");
    size_t start = 0;
    while (start <= xff.size()) {
        size_t comma = xff.find(',', start);
        std::string item = xff.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        size_t a = item.find_first_not_of(" \t"), b = item.find_last_not_of(" \t");
        if (a != std::string::npos) entries.push_back(item.substr(a, b - a + 1));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    if (entries.empty()) return req.remote_addr;
    size_t hops = static_cast<size_t>(config_.trustedProxyHops);
    std::string ip = entries.size() >= hops ? entries[entries.size() - hops] : entries.front();
    return ip.size() <= 64 ? ip : req.remote_addr;
}

bool App::originAllowed(const std::string &origin) const {
    for (const auto &o: config_.corsOrigins) {
        if (o == "*" || o == origin) return true;
        // "https://*.example.com" or "https://*--site.netlify.app" (deploy previews):
        // the part before '*' must be a prefix and the part after it a suffix.
        size_t star = o.find('*');
        if (star == std::string::npos) continue;
        std::string prefix = o.substr(0, star), suffix = o.substr(star + 1);
        if (origin.size() > prefix.size() + suffix.size() && origin.compare(0, prefix.size(), prefix) == 0 &&
            origin.compare(origin.size() - suffix.size(), suffix.size(), suffix) == 0 &&
            origin.find('/', prefix.size()) == std::string::npos) {
            return true;
        }
    }
    return false;
}

void App::limitUser(int64_t userId) {
    auto d = runWriteLimiter_.acquire("u:" + std::to_string(userId));
    if (!d.allowed) {
        Metrics::instance().inc("rate_limited_total", {{"pool", "run_writes"}});
        throw ApiError(429, "RATE_LIMITED", "Too many saves in a short time. Please slow down.", nullptr,
                       static_cast<int>(d.retryAfterSeconds) + 1);
    }
}

void App::limit(RateLimiter &limiter, const httplib::Request &req, const char *pool) {
    auto d = limiter.acquire(clientIp(req));
    if (!d.allowed) {
        Metrics::instance().inc("rate_limited_total", {{"pool", pool}});
        throw ApiError(429, "RATE_LIMITED", "Too many requests. Slow down a little.", nullptr,
                       static_cast<int>(d.retryAfterSeconds) + 1);
    }
}

int64_t App::requireUser(const httplib::Request &req) const {
    return accounts()->auth->authenticate(req.get_header_value("Authorization"));
}

App::Handler App::wrap(const std::string &pattern, Handler h) {
    bool probe = pattern == "/healthz" || pattern == "/readyz" || pattern == "/metrics";
    return [this, pattern, h, probe](const httplib::Request &req, httplib::Response &res) {
        tlsCtx.route = pattern;
        try {
            // The global limit is checked here rather than before routing: by now
            // httplib has read the request body, so answering early can't leave
            // body bytes on the connection to be parsed as another request.
            if (!probe) limit(globalLimiter_, req, "global");
            h(req, res);
        } catch (const ApiError &e) {
            sendError(res, e.status, e.code, e.what(), e.details, e.retryAfterSeconds);
        } catch (const DuplicateError &e) {
            sendError(res, 409, "CONFLICT", "That already exists.");
        } catch (const StorageUnavailable &e) {
            log().error("storage unavailable", {{"request_id", tlsCtx.requestId}, {"error", e.what()}});
            sendError(res, 503, "STORAGE_UNAVAILABLE", "Saving is temporarily unavailable. Please retry.", nullptr, 5);
        } catch (const json::exception &e) {
            sendError(res, 400, "BAD_REQUEST", std::string("Malformed request: ") + e.what());
        } catch (const std::exception &e) {
            log().error("unhandled error", {{"request_id", tlsCtx.requestId}, {"route", pattern}, {"error", e.what()}});
            sendError(res, 500, "INTERNAL", "Something went wrong on our side.");
        }
    };
}

void App::get(httplib::Server &s, const std::string &p, Handler h) { s.Get(p, wrap(p, std::move(h))); }
void App::post(httplib::Server &s, const std::string &p, Handler h) { s.Post(p, wrap(p, std::move(h))); }
void App::del(httplib::Server &s, const std::string &p, Handler h) { s.Delete(p, wrap(p, std::move(h))); }

void App::handleSolve(const httplib::Request &req, httplib::Response &res, const json &body) {
    limit(solveLimiter_, req, "solve");
    SolveRequest sr;
    sr.cube = requireString(body, "cube", 200);
    if (body.contains("method")) sr.method = requireString(body, "method", 32);
    sr.names = parseColorNames(body);
    auto out = solver()->solve(sr);
    res.set_header("X-Cache", out.cacheHit ? "HIT" : (out.coalesced ? "COALESCED" : "MISS"));
    sendJson(res, 200, out.body);
}

void App::attach(httplib::Server &server) {
    // ---------------------------------------------------------------- middleware
    server.set_pre_routing_handler([this](const httplib::Request &req, httplib::Response &res) {
        tlsCtx = RequestContext{};
        tlsCtx.start = std::chrono::steady_clock::now();
        std::string rid = req.get_header_value("X-Request-Id");
        tlsCtx.requestId = validRequestId(rid) ? rid : crypto::randomHex(8);
        tlsCtx.ip = clientIp(req);

        res.set_header("X-Request-Id", tlsCtx.requestId);
        res.set_header("X-Content-Type-Options", "nosniff");
        res.set_header("X-Frame-Options", "DENY");
        res.set_header("Referrer-Policy", "no-referrer");
        std::string origin = req.get_header_value("Origin");
        if (!origin.empty() && originAllowed(origin)) {
            bool wildcard = std::find(config_.corsOrigins.begin(), config_.corsOrigins.end(), "*") != config_.corsOrigins.end();
            res.set_header("Access-Control-Allow-Origin", wildcard ? "*" : origin);
            if (!wildcard) res.set_header("Vary", "Origin");
            res.set_header("Access-Control-Expose-Headers", "X-Request-Id, X-Cache, Retry-After");
        }
        if (req.method == "OPTIONS") {
            tlsCtx.route = "preflight";
            res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Request-Id");
            res.set_header("Access-Control-Max-Age", "86400");
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    server.set_error_handler([](const httplib::Request &req, httplib::Response &res) {
        if (tlsCtx.requestId.empty()) {
            // httplib rejected the request before routing (bad request line,
            // URI too long, bad Range...), so the pre-routing handler never ran.
            tlsCtx.start = std::chrono::steady_clock::now();
            tlsCtx.requestId = crypto::randomHex(8);
            tlsCtx.route = "rejected";
            tlsCtx.ip = req.remote_addr;
            res.set_header("X-Request-Id", tlsCtx.requestId);
            res.set_header("X-Content-Type-Options", "nosniff");
        }
        if (!res.body.empty()) return httplib::Server::HandlerResponse::Unhandled;
        if (res.status == 404) sendError(res, 404, "NOT_FOUND", "No such endpoint.");
        else if (res.status == 413) sendError(res, 413, "PAYLOAD_TOO_LARGE", "Request body is too large.");
        else if (res.status == 405) sendError(res, 405, "METHOD_NOT_ALLOWED", "Method not allowed.");
        else sendError(res, res.status, "HTTP_ERROR", "Request failed.");
        return httplib::Server::HandlerResponse::Handled;
    });

    server.set_exception_handler([](const httplib::Request &, httplib::Response &res, std::exception_ptr) {
        sendError(res, 500, "INTERNAL", "Something went wrong on our side.");
    });

    server.set_logger([](const httplib::Request &req, const httplib::Response &res) {
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - tlsCtx.start).count();
        // Metric labels must stay a small fixed set: raw methods from malformed
        // requests would otherwise create a new series per request.
        static const std::set<std::string> kMethods = {"GET", "HEAD", "POST", "PUT", "DELETE", "OPTIONS", "PATCH"};
        const std::string method = kMethods.count(req.method) ? req.method : "OTHER";
        auto &m = Metrics::instance();
        m.inc("http_requests_total", {{"route", tlsCtx.route}, {"method", method}, {"status", std::to_string(res.status)}});
        m.observe("http_request_duration_seconds", secs, {{"route", tlsCtx.route}});
        if (tlsCtx.route != "/healthz" && tlsCtx.route != "/metrics") {
            json fields = {{"request_id", tlsCtx.requestId}, {"method", method}, {"route", tlsCtx.route},
                           {"path", req.path.substr(0, 256)}, {"status", res.status}, {"duration_ms", secs * 1000},
                           {"ip", tlsCtx.ip}};
            if (!tlsCtx.errorCode.empty()) fields["error"] = tlsCtx.errorCode;
            if (res.has_header("X-Cache")) fields["cache"] = res.get_header_value("X-Cache");
            if (res.status >= 500) log().error("request", fields);
            else log().info("request", fields);
        }
        tlsCtx = RequestContext{};  // the next request on this thread starts clean
    });

    // ---------------------------------------------------------------- operations
    get(server, "/healthz", [](const httplib::Request &, httplib::Response &res) {
        // Liveness: the process is up and serving. Never depends on downstreams.
        sendJson(res, 200, {{"status", "ok"}});
    });

    get(server, "/readyz", [this](const httplib::Request &, httplib::Response &res) {
        // Readiness: the solver is loaded. It deliberately does not depend on the
        // database, so a database outage doesn't get the instance restarted and
        // take solving down with it. Storage state is reported as data.
        bool solverUp, accountsUp, failed;
        {
            std::lock_guard<std::mutex> lock(servicesMu_);
            solverUp = solver_ != nullptr;
            accountsUp = accounts_ != nullptr;
            failed = !startupError_.empty();
        }
        json storage = {{"kind", config_.databaseUrl.empty() ? "memory" : "postgres"}, {"available", accountsUp}};
        if (!solverUp) {
            // Details stay in the log: a connection error can echo the database URL.
            sendJson(res, 503, {{"status", failed ? "failed" : "warming_up"}, {"storage", storage}});
            return;
        }
        sendJson(res, 200, {{"status", "ready"}, {"storage", storage}});
    });

    get(server, "/metrics", [this](const httplib::Request &req, httplib::Response &res) {
        if (!config_.metricsToken.empty() &&
            !crypto::constantTimeEquals(req.get_header_value("Authorization"), "Bearer " + config_.metricsToken)) {
            throw ApiError(401, "UNAUTHENTICATED", "Metrics require a token.");
        }
        res.set_content(Metrics::instance().render(), "text/plain; version=0.0.4");
    });

    get(server, "/api/v1/info", [this](const httplib::Request &, httplib::Response &res) {
        auto solve = solver();
        bool accountsUp;
        {
            std::lock_guard<std::mutex> lock(servicesMu_);
            accountsUp = accounts_ != nullptr;
        }
        double uptime = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
        sendJson(res, 200, {{"name", "zen-cube-api"}, {"version", config_.version}, {"uptimeSeconds", uptime},
                            {"storage", {{"kind", config_.databaseUrl.empty() ? "memory" : "postgres"},
                                         {"available", accountsUp}}},
                            {"solvers", solve->describeSolvers()},
                            {"limits", {{"solveTimeoutMs", config_.solveTimeout.count()},
                                        {"optimalTimeoutMs", config_.optimalTimeout.count()},
                                        {"solveRatePerSecond", config_.solveRateRps}}}});
    });

    // ---------------------------------------------------------------- solving
    post(server, "/api/v1/validate", [this](const httplib::Request &req, httplib::Response &res) {
        json body = parseBody(req);
        sendJson(res, 200, solver()->validate(requireString(body, "cube", 200), parseColorNames(body)));
    });

    post(server, "/api/v1/solve", [this](const httplib::Request &req, httplib::Response &res) {
        handleSolve(req, res, parseBody(req));
    });

    // GET variant: the answer is a pure function of the query, so CDNs and
    // browsers may cache it indefinitely.
    get(server, "/api/v1/solve", [this](const httplib::Request &req, httplib::Response &res) {
        json body = {{"cube", req.get_param_value("cube")},
                     {"method", req.has_param("method") ? req.get_param_value("method") : "beginner"}};
        handleSolve(req, res, body);
        // Exact answers never change; a fallback answer might be optimal next time.
        if (res.status == 200) {
            res.set_header("Cache-Control", res.body.find("\"fallbackFrom\"") == std::string::npos
                                            ? "public, max-age=31536000, immutable" : "no-store");
        }
    });

    get(server, "/api/v1/scramble", [this](const httplib::Request &req, httplib::Response &res) {
        limit(solveLimiter_, req, "solve");
        res.set_header("Cache-Control", "no-store");
        sendJson(res, 200, solver()->scramble());
    });

    // ---------------------------------------------------------------- accounts
    post(server, "/api/v1/auth/register", [this](const httplib::Request &req, httplib::Response &res) {
        limit(authLimiter_, req, "auth");
        json body = parseBody(req);
        auto r = accounts()->auth->registerUser(requireString(body, "email", 300), requireString(body, "password", 200),
                                                requireString(body, "displayName", 100));
        res.set_header("Cache-Control", "no-store");
        sendJson(res, 201, {{"token", r.token}, {"user", AuthService::userJson(r.user)}});
    });

    post(server, "/api/v1/auth/login", [this](const httplib::Request &req, httplib::Response &res) {
        limit(authLimiter_, req, "auth");
        json body = parseBody(req);
        auto r = accounts()->auth->login(requireString(body, "email", 300), requireString(body, "password", 200));
        res.set_header("Cache-Control", "no-store");
        sendJson(res, 200, {{"token", r.token}, {"user", AuthService::userJson(r.user)}});
    });

    get(server, "/api/v1/me", [this](const httplib::Request &req, httplib::Response &res) {
        int64_t uid = requireUser(req);
        auto user = accounts()->auth->findUser(uid);
        if (!user) throw ApiError(401, "UNAUTHENTICATED", "Account not found. Please sign in again.");
        res.set_header("Cache-Control", "no-store");
        sendJson(res, 200, {{"user", AuthService::userJson(*user)}});
    });

    // ---------------------------------------------------------------- runs
    post(server, "/api/v1/runs", [this](const httplib::Request &req, httplib::Response &res) {
        int64_t uid = requireUser(req);
        limitUser(uid);
        sendJson(res, 201, accounts()->runs->create(uid, parseBody(req)));
    });

    post(server, "/api/v1/runs/batch", [this](const httplib::Request &req, httplib::Response &res) {
        int64_t uid = requireUser(req);
        limitUser(uid);
        sendJson(res, 201, accounts()->runs->importBatch(uid, parseBody(req)));
    });

    get(server, "/api/v1/runs", [this](const httplib::Request &req, httplib::Response &res) {
        int64_t uid = requireUser(req);
        int limitN = 20;
        if (req.has_param("limit")) {
            try {
                limitN = std::stoi(req.get_param_value("limit"));
            } catch (const std::exception &) {
                throw ApiError(400, "BAD_REQUEST", "limit must be a number.");
            }
        }
        res.set_header("Cache-Control", "no-store");
        sendJson(res, 200, accounts()->runs->list(uid, limitN, req.get_param_value("cursor")));
    });

    del(server, "/api/v1/runs/:id", [this](const httplib::Request &req, httplib::Response &res) {
        int64_t uid = requireUser(req);
        int64_t id;
        try {
            id = std::stoll(req.path_params.at("id"));
        } catch (const std::exception &) {
            throw ApiError(400, "BAD_REQUEST", "Run id must be a number.");
        }
        accounts()->runs->remove(uid, id);
        res.status = 204;
    });

    get(server, "/api/v1/stats", [this](const httplib::Request &req, httplib::Response &res) {
        int64_t uid = requireUser(req);
        res.set_header("Cache-Control", "no-store");
        sendJson(res, 200, accounts()->runs->stats(uid));
    });

    get(server, "/api/v1/leaderboard", [this](const httplib::Request &req, httplib::Response &res) {
        int n = 20;
        if (req.has_param("limit")) {
            try {
                n = std::stoi(req.get_param_value("limit"));
            } catch (const std::exception &) {
                throw ApiError(400, "BAD_REQUEST", "limit must be a number.");
            }
        }
        res.set_header("Cache-Control", "public, max-age=30");
        sendJson(res, 200, accounts()->runs->leaderboard(n));
    });
}

}  // namespace zc::server
