#include "server/services/solve_service.h"

#include <chrono>
#include <random>

#include "core/beginner.h"
#include "server/infra/logger.h"
#include "server/infra/metrics.h"
#include "server/services/api_error.h"

namespace zc::server {

using nlohmann::json;

std::string describeMove(const Move &m) {
    static const char *faceName[] = {"top", "right", "front", "bottom", "left", "back"};
    static const char *seenFrom[] = {"", "", "", " (as seen from below)", " (as seen from the left)", " (as seen from behind)"};
    // Where the front (or top, for F/B/z) stickers travel on a clockwise turn.
    static const char *cwEffect[] = {"front goes left", "front goes up", "top goes right", "front goes right",
                                     "front goes down", "top goes left", "front goes up", "front goes left",
                                     "top goes right"};
    static const char *ccwEffect[] = {"front goes right", "front goes down", "top goes left", "front goes left",
                                      "front goes up", "top goes right", "front goes down", "front goes right",
                                      "top goes left"};
    static const char *rotationLike[] = {"R", "U", "F"};
    int a = static_cast<int>(m.axis);
    std::string what = m.isRotation() ? std::string("Rotate the whole cube like ") + rotationLike[a - 6]
                                      : std::string("Turn the ") + faceName[a] + " face";
    if (m.turns == 2) return what + " twice" + (m.isRotation() ? "" : std::string(seenFrom[a])) + ".";
    std::string dir = m.turns == 1 ? "clockwise" : "counter-clockwise";
    if (m.isRotation()) return what + (m.turns == 1 ? "" : "'") + ": " + (m.turns == 1 ? cwEffect[a] : ccwEffect[a]) + ".";
    return what + " " + dir + seenFrom[a] + ": " + (m.turns == 1 ? cwEffect[a] : ccwEffect[a]) + ".";
}

namespace {

json movesJson(const std::vector<Move> &moves) {
    json arr = json::array();
    for (const auto &m: moves) arr.push_back(m.str());
    return arr;
}

json tutorToJson(const TutorSolution &sol) {
    json stages = json::array();
    for (const auto &st: sol.stages) {
        json algs = json::array();
        for (const auto &a: st.algorithms) algs.push_back({{"name", a.name}, {"notation", a.notation}});
        json steps = json::array();
        for (const auto &s: st.steps) {
            json step = {{"title", s.title}, {"observe", s.observe}, {"instruction", s.instruction},
                         {"tip", s.tip}, {"setup", movesJson(s.setup)}, {"moves", movesJson(s.moves)},
                         {"focus", s.focus}};
            if (s.algorithm) {
                step["algorithm"] = {{"name", s.algorithm->name}, {"notation", s.algorithm->notation},
                                     {"repeat", s.algorithm->repeat}};
            }
            if (s.quiz) {
                step["quiz"] = {{"question", s.quiz->question}, {"options", s.quiz->options},
                                {"answer", s.quiz->answer}, {"explanation", s.quiz->explanation}};
            }
            steps.push_back(std::move(step));
        }
        json stage = {{"id", st.id}, {"name", st.name}, {"goal", st.goal}, {"recognize", st.recognize},
                      {"algorithms", algs}, {"steps", steps}};
        if (!st.skippedNote.empty()) stage["skippedNote"] = st.skippedNote;
        stages.push_back(std::move(stage));
    }
    return stages;
}

// Fast/optimal solutions become one stage with a step per move, so clients render
// every method with the same player.
json movesAsStages(const std::vector<Move> &moves, const std::string &stageName, const std::string &goal) {
    json steps = json::array();
    for (size_t i = 0; i < moves.size(); ++i) {
        steps.push_back({{"title", "Move " + std::to_string(i + 1) + " of " + std::to_string(moves.size()) + ": " +
                                   moves[i].str()},
                         {"observe", ""},
                         {"instruction", describeMove(moves[i])},
                         {"tip", ""},
                         {"setup", json::array()},
                         {"moves", json::array({moves[i].str()})},
                         {"focus", json::array()}});
    }
    return json::array({json{{"id", "solution"}, {"name", stageName}, {"goal", goal}, {"recognize", ""},
                             {"algorithms", json::array()}, {"steps", steps}}});
}

json solutionBody(const std::string &method, const std::string &solver, const FaceletCube &fc,
                  const std::vector<Move> &moves, json stages) {
    json moveList = movesJson(moves);
    return {{"method", method},
            {"solver", solver},
            {"facelets", fc.str()},
            {"moves", formatMoves(moves)},
            {"moveList", moveList},
            {"moveCount", countFaceTurns(moves)},
            {"stages", std::move(stages)}};
}

class BeginnerStrategy : public SolverStrategy {
public:
    std::string id() const override { return "beginner"; }
    std::string displayName() const override { return "Beginner tutor"; }
    std::string description() const override {
        return "Layer-by-layer method with explanations, pattern recognition and quizzes (~100-150 moves).";
    }
    json solve(const FaceletCube &fc, const CubieCube &, const ColorNames &names, const SearchBudget &budget) const override {
        TutorSolution sol;
        try {
            sol = solveBeginner(fc, names, budget);
        } catch (const std::runtime_error &e) {
            if (std::string(e.what()).find("ran out of time") != std::string::npos) throw SolverBudgetExceeded(e.what());
            throw;
        }
        json body = solutionBody(id(), "layer-by-layer", fc, sol.moves, tutorToJson(sol));
        body["optimal"] = false;
        return body;
    }
};

class KociembaStrategy : public SolverStrategy {
public:
    explicit KociembaStrategy(std::shared_ptr<const KociembaTables> t) : tables_(std::move(t)) {}
    std::string id() const override { return "kociemba"; }
    std::string displayName() const override { return "Fast solve"; }
    std::string description() const override {
        return "Kociemba's two-phase algorithm: about 20 moves for any cube in milliseconds.";
    }
    json solve(const FaceletCube &fc, const CubieCube &cubie, const ColorNames &, const SearchBudget &budget) const override {
        // A 21-move solution always exists, but for about 1 cube in 200 finding
        // it takes over a second, while a 23-move one takes milliseconds. So the
        // 21 and 23 attempts each get a quarter of the remaining time; the last
        // attempt gets whatever is left.
        for (int maxLen: {21, 23, 26}) {
            SearchBudget attempt = budget;
            if (maxLen < 26) {
                auto now = SearchBudget::Clock::now();
                if (budget.deadline > now) attempt.deadline = now + (budget.deadline - now) / 4;
            }
            auto r = solveKociemba(*tables_, cubie, maxLen, attempt);
            if (r.found) {
                json body = solutionBody(id(), "kociemba-two-phase", fc, r.moves,
                                         movesAsStages(r.moves, "Fast solution",
                                                       "Follow each move. Hold the cube as you scanned it: "
                                                       "the front face towards you, the top face up."));
                body["optimal"] = false;
                body["stats"] = {{"nodes", r.nodes}, {"phase1Length", r.phase1Length}};
                return body;
            }
            if (budget.expired(0)) break;  // the whole budget is gone, or shutting down
        }
        throw SolverBudgetExceeded("two-phase search exceeded its time budget");
    }

private:
    std::shared_ptr<const KociembaTables> tables_;
};

class OptimalStrategy : public SolverStrategy {
public:
    explicit OptimalStrategy(std::shared_ptr<const OptimalSolver> s) : solver_(std::move(s)) {}
    std::string id() const override { return "optimal"; }
    std::string displayName() const override { return "Optimal (IDA*)"; }
    std::string description() const override {
        return "Korf's IDA* with a corner pattern database (the original engine). Shortest possible solution; "
               "practical for lightly scrambled cubes (up to ~9 moves).";
    }
    bool heavy() const override { return true; }
    json solve(const FaceletCube &fc, const CubieCube &, const ColorNames &, const SearchBudget &budget) const override {
        auto r = solver_->solve(fc, budget);
        if (!r.found) throw SolverBudgetExceeded("optimal search exceeded its time budget");
        json body = solutionBody(id(), "ida-star-corner-pdb", fc, r.moves,
                                 movesAsStages(r.moves, "Optimal solution",
                                               "This is the shortest possible solution. Hold the cube as you scanned it."));
        body["optimal"] = true;
        body["stats"] = {{"nodes", r.nodes}};
        return body;
    }

private:
    std::shared_ptr<const OptimalSolver> solver_;
};

std::string cacheKey(const SolveRequest &req, const FaceletCube &fc) {
    std::string key = req.method + ":" + fc.str();
    // Tutor text depends on colour names; other methods do not.
    if (req.method == "beginner") for (const auto &n: req.names) key += ":" + n;
    return key;
}

}  // namespace

SolveService::SolveService(const Config &config, std::shared_ptr<const KociembaTables> tables,
                           std::shared_ptr<const OptimalSolver> optimal, const std::atomic<bool> *shutdown)
        : config_(config), tables_(tables), cache_(config.solveCacheSize),
          solveBulkhead_("solve", config.maxConcurrentSolves), heavyBulkhead_("optimal", config.maxConcurrentOptimal),
          shutdown_(shutdown) {
    strategies_["beginner"] = std::make_unique<BeginnerStrategy>();
    strategies_["kociemba"] = std::make_unique<KociembaStrategy>(tables);
    if (optimal) strategies_["optimal"] = std::make_unique<OptimalStrategy>(optimal);
}

json SolveService::describeSolvers() const {
    json out = json::array();
    for (const char *id: {"beginner", "kociemba", "optimal"}) {
        auto it = strategies_.find(id);
        if (it == strategies_.end()) {
            out.push_back({{"id", id}, {"available", false}});
            continue;
        }
        out.push_back({{"id", id}, {"name", it->second->displayName()}, {"description", it->second->description()},
                       {"available", true}});
    }
    return out;
}

json SolveService::validate(const std::string &cube, const ColorNames &names) const {
    auto v = validateCube(cube, names);
    json issues = json::array();
    for (const auto &i: v.issues) {
        issues.push_back({{"code", i.code}, {"message", i.message}, {"hint", i.hint}, {"facelets", i.facelets}});
    }
    json body = {{"valid", v.valid}, {"issues", issues}};
    if (v.facelets) body["facelets"] = v.facelets->str();
    return body;
}

json SolveService::scramble() const {
    // WCA-style random-state scramble: pick a uniformly random cube, solve it,
    // and invert the solution.
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    CubieCube c = randomCubieCube(rng);
    auto r = solveKociemba(*tables_, c, 23, SearchBudget::withTimeout(config_.solveTimeout));
    if (!r.found) throw ApiError(503, "BUSY", "Could not generate a scramble right now.", nullptr, 1);
    auto scramble = invertMoves(r.moves);
    return {{"scramble", formatMoves(scramble)}, {"moveList", movesJson(scramble)},
            {"facelets", FaceletCube().after(scramble).str()}};
}

json SolveService::runStrategy(const SolverStrategy &s, const FaceletCube &fc, const CubieCube &cubie,
                               const ColorNames &names) {
    // Bulkheads: a general pool for all solves, plus a separate small pool for
    // heavy searches so they can never occupy every solve slot.
    Bulkhead::Permit heavy;
    if (s.heavy()) {
        heavy = heavyBulkhead_.tryAcquire(config_.queueTimeout);
        if (!heavy) {
            Metrics::instance().inc("bulkhead_rejections_total", {{"pool", heavyBulkhead_.name()}});
            throw ApiError(503, "BUSY", "The optimal solver is busy. Try again shortly or use the fast solver.",
                           nullptr, 2);
        }
    }
    Bulkhead::Permit permit = solveBulkhead_.tryAcquire(config_.queueTimeout);
    if (!permit) {
        Metrics::instance().inc("bulkhead_rejections_total", {{"pool", solveBulkhead_.name()}});
        throw ApiError(503, "BUSY", "The solver is busy. Please try again in a moment.", nullptr, 2);
    }

    SearchBudget budget = SearchBudget::withTimeout(s.heavy() ? config_.optimalTimeout : config_.solveTimeout);
    budget.cancel = shutdown_;
    auto start = std::chrono::steady_clock::now();
    try {
        json body = s.solve(fc, cubie, names, budget);
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        Metrics::instance().observe("solver_duration_seconds", secs, {{"solver", s.id()}});
        body["elapsedMs"] = secs * 1000;
        return body;
    } catch (const SolverBudgetExceeded &) {
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        Metrics::instance().observe("solver_duration_seconds", secs, {{"solver", s.id()}});
        Metrics::instance().inc("solver_timeouts_total", {{"solver", s.id()}});
        throw;
    }
}

SolveResponse SolveService::solve(const SolveRequest &req) {
    auto it = strategies_.find(req.method);
    if (it == strategies_.end()) {
        if (req.method == "optimal") {
            throw ApiError(503, "SOLVER_UNAVAILABLE", "The optimal solver's pattern database is not loaded.");
        }
        throw ApiError(400, "UNKNOWN_METHOD", "Unknown method '" + req.method + "'. Use beginner, kociemba or optimal.");
    }
    auto v = validateCube(req.cube, req.names);
    if (!v.valid) {
        json issues = json::array();
        for (const auto &i: v.issues) {
            issues.push_back({{"code", i.code}, {"message", i.message}, {"hint", i.hint}, {"facelets", i.facelets}});
        }
        throw ApiError(422, "INVALID_CUBE", v.issues.front().message, {{"issues", issues}});
    }

    const FaceletCube &fc = *v.facelets;
    std::string key = cacheKey(req, fc);
    if (auto hit = cache_.get(key)) {
        Metrics::instance().inc("solve_cache_requests_total", {{"result", "hit"}});
        json body = *hit;
        body["cached"] = true;
        return {body, true, false};
    }
    Metrics::instance().inc("solve_cache_requests_total", {{"result", "miss"}});

    const SolverStrategy &strategy = *it->second;
    auto outcome = flights_.run(key, [&]() -> json {
        try {
            json body = runStrategy(strategy, fc, *v.cubie, req.names);
            cache_.put(key, body);
            return body;
        } catch (const SolverBudgetExceeded &) {
            if (strategy.id() == "optimal") {
                // Optimal search time grows exponentially with depth; for deep
                // scrambles return a near-optimal answer instead of an error.
                Metrics::instance().inc("solver_fallbacks_total", {{"from", "optimal"}, {"to", "kociemba"}});
                json body;
                try {
                    body = runStrategy(*strategies_.at("kociemba"), fc, *v.cubie, req.names);
                } catch (const SolverBudgetExceeded &) {
                    throw ApiError(504, "SOLVER_TIMEOUT", "The solver ran out of time. Please try again.");
                }
                body["fallbackFrom"] = "optimal";
                body["fallbackReason"] =
                        "This cube is too far from solved for an optimal search within the time limit, "
                        "so here is a near-optimal solution instead.";
                // Cached so a repeat request doesn't spend the full budget again.
                cache_.put(key, body);
                return body;
            }
            throw ApiError(504, "SOLVER_TIMEOUT", "The solver ran out of time. Please try again.");
        }
    });
    if (outcome.shared) Metrics::instance().inc("solve_coalesced_total");
    json body = outcome.value;
    body["cached"] = false;
    return {body, false, outcome.shared};
}

}  // namespace zc::server
