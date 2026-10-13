#include <algorithm>
#include <ctime>
#include <map>
#include <mutex>
#include <set>

#include "server/infra/logger.h"
#include "server/repo/repositories.h"

namespace zc::server {

std::string utcDay(int64_t epochMs) {
    std::time_t secs = static_cast<std::time_t>(epochMs / 1000);
    std::tm tm{};
    gmtime_r(&secs, &tm);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
    return buf;
}

namespace {

class MemoryUsers : public UserRepository {
public:
    User create(const std::string &email, const std::string &displayName, const std::string &hash) override {
        std::lock_guard<std::mutex> lock(mu_);
        for (const auto &[id, u]: users_) if (u.email == email) throw DuplicateError("email already registered");
        User u{++nextId_, email, displayName, hash, nowEpochMs()};
        users_[u.id] = u;
        return u;
    }
    std::optional<User> findByEmail(const std::string &email) override {
        std::lock_guard<std::mutex> lock(mu_);
        for (const auto &[id, u]: users_) if (u.email == email) return u;
        return std::nullopt;
    }
    std::optional<User> findById(int64_t id) override {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = users_.find(id);
        if (it == users_.end()) return std::nullopt;
        return it->second;
    }
    void updatePasswordHash(int64_t id, const std::string &hash) override {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = users_.find(id);
        if (it != users_.end()) it->second.passwordHash = hash;
    }
    std::optional<std::string> displayName(int64_t id) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = users_.find(id);
        if (it == users_.end()) return std::nullopt;
        return it->second.displayName;
    }

private:
    std::mutex mu_;
    std::map<int64_t, User> users_;
    int64_t nextId_ = 0;
};

class MemoryRuns : public RunRepository {
public:
    explicit MemoryRuns(std::shared_ptr<MemoryUsers> users) : users_(std::move(users)) {}

    Run create(const Run &in) override {
        std::lock_guard<std::mutex> lock(mu_);
        if (!in.clientId.empty()) {
            for (const auto &r: runs_) if (r.userId == in.userId && r.clientId == in.clientId) return r;
        }
        Run r = in;
        r.id = ++nextId_;
        if (r.createdAtMs == 0) r.createdAtMs = nowEpochMs();
        runs_.push_back(r);
        return r;
    }

    RunPage list(int64_t userId, int limit, const std::optional<RunCursor> &after) override {
        std::lock_guard<std::mutex> lock(mu_);
        std::vector<Run> mine;
        for (const auto &r: runs_) {
            if (r.userId != userId) continue;
            if (after && !(r.createdAtMs < after->createdAtMs ||
                           (r.createdAtMs == after->createdAtMs && r.id < after->id))) continue;
            mine.push_back(r);
        }
        std::sort(mine.begin(), mine.end(), [](const Run &a, const Run &b) {
            return a.createdAtMs != b.createdAtMs ? a.createdAtMs > b.createdAtMs : a.id > b.id;
        });
        RunPage page;
        for (const auto &r: mine) {
            if (static_cast<int>(page.runs.size()) == limit) {
                page.next = RunCursor{page.runs.back().createdAtMs, page.runs.back().id};
                break;
            }
            page.runs.push_back(r);
        }
        return page;
    }

    bool remove(int64_t userId, int64_t runId) override {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = std::find_if(runs_.begin(), runs_.end(),
                               [&](const Run &r) { return r.id == runId && r.userId == userId; });
        if (it == runs_.end()) return false;
        runs_.erase(it);
        return true;
    }

    int64_t count(int64_t userId) override {
        std::lock_guard<std::mutex> lock(mu_);
        return std::count_if(runs_.begin(), runs_.end(), [&](const Run &r) { return r.userId == userId; });
    }

    RunTotals totals(int64_t userId) override {
        std::lock_guard<std::mutex> lock(mu_);
        RunTotals t;
        std::map<std::string, KindTotals> byKind;
        for (const auto &r: runs_) {
            if (r.userId != userId) continue;
            auto &k = byKind[r.kind];
            k.kind = r.kind;
            ++k.total;
            k.completed += r.completed;
            t.quizCorrect += r.quizCorrect;
            t.quizTotal += r.quizTotal;
            if (r.completed && r.kind != "timer") t.movesFollowed += r.moveCount;
            if (r.kind == "timer" && r.completed && r.durationMs > 0 &&
                (!t.bestTimerMs || r.durationMs < *t.bestTimerMs)) t.bestTimerMs = r.durationMs;
        }
        for (auto &[_, k]: byKind) t.byKind.push_back(k);
        return t;
    }

    std::vector<int64_t> recentTimerTimes(int64_t userId, int n) override {
        std::lock_guard<std::mutex> lock(mu_);
        std::vector<const Run *> timers;
        for (const auto &r: runs_)
            if (r.userId == userId && r.kind == "timer" && r.completed) timers.push_back(&r);
        std::sort(timers.begin(), timers.end(), [](const Run *a, const Run *b) {
            return a->createdAtMs != b->createdAtMs ? a->createdAtMs > b->createdAtMs : a->id > b->id;
        });
        std::vector<int64_t> out;
        for (size_t i = 0; i < timers.size() && static_cast<int>(i) < n; ++i) out.push_back(timers[i]->durationMs);
        return out;
    }

    std::vector<std::string> activeDays(int64_t userId, int maxDays) override {
        std::lock_guard<std::mutex> lock(mu_);
        std::set<std::string, std::greater<>> days;
        for (const auto &r: runs_) if (r.userId == userId) days.insert(utcDay(r.createdAtMs));
        std::vector<std::string> out(days.begin(), days.end());
        if (static_cast<int>(out.size()) > maxDays) out.resize(static_cast<size_t>(maxDays));
        return out;
    }

    std::vector<LeaderboardEntry> leaderboard(int limit, int64_t minMs) override {
        std::map<int64_t, LeaderboardEntry> best;
        {
            std::lock_guard<std::mutex> lock(mu_);
            for (const auto &r: runs_) {
                if (r.kind != "timer" || !r.completed || r.durationMs < minMs || r.scramble.empty()) continue;
                auto &e = best[r.userId];
                if (e.solves == 0 || r.durationMs < e.bestMs) e.bestMs = r.durationMs;
                ++e.solves;
            }
        }
        std::vector<LeaderboardEntry> out;
        for (auto &[uid, e]: best) {
            e.displayName = users_->displayName(uid).value_or("?");
            out.push_back(e);
        }
        std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.bestMs < b.bestMs; });
        if (static_cast<int>(out.size()) > limit) out.resize(static_cast<size_t>(limit));
        return out;
    }

private:
    std::shared_ptr<MemoryUsers> users_;
    std::mutex mu_;
    std::vector<Run> runs_;
    int64_t nextId_ = 0;
};

}  // namespace

Storage makeMemoryStorage() {
    auto users = std::make_shared<MemoryUsers>();
    Storage s;
    s.kind = "memory";
    s.users = users;
    s.runs = std::make_shared<MemoryRuns>(users);
    s.healthy = [] { return true; };
    return s;
}

}  // namespace zc::server
