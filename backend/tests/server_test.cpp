#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <filesystem>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "Pattern_Database/CornerDBMaker.h"
#include "core/facelet.h"
#include "server/api/app.h"
#include "server/bootstrap.h"

using namespace zc;
using namespace zc::server;
using nlohmann::json;

namespace {

// Small (depth-5) corner database, enough for the optimal solver on short scrambles.
std::string testDataDir() {
    static std::string dir = [] {
        auto d = std::filesystem::temp_directory_path() / "zencube_server_test";
        std::filesystem::create_directories(d);
        if (!std::filesystem::exists(d / "corner.db")) {
            CornerDBMaker maker((d / "corner.db").string(), 0xFF);
            maker.bfsAndStore(5);
        }
        return d.string();
    }();
    return dir;
}

class TestServer {
public:
    // withAccounts=false simulates a database that hasn't connected (yet).
    explicit TestServer(Config cfg, bool withAccounts = true) : config_(std::move(cfg)), app_(config_, &shutdown_) {
        app_.attach(server_);
        server_.set_payload_max_length(config_.maxBodyBytes);
        app_.setSolver(buildSolveService(config_, &shutdown_));
        if (withAccounts) app_.setAccounts(connectAccounts(config_, &shutdown_));
        port_ = server_.bind_to_any_port("127.0.0.1");
        thread_ = std::thread([this] { server_.listen_after_bind(); });
        server_.wait_until_ready();
    }
    ~TestServer() {
        server_.stop();
        thread_.join();
    }
    httplib::Client client() const {
        httplib::Client c("127.0.0.1", port_);
        c.set_read_timeout(30, 0);
        return c;
    }
    int port() const { return port_; }

private:
    Config config_;
    std::atomic<bool> shutdown_{false};
    httplib::Server server_;
    App app_;
    int port_ = 0;
    std::thread thread_;
};

Config baseConfig() {
    Config c;
    c.dataDir = testDataDir();
    c.jwtSecret = "test-secret";
    c.passwordHashIterations = 1000;
    c.rateLimitRps = 1000;
    c.rateLimitBurst = 1000;
    c.solveRateRps = 1000;
    c.solveRateBurst = 1000;
    c.authRatePerMinute = 1000;
    c.runWritesPerMinute = 1000;
    c.corsOrigins = {"https://zen.example", "https://*--zen.netlify.app"};
    c.maxBodyBytes = 64 * 1024;
    return c;
}

TestServer &shared() {
    static TestServer s(baseConfig());
    return s;
}

json body(const httplib::Result &r) { return json::parse(r->body); }

httplib::Headers bearer(const std::string &token) { return {{"Authorization", "Bearer " + token}}; }

std::string registerUser(httplib::Client &c, const std::string &name = "Ada") {
    static int n = 0;
    json req = {{"email", "user" + std::to_string(++n) + "-" + name + "@example.com"},
                {"password", "correct horse battery"}, {"displayName", name}};
    auto r = c.Post("/api/v1/auth/register", req.dump(), "application/json");
    EXPECT_EQ(r->status, 201) << r->body;
    return body(r)["token"];
}

const char *kOneMove = "UUFUUFUUFRRRRRRRRRFFDFFDFFDDDBDDBDDBLLLLLLLLLUBBUBBUBB";  // after R

// Sends raw bytes (in the given pieces, with small pauses between them) on one
// connection and returns everything the server writes back within ~1 s.
std::string rawExchange(int port, const std::vector<std::string> &pieces) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0) {
        close(fd);
        return "";
    }
    for (const auto &p: pieces) {
        send(fd, p.data(), p.size(), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }
    timeval tv{1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    std::string out;
    char buf[4096];
    for (ssize_t n; (n = recv(fd, buf, sizeof buf, 0)) > 0;) out.append(buf, static_cast<size_t>(n));
    close(fd);
    return out;
}

int countOf(const std::string &haystack, const std::string &needle) {
    int n = 0;
    for (size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1)) ++n;
    return n;
}

}  // namespace

TEST(Server, HealthReadinessAndInfo) {
    auto c = shared().client();
    EXPECT_EQ(c.Get("/healthz")->status, 200);
    auto ready = c.Get("/readyz");
    EXPECT_EQ(ready->status, 200);
    EXPECT_EQ(body(ready)["storage"]["kind"], "memory");
    EXPECT_TRUE(body(ready)["storage"]["available"].get<bool>());
    EXPECT_FALSE(body(ready).contains("error"));
    auto info = body(c.Get("/api/v1/info"));
    EXPECT_EQ(info["solvers"].size(), 3u);
    EXPECT_TRUE(info["solvers"][2]["available"].get<bool>());
}

TEST(Server, ValidateReportsIssues) {
    auto c = shared().client();
    auto ok = body(c.Post("/api/v1/validate", json{{"cube", kOneMove}}.dump(), "application/json"));
    EXPECT_TRUE(ok["valid"].get<bool>());
    std::string bad = kOneMove;
    std::swap(bad[5], bad[10]);  // flip an edge in place
    auto r = body(c.Post("/api/v1/validate", json{{"cube", bad}}.dump(), "application/json"));
    EXPECT_FALSE(r["valid"].get<bool>());
    EXPECT_FALSE(r["issues"].empty());
}

TEST(Server, BeginnerSolveIsCorrectAndCached) {
    auto c = shared().client();
    std::string cube = FaceletCube().after(parseMoves("R U2 F' L D B2 R' U F2 L'")).str();
    auto r1 = c.Post("/api/v1/solve", json{{"cube", cube}, {"method", "beginner"}}.dump(), "application/json");
    ASSERT_EQ(r1->status, 200) << r1->body;
    EXPECT_EQ(r1->get_header_value("X-Cache"), "MISS");
    json b = body(r1);
    EXPECT_GE(b["stages"].size(), 8u);
    FaceletCube f = FaceletCube::fromString(cube);
    f.apply(parseMoves(b["moves"].get<std::string>()));
    EXPECT_TRUE(f.isSolved());

    auto r2 = c.Post("/api/v1/solve", json{{"cube", cube}, {"method", "beginner"}}.dump(), "application/json");
    EXPECT_EQ(r2->get_header_value("X-Cache"), "HIT");
    EXPECT_TRUE(body(r2)["cached"].get<bool>());
}

TEST(Server, KociembaAndOptimalSolves) {
    auto c = shared().client();
    auto scr = body(c.Get("/api/v1/scramble"));
    std::string cube = scr["facelets"];
    EXPECT_TRUE(FaceletCube().after(parseMoves(scr["scramble"].get<std::string>())) == FaceletCube::fromString(cube));

    auto fast = body(c.Post("/api/v1/solve", json{{"cube", cube}, {"method", "kociemba"}}.dump(), "application/json"));
    EXPECT_LE(fast["moveCount"].get<int>(), 23);
    EXPECT_TRUE(FaceletCube::fromString(cube).after(parseMoves(fast["moves"].get<std::string>())).isSolved());

    auto opt = body(c.Post("/api/v1/solve", json{{"cube", kOneMove}, {"method", "optimal"}}.dump(), "application/json"));
    EXPECT_EQ(opt["moves"], "R'");
    EXPECT_TRUE(opt["optimal"].get<bool>());
}

TEST(Server, OptimalFallsBackToFastForDeepScrambles) {
    Config cfg = baseConfig();
    cfg.optimalTimeout = std::chrono::milliseconds(50);
    TestServer s(cfg);
    auto c = s.client();
    std::string cube = body(c.Get("/api/v1/scramble"))["facelets"];
    auto r = c.Post("/api/v1/solve", json{{"cube", cube}, {"method", "optimal"}}.dump(), "application/json");
    ASSERT_EQ(r->status, 200) << r->body;
    json b = body(r);
    EXPECT_EQ(b["fallbackFrom"], "optimal");
    EXPECT_EQ(b["method"], "kociemba");

    // The fallback is cached, so a repeat doesn't spend the budget again...
    auto again = c.Post("/api/v1/solve", json{{"cube", cube}, {"method", "optimal"}}.dump(), "application/json");
    EXPECT_EQ(again->get_header_value("X-Cache"), "HIT");
    // ...but HTTP caches must not keep it forever: it isn't an exact answer.
    auto viaGet = c.Get("/api/v1/solve?method=optimal&cube=" + cube);
    EXPECT_EQ(viaGet->get_header_value("Cache-Control"), "no-store");
}

TEST(Server, FastSolverFindsSlowCubesWithinBudget) {
    // About 1 cube in 200 takes over a second to solve in 21 moves but
    // milliseconds in 23; the solver must not spend its whole budget on 21.
    Config cfg = baseConfig();
    cfg.solveTimeout = std::chrono::milliseconds(300);
    TestServer s(cfg);
    auto c = s.client();
    const std::string slow = "BRFUULDFUFDDFRRULURULDFDLUBDBLBDULRFRRBFLBDLBRBUFBLRDF";
    auto r = c.Post("/api/v1/solve", json{{"cube", slow}, {"method", "kociemba"}}.dump(), "application/json");
    ASSERT_EQ(r->status, 200) << r->body;
    EXPECT_TRUE(FaceletCube::fromString(slow).after(parseMoves(body(r)["moves"].get<std::string>())).isSolved());
}

TEST(Server, ErrorEnvelope) {
    auto c = shared().client();
    auto invalid = c.Post("/api/v1/solve", json{{"cube", std::string(54, 'U')}}.dump(), "application/json");
    EXPECT_EQ(invalid->status, 422);
    EXPECT_EQ(body(invalid)["error"]["code"], "INVALID_CUBE");
    EXPECT_FALSE(body(invalid)["error"]["details"]["issues"].empty());
    EXPECT_FALSE(body(invalid)["error"]["requestId"].get<std::string>().empty());

    auto method = c.Post("/api/v1/solve", json{{"cube", kOneMove}, {"method", "magic"}}.dump(), "application/json");
    EXPECT_EQ(method->status, 400);
    EXPECT_EQ(body(method)["error"]["code"], "UNKNOWN_METHOD");

    auto badJson = c.Post("/api/v1/solve", "{nope", "application/json");
    EXPECT_EQ(body(badJson)["error"]["code"], "BAD_JSON");

    auto missing = c.Get("/api/v1/nothing-here");
    EXPECT_EQ(missing->status, 404);
    EXPECT_EQ(body(missing)["error"]["code"], "NOT_FOUND");

    auto huge = c.Post("/api/v1/solve", std::string(100 * 1024, 'x'), "application/json");
    EXPECT_EQ(huge->status, 413);
}

TEST(Server, CorsAndRequestIds) {
    auto c = shared().client();
    httplib::Headers h = {{"Origin", "https://zen.example"}, {"Access-Control-Request-Method", "POST"}};
    auto pre = c.Options("/api/v1/solve", h);
    EXPECT_EQ(pre->status, 204);
    EXPECT_EQ(pre->get_header_value("Access-Control-Allow-Origin"), "https://zen.example");
    EXPECT_NE(pre->get_header_value("Access-Control-Allow-Headers").find("Authorization"), std::string::npos);

    auto preview = c.Get("/healthz", {{"Origin", "https://deploy-preview-7--zen.netlify.app"}});
    EXPECT_EQ(preview->get_header_value("Access-Control-Allow-Origin"), "https://deploy-preview-7--zen.netlify.app");
    auto lookalike = c.Get("/healthz", {{"Origin", "https://evil.com/x--zen.netlify.app"}});
    EXPECT_FALSE(lookalike->has_header("Access-Control-Allow-Origin"));

    auto evil = c.Get("/healthz", {{"Origin", "https://evil.example"}});
    EXPECT_FALSE(evil->has_header("Access-Control-Allow-Origin"));

    auto echoed = c.Get("/healthz", {{"X-Request-Id", "trace-123"}});
    EXPECT_EQ(echoed->get_header_value("X-Request-Id"), "trace-123");
    EXPECT_EQ(echoed->get_header_value("X-Content-Type-Options"), "nosniff");
}

TEST(Server, GetSolveIsCacheable) {
    auto c = shared().client();
    auto r = c.Get(std::string("/api/v1/solve?method=kociemba&cube=") + kOneMove);
    ASSERT_EQ(r->status, 200);
    EXPECT_NE(r->get_header_value("Cache-Control").find("immutable"), std::string::npos);
}

TEST(Server, AuthFlow) {
    auto c = shared().client();
    json reg = {{"email", "Grace@Example.com"}, {"password", "hopper-1906"}, {"displayName", "Grace"}};
    auto r = c.Post("/api/v1/auth/register", reg.dump(), "application/json");
    ASSERT_EQ(r->status, 201) << r->body;
    std::string token = body(r)["token"];
    EXPECT_EQ(body(r)["user"]["email"], "grace@example.com");  // normalised

    EXPECT_EQ(c.Post("/api/v1/auth/register", reg.dump(), "application/json")->status, 409);

    auto weak = c.Post("/api/v1/auth/register",
                       json{{"email", "bad"}, {"password", "short"}, {"displayName", "x"}}.dump(), "application/json");
    EXPECT_EQ(weak->status, 400);
    EXPECT_EQ(body(weak)["error"]["details"]["fields"].size(), 3u);

    auto wrong = c.Post("/api/v1/auth/login", json{{"email", "grace@example.com"}, {"password", "nope-nope"}}.dump(),
                        "application/json");
    EXPECT_EQ(wrong->status, 401);
    auto unknown = c.Post("/api/v1/auth/login", json{{"email", "nobody@example.com"}, {"password", "whatever1"}}.dump(),
                          "application/json");
    EXPECT_EQ(unknown->status, 401);
    EXPECT_EQ(body(unknown)["error"]["code"], body(wrong)["error"]["code"]);  // no user enumeration

    auto login = c.Post("/api/v1/auth/login", json{{"email", " GRACE@example.com "}, {"password", "hopper-1906"}}.dump(),
                        "application/json");
    ASSERT_EQ(login->status, 200);

    auto me = c.Get("/api/v1/me", bearer(body(login)["token"]));
    EXPECT_EQ(me->status, 200);
    EXPECT_EQ(body(me)["user"]["displayName"], "Grace");
    EXPECT_EQ(me->get_header_value("Cache-Control"), "no-store");

    EXPECT_EQ(c.Get("/api/v1/me")->status, 401);
    std::string tampered = token;
    tampered[tampered.size() - 2] = tampered[tampered.size() - 2] == 'A' ? 'B' : 'A';
    EXPECT_EQ(c.Get("/api/v1/me", bearer(tampered))->status, 401);
}

TEST(Server, RunsHistoryStatsAndLeaderboard) {
    auto c = shared().client();
    std::string token = registerUser(c, "Runner");
    auto h = bearer(token);

    json run = {{"kind", "tutor"}, {"method", "beginner"}, {"facelets", kOneMove}, {"moveCount", 140},
                {"durationMs", 600000}, {"completed", true}, {"quizCorrect", 7}, {"quizTotal", 9},
                {"stepsCompleted", 30}, {"totalSteps", 30}, {"clientId", "tutor-1"}};
    auto created = c.Post("/api/v1/runs", h, run.dump(), "application/json");
    ASSERT_EQ(created->status, 201) << created->body;
    auto again = c.Post("/api/v1/runs", h, run.dump(), "application/json");
    EXPECT_EQ(body(again)["id"], body(created)["id"]);  // idempotent retry

    for (int ms: {21000, 19000, 25000, 18000, 30000}) {
        json t = {{"kind", "timer"}, {"durationMs", ms}, {"completed", true}, {"scramble", "R U F"}};
        ASSERT_EQ(c.Post("/api/v1/runs", h, t.dump(), "application/json")->status, 201);
    }
    auto bad = c.Post("/api/v1/runs", h, json{{"kind", "speedrun"}, {"durationMs", -5}}.dump(), "application/json");
    EXPECT_EQ(bad->status, 400);
    EXPECT_TRUE(body(bad)["error"]["details"]["fields"].contains("kind"));
    EXPECT_EQ(c.Post("/api/v1/runs", run.dump(), "application/json")->status, 401);

    auto page1 = body(c.Get("/api/v1/runs?limit=4", h));
    EXPECT_EQ(page1["runs"].size(), 4u);
    ASSERT_TRUE(page1["nextCursor"].is_string());
    auto page2 = body(c.Get("/api/v1/runs?limit=4&cursor=" + page1["nextCursor"].get<std::string>(), h));
    EXPECT_EQ(page2["runs"].size(), 2u);
    EXPECT_TRUE(page2["nextCursor"].is_null());

    auto stats = body(c.Get("/api/v1/stats", h));
    EXPECT_EQ(stats["totalRuns"], 6);
    EXPECT_EQ(stats["timer"]["bestMs"], 18000);
    EXPECT_EQ(stats["timer"]["ao5Ms"], (19000 + 21000 + 25000) / 3);
    EXPECT_EQ(stats["streak"]["current"], 1);
    EXPECT_NEAR(stats["quiz"]["accuracy"].get<double>(), 7.0 / 9.0, 1e-9);

    auto board = body(c.Get("/api/v1/leaderboard"));
    bool found = false;
    for (const auto &e: board["entries"]) found |= e["displayName"] == "Runner" && e["bestMs"] == 18000;
    EXPECT_TRUE(found);

    int64_t id = body(created)["id"];
    EXPECT_EQ(c.Delete("/api/v1/runs/" + std::to_string(id), h)->status, 204);
    EXPECT_EQ(c.Delete("/api/v1/runs/" + std::to_string(id), h)->status, 404);

    json batch = {{"runs", json::array({{{"kind", "fast"}, {"clientId", "g1"}, {"completed", true}},
                                        {{"kind", "fast"}, {"clientId", "g2"}, {"completed", false}}})}};
    auto imported = c.Post("/api/v1/runs/batch", h, batch.dump(), "application/json");
    ASSERT_EQ(imported->status, 201) << imported->body;
    EXPECT_EQ(body(imported)["imported"], 2);
}

TEST(Server, RateLimitingReturns429WithRetryAfter) {
    Config cfg = baseConfig();
    cfg.rateLimitRps = 0.5;
    cfg.rateLimitBurst = 3;
    TestServer s(cfg);
    auto c = s.client();
    int limited = 0;
    std::string retryAfter;
    for (int i = 0; i < 6; ++i) {
        auto r = c.Get("/api/v1/info");
        if (r->status == 429) {
            ++limited;
            retryAfter = r->get_header_value("Retry-After");
        }
    }
    EXPECT_EQ(limited, 3);
    EXPECT_FALSE(retryAfter.empty());
    EXPECT_EQ(c.Get("/healthz")->status, 200);  // probes are never limited
}

TEST(Server, RateLimitedBodyIsNotParsedAsAnotherRequest) {
    // A 429 sent before the body is read would leave the body on the
    // connection, to be parsed as the next request.
    Config cfg = baseConfig();
    cfg.rateLimitRps = 0.01;
    cfg.rateLimitBurst = 1;
    TestServer s(cfg);
    std::string smuggled = "GET /api/v1/info HTTP/1.1\r\nHost: x\r\nX-Request-Id: SMUGGLED\r\n\r\n";
    std::string first = "GET /api/v1/info HTTP/1.1\r\nHost: x\r\n\r\n";
    std::string headers = "POST /api/v1/validate HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\n"
                          "Content-Length: " + std::to_string(smuggled.size()) + "\r\n\r\n";
    std::string out = rawExchange(s.port(), {first, headers, smuggled});
    EXPECT_EQ(countOf(out, "HTTP/1.1 429"), 1) << out;
    EXPECT_EQ(out.find("SMUGGLED"), std::string::npos) << out;
}

TEST(Server, ClientIpComesFromTheRightOfForwardedFor) {
    Config cfg = baseConfig();
    cfg.rateLimitRps = 0.01;
    cfg.rateLimitBurst = 3;
    cfg.trustedProxyHops = 1;
    TestServer s(cfg);
    auto c = s.client();
    int limited = 0;
    for (int i = 0; i < 5; ++i) {
        // A client changing the left part must still share one bucket.
        httplib::Headers h = {{"X-Forwarded-For", "10.0.0." + std::to_string(i) + ", 198.51.100.7"}};
        if (c.Get("/api/v1/info", h)->status == 429) ++limited;
    }
    EXPECT_EQ(limited, 2);
    EXPECT_EQ(c.Get("/api/v1/info", {{"X-Forwarded-For", "10.0.0.1, 198.51.100.8"}})->status, 200);
}

TEST(Server, SolvingWorksWhileTheDatabaseIsDown) {
    TestServer s(baseConfig(), /*withAccounts=*/false);
    auto c = s.client();
    auto ready = c.Get("/readyz");
    EXPECT_EQ(ready->status, 200);  // readiness doesn't depend on the database
    EXPECT_FALSE(body(ready)["storage"]["available"].get<bool>());
    EXPECT_EQ(c.Post("/api/v1/solve", json{{"cube", kOneMove}}.dump(), "application/json")->status, 200);
    auto login = c.Post("/api/v1/auth/login", json{{"email", "a@b.co"}, {"password", "whatever1"}}.dump(),
                        "application/json");
    EXPECT_EQ(login->status, 503);
    EXPECT_EQ(body(login)["error"]["code"], "STORAGE_UNAVAILABLE");
}

TEST(Server, RunLimitPerAccount) {
    Config cfg = baseConfig();
    cfg.maxRunsPerUser = 3;
    TestServer s(cfg);
    auto c = s.client();
    auto h = bearer(registerUser(c, "Quota"));
    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(c.Post("/api/v1/runs", h, json{{"kind", "timer"}, {"durationMs", 9000}}.dump(), "application/json")->status, 201);
    }
    auto over = c.Post("/api/v1/runs", h, json{{"kind", "timer"}, {"durationMs", 9000}}.dump(), "application/json");
    EXPECT_EQ(over->status, 409);
    EXPECT_EQ(body(over)["error"]["code"], "RUN_LIMIT_REACHED");
}

TEST(Server, MalformedRequestsDontCreateMetricSeries) {
    auto &srv = shared();
    rawExchange(srv.port(), {"ZZWEIRDMETHOD / HTTP/1.1\r\nHost: x\r\n\r\n"});
    auto m = srv.client().Get("/metrics");
    EXPECT_EQ(m->body.find("ZZWEIRDMETHOD"), std::string::npos);
}

TEST(Server, MetricsExposeRequestCounters) {
    auto c = shared().client();
    c.Get("/healthz");
    auto m = c.Get("/metrics");
    EXPECT_EQ(m->status, 200);
    EXPECT_NE(m->body.find("http_requests_total{method=\"GET\",route=\"/healthz\",status=\"200\"}"), std::string::npos);
    EXPECT_NE(m->body.find("http_request_duration_seconds_bucket"), std::string::npos);
}
