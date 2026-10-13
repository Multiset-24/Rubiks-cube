// Contract tests: the same behaviour is required from every storage adapter.
// The Postgres variant runs when ZENCUBE_TEST_DATABASE_URL points at a scratch
// database (CI provides one as a service container).
#include <gtest/gtest.h>

#include <cstdlib>
#include <thread>

#include "server/infra/crypto.h"
#include "server/infra/logger.h"
#include "server/repo/repositories.h"

using namespace zc::server;

namespace {

struct StorageFactory {
    std::string name;
    std::function<Storage()> make;
};

std::string uniqueEmail() { return "u" + crypto::randomHex(6) + "@example.com"; }

class RepoContract : public ::testing::TestWithParam<StorageFactory> {
protected:
    void SetUp() override {
        if (GetParam().name == "postgres" && !std::getenv("ZENCUBE_TEST_DATABASE_URL")) {
            GTEST_SKIP() << "ZENCUBE_TEST_DATABASE_URL not set";
        }
        storage = GetParam().make();
    }
    Storage storage;
};

}  // namespace

TEST_P(RepoContract, UsersAreUniqueByEmail) {
    std::string email = uniqueEmail();
    User u = storage.users->create(email, "Ada", "hash");
    EXPECT_GT(u.id, 0);
    EXPECT_GT(u.createdAtMs, 0);
    EXPECT_THROW(storage.users->create(email, "Other", "hash2"), DuplicateError);
    EXPECT_EQ(storage.users->findByEmail(email)->id, u.id);
    EXPECT_EQ(storage.users->findById(u.id)->displayName, "Ada");
    EXPECT_FALSE(storage.users->findByEmail("nobody-" + email).has_value());
    storage.users->updatePasswordHash(u.id, "hash3");
    EXPECT_EQ(storage.users->findById(u.id)->passwordHash, "hash3");
}

TEST_P(RepoContract, RunsCreateIsIdempotentOnClientId) {
    User u = storage.users->create(uniqueEmail(), "Ada", "h");
    zc::server::Run r;
    r.userId = u.id;
    r.kind = "tutor";
    r.clientId = "abc-123";
    r.moveCount = 120;
    zc::server::Run first = storage.runs->create(r);
    zc::server::Run again = storage.runs->create(r);
    EXPECT_EQ(first.id, again.id);
    r.clientId = "";
    zc::server::Run noKey1 = storage.runs->create(r);
    zc::server::Run noKey2 = storage.runs->create(r);
    EXPECT_NE(noKey1.id, noKey2.id);
}

TEST_P(RepoContract, KeysetPaginationIsStableAndOrdered) {
    User u = storage.users->create(uniqueEmail(), "Ada", "h");
    int64_t base = nowEpochMs() - 100000;
    for (int i = 0; i < 25; ++i) {
        zc::server::Run r;
        r.userId = u.id;
        r.kind = "timer";
        r.completed = true;
        r.durationMs = 10000 + i;
        r.createdAtMs = base + (i / 2) * 1000;  // pairs share a timestamp: id breaks ties
        storage.runs->create(r);
    }
    std::vector<int64_t> seen;
    std::optional<RunCursor> cursor;
    int pages = 0;
    do {
        RunPage p = storage.runs->list(u.id, 10, cursor);
        for (const auto &r: p.runs) seen.push_back(r.id);
        cursor = p.next;
        ++pages;
    } while (cursor && pages < 10);
    EXPECT_EQ(pages, 3);
    ASSERT_EQ(seen.size(), 25u);
    std::set<int64_t> unique(seen.begin(), seen.end());
    EXPECT_EQ(unique.size(), 25u);
    auto first = storage.runs->list(u.id, 25, std::nullopt).runs;
    for (size_t i = 1; i < first.size(); ++i) {
        EXPECT_TRUE(first[i - 1].createdAtMs > first[i].createdAtMs ||
                    (first[i - 1].createdAtMs == first[i].createdAtMs && first[i - 1].id > first[i].id));
    }
}

TEST_P(RepoContract, TotalsTimesDaysAndDeletion) {
    User u = storage.users->create(uniqueEmail(), "Ada", "h");
    User other = storage.users->create(uniqueEmail(), "Bob", "h");
    auto add = [&](const User &who, const std::string &kind, bool done, int64_t ms, int quizC, int quizT, int moves) {
        zc::server::Run r;
        r.userId = who.id;
        r.kind = kind;
        r.completed = done;
        r.durationMs = ms;
        r.quizCorrect = quizC;
        r.quizTotal = quizT;
        r.moveCount = moves;
        if (kind == "timer") r.scramble = "R U F' L2 D";
        return storage.runs->create(r);
    };
    add(u, "tutor", true, 300000, 8, 10, 150);
    add(u, "tutor", false, 1000, 1, 2, 150);
    add(u, "timer", true, 45000, 0, 0, 0);
    zc::server::Run fast = add(u, "timer", true, 30000, 0, 0, 0);
    add(u, "timer", false, 5000, 0, 0, 0);
    add(other, "timer", true, 20000, 0, 0, 0);

    RunTotals t = storage.runs->totals(u.id);
    int64_t total = 0;
    for (const auto &k: t.byKind) total += k.total;
    EXPECT_EQ(total, 5);
    EXPECT_EQ(t.quizCorrect, 9);
    EXPECT_EQ(t.quizTotal, 12);
    EXPECT_EQ(t.movesFollowed, 150);
    ASSERT_TRUE(t.bestTimerMs.has_value());
    EXPECT_EQ(*t.bestTimerMs, 30000);

    auto times = storage.runs->recentTimerTimes(u.id, 12);
    ASSERT_EQ(times.size(), 2u);
    EXPECT_EQ(times[0], 30000);  // newest first

    auto days = storage.runs->activeDays(u.id, 10);
    ASSERT_EQ(days.size(), 1u);
    EXPECT_EQ(days[0], utcDay(nowEpochMs()));

    auto board = storage.runs->leaderboard(100, 3000);
    bool sawBob = false, sawAda = false;
    for (const auto &e: board) {
        if (e.displayName == "Bob" && e.bestMs == 20000) sawBob = true;
        if (e.displayName == "Ada" && e.bestMs == 30000 && e.solves == 2) sawAda = true;
    }
    EXPECT_TRUE(sawBob);
    EXPECT_TRUE(sawAda);

    EXPECT_EQ(storage.runs->count(u.id), 5);

    EXPECT_FALSE(storage.runs->remove(other.id, fast.id));  // not yours
    EXPECT_TRUE(storage.runs->remove(u.id, fast.id));
    EXPECT_FALSE(storage.runs->remove(u.id, fast.id));
}

INSTANTIATE_TEST_SUITE_P(
        Storage, RepoContract,
        ::testing::Values(StorageFactory{"memory", [] { return makeMemoryStorage(); }},
                          StorageFactory{"postgres",
                                         [] {
                                             const char *url = std::getenv("ZENCUBE_TEST_DATABASE_URL");
                                             return makePostgresStorage(url ? url : "", 2);
                                         }}),
        [](const auto &info) { return info.param.name; });
