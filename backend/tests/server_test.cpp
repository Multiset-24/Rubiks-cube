#include <gtest/gtest.h>

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
    explicit TestServer(Config cfg) : config_(std::move(cfg)), app_(config_, &shutdown_) {
        app_.attach(server_);
        server_.set_payload_max_length(config_.maxBodyBytes);
        app_.setServices(buildServices(config_, &shutdown_));
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

}  // namespace

TEST(Server, HealthReadinessAndInfo) {
    auto c = shared().client();
    EXPECT_EQ(c.Get("/healthz")->status, 200);
    auto ready = c.Get("/readyz");
    EXPECT_EQ(ready->status, 200);
    EXPECT_EQ(body(ready)["storage"], "memory");
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

TEST(Server, MetricsExposeRequestCounters) {
    auto c = shared().client();
    c.Get("/healthz");
    auto m = c.Get("/metrics");
    EXPECT_EQ(m->status, 200);
    EXPECT_NE(m->body.find("http_requests_total{method=\"GET\",route=\"/healthz\",status=\"200\"}"), std::string::npos);
    EXPECT_NE(m->body.find("http_request_duration_seconds_bucket"), std::string::npos);
}
