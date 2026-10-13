#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include "server/infra/bulkhead.h"
#include "server/infra/crypto.h"
#include "server/infra/jwt.h"
#include "server/infra/lru_cache.h"
#include "server/infra/metrics.h"
#include "server/infra/rate_limiter.h"
#include "server/infra/singleflight.h"
#include "server/services/run_service.h"

using namespace zc::server;

TEST(LruCache, EvictsLeastRecentlyUsed) {
    LruCache<std::string, int> c(2);
    c.put("a", 1);
    c.put("b", 2);
    ASSERT_EQ(c.get("a").value(), 1);  // a is now most recent
    c.put("c", 3);                     // evicts b
    EXPECT_FALSE(c.get("b").has_value());
    EXPECT_EQ(c.get("a").value(), 1);
    EXPECT_EQ(c.get("c").value(), 3);
    EXPECT_EQ(c.size(), 2u);
}

TEST(RateLimiter, AllowsBurstThenRefills) {
    RateLimiter rl(2.0, 3.0);
    auto t = RateLimiter::Clock::now();
    EXPECT_TRUE(rl.acquire("ip", t).allowed);
    EXPECT_TRUE(rl.acquire("ip", t).allowed);
    EXPECT_TRUE(rl.acquire("ip", t).allowed);
    auto denied = rl.acquire("ip", t);
    EXPECT_FALSE(denied.allowed);
    EXPECT_NEAR(denied.retryAfterSeconds, 0.5, 1e-6);
    EXPECT_TRUE(rl.acquire("other", t).allowed);  // independent keys
    EXPECT_TRUE(rl.acquire("ip", t + std::chrono::milliseconds(600)).allowed);
}

TEST(Bulkhead, RejectsWhenFull) {
    Bulkhead b("test", 1);
    auto p1 = b.tryAcquire(std::chrono::milliseconds(0));
    ASSERT_TRUE(p1);
    EXPECT_FALSE(b.tryAcquire(std::chrono::milliseconds(10)));
    { auto released = std::move(p1); }
    EXPECT_TRUE(b.tryAcquire(std::chrono::milliseconds(0)));
}

TEST(SingleFlight, CoalescesConcurrentCalls) {
    SingleFlight<int> sf;
    std::atomic<int> calls{0};
    std::atomic<int> shared{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&] {
            auto r = sf.run("k", [&] {
                ++calls;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                return 42;
            });
            EXPECT_EQ(r.value, 42);
            if (r.shared) ++shared;
        });
    }
    for (auto &t: threads) t.join();
    EXPECT_EQ(calls.load(), 1);
    EXPECT_EQ(shared.load(), 7);
}

TEST(SingleFlight, PropagatesExceptionsToWaiters) {
    SingleFlight<int> sf;
    EXPECT_THROW(sf.run("k", []() -> int { throw std::runtime_error("boom"); }), std::runtime_error);
    EXPECT_EQ(sf.run("k", [] { return 1; }).value, 1);  // key released after failure
}

TEST(Crypto, Base64UrlRoundTrip) {
    for (std::string s: {std::string(""), std::string("f"), std::string("fo"), std::string("foo"),
                         std::string("foob"), std::string("fooba"), std::string("foobar"), std::string("\0\xff\x10", 3)}) {
        EXPECT_EQ(crypto::base64UrlDecode(crypto::base64UrlEncode(s)).value(), s);
    }
    EXPECT_EQ(crypto::base64UrlEncode("foobar"), "Zm9vYmFy");
    EXPECT_FALSE(crypto::base64UrlDecode("a$b").has_value());
}

TEST(Crypto, Sha256KnownVector) {
    EXPECT_EQ(crypto::sha256Hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Crypto, PasswordHashing) {
    std::string h = crypto::hashPassword("correct horse", 1000);
    EXPECT_EQ(h.rfind("pbkdf2_sha256$1000$", 0), 0u);
    EXPECT_TRUE(crypto::verifyPassword("correct horse", h));
    EXPECT_FALSE(crypto::verifyPassword("correct horse!", h));
    EXPECT_NE(h, crypto::hashPassword("correct horse", 1000));  // salted
    EXPECT_TRUE(crypto::needsRehash(h, 2000));
    EXPECT_FALSE(crypto::needsRehash(h, 1000));
    EXPECT_FALSE(crypto::verifyPassword("x", "garbage"));
}

TEST(Jwt, SignAndVerify) {
    Jwt jwt("secret", "zen-cube");
    std::string token = jwt.sign({{"sub", "42"}}, 60, 1000);
    auto claims = jwt.verify(token, 1030);
    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ((*claims)["sub"], "42");

    std::string why;
    EXPECT_FALSE(jwt.verify(token, 1061, &why).has_value());
    EXPECT_EQ(why, "token expired");
    EXPECT_FALSE(Jwt("other", "zen-cube").verify(token, 1030, &why).has_value());
    EXPECT_EQ(why, "bad signature");
    EXPECT_FALSE(Jwt("secret", "someone-else").verify(token, 1030).has_value());
}

TEST(Jwt, RejectsTamperedPayloadAndAlgNone) {
    Jwt jwt("secret", "zen-cube");
    std::string token = jwt.sign({{"sub", "1"}}, 60, 1000);
    size_t a = token.find('.'), b = token.rfind('.');
    std::string forgedPayload = crypto::base64UrlEncode(R"({"sub":"2","iss":"zen-cube","exp":99999})");
    EXPECT_FALSE(jwt.verify(token.substr(0, a + 1) + forgedPayload + token.substr(b), 1000).has_value());
    std::string none = crypto::base64UrlEncode(R"({"alg":"none"})") + "." + forgedPayload + ".";
    EXPECT_FALSE(jwt.verify(none, 1000).has_value());
}

TEST(Metrics, RendersPrometheusText) {
    Metrics m;
    m.describe("x_total", "counter", "Things.");
    m.inc("x_total", {{"route", "/a"}});
    m.inc("x_total", {{"route", "/a"}});
    m.observe("lat_seconds", 0.02, {{"route", "/a"}});
    std::string out = m.render();
    EXPECT_NE(out.find("# TYPE x_total counter"), std::string::npos);
    EXPECT_NE(out.find("x_total{route=\"/a\"} 2"), std::string::npos);
    EXPECT_NE(out.find("lat_seconds_bucket{route=\"/a\",le=\"0.025\"} 1"), std::string::npos) << out;
    EXPECT_NE(out.find("lat_seconds_count{route=\"/a\"} 1"), std::string::npos);
}

TEST(RunStats, WcaAverages) {
    EXPECT_FALSE(RunService::averageOf({1000, 2000}, 5).has_value());
    // first five: 9000 1000 2000 3000 4000 -> drop 1000 and 9000 -> mean 3000
    EXPECT_EQ(RunService::averageOf({9000, 1000, 2000, 3000, 4000, 99}, 5).value(), 3000);
}

TEST(RunStats, Streaks) {
    EXPECT_EQ(RunService::streaks({}, "2026-01-10"), std::make_pair(0, 0));
    auto s = RunService::streaks({"2026-01-10", "2026-01-09", "2026-01-08", "2026-01-01", "2025-12-31"}, "2026-01-10");
    EXPECT_EQ(s.first, 3);
    EXPECT_EQ(s.second, 3);
    EXPECT_EQ(RunService::streaks({"2026-01-09", "2026-01-08"}, "2026-01-10").first, 2);  // yesterday counts
    EXPECT_EQ(RunService::streaks({"2026-01-07"}, "2026-01-10").first, 0);
}

TEST(RunStats, CursorRoundTrip) {
    auto c = RunService::decodeCursor(RunService::encodeCursor({1700000000123, 77}));
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->createdAtMs, 1700000000123);
    EXPECT_EQ(c->id, 77);
    EXPECT_FALSE(RunService::decodeCursor("!!!").has_value());
}
