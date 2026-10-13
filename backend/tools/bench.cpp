// Reproducible solver benchmarks:  zencube_bench [--data-dir DIR] [--samples N]
// Prints solution lengths and timings for each solver on uniformly random cubes
// (and on short scrambles for the optimal solver).

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "core/beginner.h"
#include "core/kociemba.h"
#include "core/optimal.h"

using namespace zc;
using Clock = std::chrono::steady_clock;

namespace {

struct Summary {
    double mean = 0, p50 = 0, p95 = 0, max = 0;
};

Summary summarize(std::vector<double> v) {
    Summary s;
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    s.mean = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
    s.p50 = v[v.size() / 2];
    s.p95 = v[std::min(v.size() - 1, v.size() * 95 / 100)];
    s.max = v.back();
    return s;
}

double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

void row(const char *name, const Summary &moves, const Summary &time) {
    std::printf("| %-28s | %6.1f | %4.0f | %4.0f | %8.2f | %8.2f | %8.2f |\n", name, moves.mean, moves.p95, moves.max,
                time.p50, time.p95, time.max);
}

}  // namespace

int main(int argc, char **argv) {
    std::string dataDir = "./data";
    int samples = 500;
    for (int i = 1; i + 1 < argc; ++i) {
        std::string a = argv[i];
        if (a == "--data-dir") dataDir = argv[++i];
        else if (a == "--samples") samples = std::stoi(argv[++i]);
    }
    std::mt19937 rng(2024);

    auto t0 = Clock::now();
    auto tables = KociembaTables::build();
    std::printf("Kociemba tables: %.0f ms, %.1f MB\n\n", ms(t0, Clock::now()), tables->memoryBytes() / 1e6);

    std::printf("| %-28s | %6s | %4s | %4s | %8s | %8s | %8s |\n", "solver (random cubes)", "moves", "p95", "max",
                "p50 ms", "p95 ms", "max ms");
    std::printf("|------------------------------|--------|------|------|----------|----------|----------|\n");

    std::vector<CubieCube> cubes;
    for (int i = 0; i < samples; ++i) cubes.push_back(randomCubieCube(rng));

    std::vector<double> kMoves, kTime, bMoves, bTime;
    for (const auto &c: cubes) {
        auto a = Clock::now();
        auto r = solveKociemba(*tables, c, 21, SearchBudget::withTimeout(std::chrono::seconds(5)));
        if (!r.found) r = solveKociemba(*tables, c, 23, SearchBudget::withTimeout(std::chrono::seconds(5)));
        kTime.push_back(ms(a, Clock::now()));
        kMoves.push_back(static_cast<double>(r.moves.size()));

        a = Clock::now();
        auto t = solveBeginner(c.toFacelets(), defaultColorNames(), SearchBudget{});
        bTime.push_back(ms(a, Clock::now()));
        bMoves.push_back(countFaceTurns(t.moves));
    }
    row("Kociemba two-phase", summarize(kMoves), summarize(kTime));
    row("Beginner tutor (face turns)", summarize(bMoves), summarize(bTime));

    auto db = OptimalSolver::loadDatabase(dataDir + "/corner.db");
    if (!db) {
        std::printf("\n(no corner database in %s: run zencube_server --build-tables)\n", dataDir.c_str());
        return 0;
    }
    OptimalSolver optimal(db);
    std::printf("\n| %-28s | %6s | %10s | %8s | %8s |\n", "optimal IDA* (scramble)", "moves", "nodes p50", "p50 ms", "max ms");
    std::printf("|------------------------------|--------|------------|----------|----------|\n");
    for (int depth: {5, 6, 7, 8, 9}) {
        std::vector<double> m, t, nodes;
        int n = depth >= 9 ? 5 : 20;
        for (int i = 0; i < n; ++i) {
            FaceletCube f;
            int last = -1;
            for (int k = 0; k < depth;) {
                Move mv = Move::fromFaceMoveIndex(static_cast<int>(rng() % 18));
                if (mv.face() == last) continue;
                last = mv.face();
                f.apply(mv);
                ++k;
            }
            auto a = Clock::now();
            auto r = optimal.solve(f, SearchBudget::withTimeout(std::chrono::seconds(60)));
            t.push_back(ms(a, Clock::now()));
            m.push_back(static_cast<double>(r.moves.size()));
            nodes.push_back(static_cast<double>(r.nodes));
        }
        char label[64];
        std::snprintf(label, sizeof label, "%d-move scrambles (n=%d)", depth, n);
        auto sm = summarize(m), st = summarize(t), sn = summarize(nodes);
        std::printf("| %-28s | %6.1f | %10.0f | %8.1f | %8.1f |\n", label, sm.mean, sn.p50, st.p50, st.max);
    }
    return 0;
}
