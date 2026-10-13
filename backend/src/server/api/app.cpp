#include "server/api/app.h"

#include <regex>

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
          authLimiter_(config.authRatePerMinute / 60.0, config.authRatePerMinute) {
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

void App::setServices(std::shared_ptr<Services> s) {
    std::lock_guard<std::mutex> lock(servicesMu_);
    services_ = std::move(s);
}

void App::setStartupError(const std::string &error) {
    std::lock_guard<std::mutex> lock(servicesMu_);
    startupError_ = error;
}

bool App::ready() const {
    std::lock_guard<std::mutex> lock(servicesMu_);
    return services_ != nullptr;
}

std::shared_ptr<Services> App::services() const {
    std::lock_guard<std::mutex> lock(servicesMu_);
    if (!services_) {
        throw ApiError(503, "WARMING_UP", "The server is starting up. Please retry in a few seconds.", nullptr, 3);
    }
    return services_;
}

std::string App::clientIp(const httplib::Request &req) const {
    if (config_.trustProxy) {
        // Behind Render's load balancer the original client is the first X-Forwarded-For entry.
        std::string xff = req.get_header_value("X-Forwarded-For");
        if (!xff.empty()) {
            std::string first = xff.substr(0, xff.find(','));
            size_t a = first.find_first_not_of(' '), b = first.find_last_not_of(' ');
            if (a != std::string::npos) return first.substr(a, b - a + 1);
        }
    }
    return req.remote_addr;
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

void App::limit(RateLimiter &limiter, const httplib::Request &req, const char *pool) {
    auto d = limiter.acquire(clientIp(req));
    if (!d.allowed) {
        Metrics::instance().inc("rate_limited_total", {{"pool", pool}});
        throw ApiError(429, "RATE_LIMITED", "Too many requests. Slow down a little.", nullptr,
                       static_cast<int>(d.retryAfterSeconds) + 1);
    }
}

int64_t App::requireUser(const httplib::Request &req) const {
    return services()->auth->authenticate(req.get_header_value("Authorization"));
}

App::Handler App::wrap(const std::string &pattern, Handler h) {
    return [pattern, h](const httplib::Request &req, httplib::Response &res) {
        tlsCtx.route = pattern;
        try {
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
    auto out = services()->solve->solve(sr);
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
        bool infra = req.path == "/healthz" || req.path == "/readyz" || req.path == "/metrics";
        if (!infra) {
            auto d = globalLimiter_.acquire(tlsCtx.ip);
            if (!d.allowed) {
                tlsCtx.route = "rate_limited";
                Metrics::instance().inc("rate_limited_total", {{"pool", "global"}});
                sendError(res, 429, "RATE_LIMITED", "Too many requests. Slow down a little.", nullptr,
                          static_cast<int>(d.retryAfterSeconds) + 1);
                return httplib::Server::HandlerResponse::Handled;
            }
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    server.set_error_handler([](const httplib::Request &, httplib::Response &res) {
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
        auto &m = Metrics::instance();
        m.inc("http_requests_total", {{"route", tlsCtx.route}, {"method", req.method}, {"status", std::to_string(res.status)}});
        m.observe("http_request_duration_seconds", secs, {{"route", tlsCtx.route}});
        if (tlsCtx.route == "/healthz" || tlsCtx.route == "/metrics") return;  // keep logs quiet
        json fields = {{"request_id", tlsCtx.requestId}, {"method", req.method}, {"route", tlsCtx.route},
                       {"path", req.path}, {"status", res.status}, {"duration_ms", secs * 1000}, {"ip", tlsCtx.ip}};
        if (!tlsCtx.errorCode.empty()) fields["error"] = tlsCtx.errorCode;
        if (res.has_header("X-Cache")) fields["cache"] = res.get_header_value("X-Cache");
        if (res.status >= 500) log().error("request", fields);
        else log().info("request", fields);
    });

    // ---------------------------------------------------------------- operations
    get(server, "/healthz", [](const httplib::Request &, httplib::Response &res) {
        // Liveness: the process is up and serving. Never depends on downstreams.
        sendJson(res, 200, {{"status", "ok"}});
    });

    get(server, "/readyz", [this](const httplib::Request &, httplib::Response &res) {
        // Readiness: safe to route traffic here (tables loaded, database reachable).
        std::shared_ptr<Services> s;
        std::string startupError;
        {
            std::lock_guard<std::mutex> lock(servicesMu_);
            s = services_;
            startupError = startupError_;
        }
        if (!s) {
            json body = {{"status", "warming_up"}};
            if (!startupError.empty()) body["error"] = startupError;
            sendJson(res, 503, body);
            return;
        }
        bool healthy;
        {
            std::lock_guard<std::mutex> lock(healthMu_);
            auto now = std::chrono::steady_clock::now();
            if (now - healthCheckedAt_ > std::chrono::seconds(5)) {  // don't hammer the database
                lastHealthy_ = s->storage.healthy();
                healthCheckedAt_ = now;
            }
            healthy = lastHealthy_;
        }
        sendJson(res, healthy ? 200 : 503, {{"status", healthy ? "ready" : "degraded"}, {"storage", s->storage.kind}});
    });

    get(server, "/metrics", [](const httplib::Request &, httplib::Response &res) {
        res.set_content(Metrics::instance().render(), "text/plain; version=0.0.4");
    });

    get(server, "/api/v1/info", [this](const httplib::Request &, httplib::Response &res) {
        auto s = services();
        double uptime = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
        sendJson(res, 200, {{"name", "zen-cube-api"}, {"version", config_.version}, {"uptimeSeconds", uptime},
                            {"storage", s->storage.kind}, {"solvers", s->solve->describeSolvers()},
                            {"limits", {{"solveTimeoutMs", config_.solveTimeout.count()},
                                        {"optimalTimeoutMs", config_.optimalTimeout.count()},
                                        {"solveRatePerSecond", config_.solveRateRps}}}});
    });

    // ---------------------------------------------------------------- solving
    post(server, "/api/v1/validate", [this](const httplib::Request &req, httplib::Response &res) {
        json body = parseBody(req);
        sendJson(res, 200, services()->solve->validate(requireString(body, "cube", 200), parseColorNames(body)));
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
        if (res.status == 200) res.set_header("Cache-Control", "public, max-age=31536000, immutable");
    });

    get(server, "/api/v1/scramble", [this](const httplib::Request &req, httplib::Response &res) {
        limit(solveLimiter_, req, "solve");
        res.set_header("Cache-Control", "no-store");
        sendJson(res, 200, services()->solve->scramble());
    });

    // ---------------------------------------------------------------- accounts
    post(server, "/api/v1/auth/register", [this](const httplib::Request &req, httplib::Response &res) {
        limit(authLimiter_, req, "auth");
        json body = parseBody(req);
        auto r = services()->auth->registerUser(requireString(body, "email", 300), requireString(body, "password", 200),
                                                requireString(body, "displayName", 100));
        res.set_header("Cache-Control", "no-store");
        sendJson(res, 201, {{"token", r.token}, {"user", AuthService::userJson(r.user)}});
    });

    post(server, "/api/v1/auth/login", [this](const httplib::Request &req, httplib::Response &res) {
        limit(authLimiter_, req, "auth");
        json body = parseBody(req);
        auto r = services()->auth->login(requireString(body, "email", 300), requireString(body, "password", 200));
        res.set_header("Cache-Control", "no-store");
        sendJson(res, 200, {{"token", r.token}, {"user", AuthService::userJson(r.user)}});
    });

    get(server, "/api/v1/me", [this](const httplib::Request &req, httplib::Response &res) {
        int64_t uid = requireUser(req);
        auto user = services()->auth->findUser(uid);
        if (!user) throw ApiError(401, "UNAUTHENTICATED", "Account not found. Please sign in again.");
        res.set_header("Cache-Control", "no-store");
        sendJson(res, 200, {{"user", AuthService::userJson(*user)}});
    });

    // ---------------------------------------------------------------- runs
    post(server, "/api/v1/runs", [this](const httplib::Request &req, httplib::Response &res) {
        int64_t uid = requireUser(req);
        sendJson(res, 201, services()->runs->create(uid, parseBody(req)));
    });

    post(server, "/api/v1/runs/batch", [this](const httplib::Request &req, httplib::Response &res) {
        int64_t uid = requireUser(req);
        sendJson(res, 201, services()->runs->importBatch(uid, parseBody(req)));
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
        sendJson(res, 200, services()->runs->list(uid, limitN, req.get_param_value("cursor")));
    });

    del(server, "/api/v1/runs/:id", [this](const httplib::Request &req, httplib::Response &res) {
        int64_t uid = requireUser(req);
        int64_t id;
        try {
            id = std::stoll(req.path_params.at("id"));
        } catch (const std::exception &) {
            throw ApiError(400, "BAD_REQUEST", "Run id must be a number.");
        }
        services()->runs->remove(uid, id);
        res.status = 204;
    });

    get(server, "/api/v1/stats", [this](const httplib::Request &req, httplib::Response &res) {
        int64_t uid = requireUser(req);
        res.set_header("Cache-Control", "no-store");
        sendJson(res, 200, services()->runs->stats(uid));
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
        sendJson(res, 200, services()->runs->leaderboard(n));
    });
}

}  // namespace zc::server
