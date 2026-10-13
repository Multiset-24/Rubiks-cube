#include "server/services/run_service.h"

#include <algorithm>
#include <ctime>
#include <set>

#include "server/infra/crypto.h"
#include "server/infra/logger.h"
#include "server/services/api_error.h"

namespace zc::server {

using nlohmann::json;

namespace {

const std::set<std::string> kKinds = {"tutor", "fast", "optimal", "timer"};
constexpr int64_t kDayMs = 24LL * 3600 * 1000;

int64_t daysFromCivil(const std::string &ymd) {
    std::tm tm{};
    if (sscanf(ymd.c_str(), "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) != 3) return 0;
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    return static_cast<int64_t>(timegm(&tm)) / 86400;
}

template<typename T>
T intField(const json &j, const char *key, T lo, T hi, json &errors, T fallback = 0) {
    if (!j.contains(key) || j[key].is_null()) return fallback;
    if (!j[key].is_number_integer()) {
        errors[key] = "Must be an integer.";
        return fallback;
    }
    auto v = j[key].get<int64_t>();
    if (v < lo || v > hi) {
        errors[key] = "Must be between " + std::to_string(lo) + " and " + std::to_string(hi) + ".";
        return fallback;
    }
    return static_cast<T>(v);
}

std::string strField(const json &j, const char *key, size_t maxLen, json &errors) {
    if (!j.contains(key) || j[key].is_null()) return "";
    if (!j[key].is_string()) {
        errors[key] = "Must be a string.";
        return "";
    }
    std::string s = j[key].get<std::string>();
    if (s.size() > maxLen) errors[key] = "Too long (max " + std::to_string(maxLen) + ").";
    return s;
}

}  // namespace

json RunService::runJson(const Run &r) {
    return {{"id", r.id},
            {"kind", r.kind},
            {"method", r.method},
            {"facelets", r.facelets},
            {"scramble", r.scramble},
            {"moveCount", r.moveCount},
            {"durationMs", r.durationMs},
            {"completed", r.completed},
            {"quizCorrect", r.quizCorrect},
            {"quizTotal", r.quizTotal},
            {"stepsCompleted", r.stepsCompleted},
            {"totalSteps", r.totalSteps},
            {"clientId", r.clientId},
            {"createdAt", isoTimestampMs(r.createdAtMs)}};
}

std::string RunService::encodeCursor(const RunCursor &c) {
    return crypto::base64UrlEncode(std::to_string(c.createdAtMs) + ":" + std::to_string(c.id));
}

std::optional<RunCursor> RunService::decodeCursor(const std::string &s) {
    auto raw = crypto::base64UrlDecode(s);
    if (!raw) return std::nullopt;
    auto colon = raw->find(':');
    if (colon == std::string::npos) return std::nullopt;
    try {
        return RunCursor{std::stoll(raw->substr(0, colon)), std::stoll(raw->substr(colon + 1))};
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

Run RunService::parseRun(int64_t userId, const json &in) const {
    if (!in.is_object()) throw ApiError(400, "VALIDATION_FAILED", "Each run must be a JSON object.");
    json errors = json::object();
    Run r;
    r.userId = userId;
    r.kind = strField(in, "kind", 16, errors);
    if (!kKinds.count(r.kind)) errors["kind"] = "Must be one of tutor, fast, optimal, timer.";
    r.method = strField(in, "method", 32, errors);
    r.facelets = strField(in, "facelets", 54, errors);
    if (!r.facelets.empty() && r.facelets.size() != 54) errors["facelets"] = "Must be 54 characters.";
    r.scramble = strField(in, "scramble", 200, errors);
    r.clientId = strField(in, "clientId", 64, errors);
    r.moveCount = intField<int>(in, "moveCount", 0, 10000, errors);
    r.durationMs = intField<int64_t>(in, "durationMs", 0, kDayMs, errors);
    r.quizCorrect = intField<int>(in, "quizCorrect", 0, 1000, errors);
    r.quizTotal = intField<int>(in, "quizTotal", 0, 1000, errors);
    r.stepsCompleted = intField<int>(in, "stepsCompleted", 0, 1000, errors);
    r.totalSteps = intField<int>(in, "totalSteps", 0, 1000, errors);
    if (r.quizCorrect > r.quizTotal) errors["quizCorrect"] = "Cannot exceed quizTotal.";
    if (in.contains("completed")) {
        if (!in["completed"].is_boolean()) errors["completed"] = "Must be true or false.";
        else r.completed = in["completed"].get<bool>();
    }
    // Clients may back-date runs they recorded while signed out (guest mode).
    int64_t now = nowEpochMs();
    r.createdAtMs = intField<int64_t>(in, "createdAtMs", now - 400 * kDayMs, now + 5 * 60 * 1000, errors, 0);
    if (!errors.empty()) throw ApiError(400, "VALIDATION_FAILED", "Some run fields are invalid.", {{"fields", errors}});
    return r;
}

void RunService::checkQuota(int64_t userId, size_t adding) {
    if (runs_->count(userId) + static_cast<int64_t>(adding) > maxRunsPerUser_) {
        throw ApiError(409, "RUN_LIMIT_REACHED",
                       "This account has reached its limit of " + std::to_string(maxRunsPerUser_) +
                       " saved runs. Delete some to save more.");
    }
}

json RunService::create(int64_t userId, const json &input) {
    Run r = parseRun(userId, input);
    checkQuota(userId, 1);
    return runJson(runs_->create(r));
}

json RunService::importBatch(int64_t userId, const json &input) {
    if (!input.is_object() || !input.contains("runs") || !input["runs"].is_array()) {
        throw ApiError(400, "VALIDATION_FAILED", "Expected {\"runs\": [...]}.");
    }
    const auto &arr = input["runs"];
    if (arr.size() > 200) throw ApiError(413, "TOO_MANY_RUNS", "Import at most 200 runs at a time.");
    std::vector<Run> parsed;
    for (const auto &item: arr) parsed.push_back(parseRun(userId, item));  // validate all before writing any
    checkQuota(userId, parsed.size());
    json out = json::array();
    for (const auto &r: parsed) out.push_back(runJson(runs_->create(r)));
    return {{"runs", out}, {"imported", out.size()}};
}

json RunService::list(int64_t userId, int limit, const std::string &cursor) {
    limit = std::clamp(limit, 1, 100);
    std::optional<RunCursor> after;
    if (!cursor.empty()) {
        after = decodeCursor(cursor);
        if (!after) throw ApiError(400, "BAD_CURSOR", "Invalid pagination cursor.");
    }
    RunPage page = runs_->list(userId, limit, after);
    json runs = json::array();
    for (const auto &r: page.runs) runs.push_back(runJson(r));
    return {{"runs", runs}, {"nextCursor", page.next ? json(encodeCursor(*page.next)) : json(nullptr)}};
}

void RunService::remove(int64_t userId, int64_t runId) {
    if (!runs_->remove(userId, runId)) throw ApiError(404, "NOT_FOUND", "Run not found.");
}

std::optional<int64_t> RunService::averageOf(const std::vector<int64_t> &times, size_t n) {
    if (times.size() < n || n < 3) return std::nullopt;
    std::vector<int64_t> t(times.begin(), times.begin() + static_cast<long>(n));
    std::sort(t.begin(), t.end());
    int64_t sum = 0;
    for (size_t i = 1; i + 1 < t.size(); ++i) sum += t[i];
    return sum / static_cast<int64_t>(n - 2);
}

std::pair<int, int> RunService::streaks(const std::vector<std::string> &days, const std::string &today) {
    if (days.empty()) return {0, 0};
    std::vector<int64_t> d;
    for (const auto &s: days) d.push_back(daysFromCivil(s));  // newest first
    int longest = 1, run = 1;
    for (size_t i = 1; i < d.size(); ++i) {
        run = d[i - 1] - d[i] == 1 ? run + 1 : 1;
        longest = std::max(longest, run);
    }
    int64_t t = daysFromCivil(today);
    int current = 0;
    if (t - d[0] <= 1) {  // active today or yesterday keeps the streak alive
        current = 1;
        for (size_t i = 1; i < d.size() && d[i - 1] - d[i] == 1; ++i) ++current;
    }
    return {current, longest};
}

json RunService::stats(int64_t userId) {
    RunTotals t = runs_->totals(userId);
    auto times = runs_->recentTimerTimes(userId, 12);
    auto days = runs_->activeDays(userId, 400);
    auto [current, longest] = streaks(days, utcDay(nowEpochMs()));

    json byKind = json::object();
    int64_t total = 0, completed = 0;
    for (const auto &k: t.byKind) {
        byKind[k.kind] = {{"total", k.total}, {"completed", k.completed}};
        total += k.total;
        completed += k.completed;
    }
    auto opt = [](const std::optional<int64_t> &v) { return v ? json(*v) : json(nullptr); };
    json last30 = json::array();
    for (const auto &d: days) {
        if (static_cast<int>(last30.size()) == 30) break;
        last30.push_back(d);
    }
    return {{"totalRuns", total},
            {"completedRuns", completed},
            {"byKind", byKind},
            {"movesFollowed", t.movesFollowed},
            {"quiz", {{"correct", t.quizCorrect}, {"total", t.quizTotal},
                      {"accuracy", t.quizTotal ? json(static_cast<double>(t.quizCorrect) / t.quizTotal) : json(nullptr)}}},
            {"timer", {{"bestMs", opt(t.bestTimerMs)}, {"ao5Ms", opt(averageOf(times, 5))},
                       {"ao12Ms", opt(averageOf(times, 12))}, {"recentMs", times}}},
            {"streak", {{"current", current}, {"longest", longest}}},
            {"activeDays", last30}};
}

json RunService::leaderboard(int limit) {
    limit = std::clamp(limit, 1, 50);
    json entries;
    bool refresh = false;
    {
        std::lock_guard<std::mutex> lock(boardMu_);
        bool fresh = !boardEntries_.is_null() && std::chrono::steady_clock::now() - boardAt_ < std::chrono::seconds(30);
        // One caller refreshes; the others keep serving the previous board.
        if (!fresh && !boardRefreshing_) {
            boardRefreshing_ = true;
            refresh = true;
        }
        entries = boardEntries_;
    }
    if (refresh || entries.is_null()) {
        json fetched = json::array();
        try {
            int rank = 0;
            for (const auto &e: runs_->leaderboard(50, kLeaderboardMinMs)) {
                fetched.push_back({{"rank", ++rank}, {"displayName", e.displayName}, {"bestMs", e.bestMs},
                                   {"solves", e.solves}});
            }
        } catch (...) {
            if (refresh) {
                std::lock_guard<std::mutex> lock(boardMu_);
                boardRefreshing_ = false;
            }
            if (entries.is_null()) throw;
            fetched = entries;  // serve the stale board rather than fail
            refresh = false;
        }
        if (refresh) {
            std::lock_guard<std::mutex> lock(boardMu_);
            boardEntries_ = fetched;
            boardAt_ = std::chrono::steady_clock::now();
            boardRefreshing_ = false;
        }
        entries = fetched;
    }
    json top = json::array();
    for (size_t i = 0; i < entries.size() && static_cast<int>(i) < limit; ++i) top.push_back(entries[i]);
    return {{"entries", top}, {"generatedAt", isoTimestampMs(nowEpochMs())}, {"selfReported", true}};
}

}  // namespace zc::server
