// The engine headers put `using namespace std` in scope, so they are only
// included in this file.
#include "core/optimal.h"

#include <chrono>
#include <fstream>

#include "Model/RubiksCubeBitboard.h"
#include "Pattern_Database/CornerPatternDatabase.h"
#include "Solvers/IdaStarSolver.h"

namespace zc {

namespace {

// Engine face order is UP LEFT FRONT RIGHT BACK DOWN; ours is U R F D L B.
constexpr int kEngineFaceToOurs[6] = {0, 4, 2, 1, 5, 3};
// Colour letters the engine's solved cube uses for faces U R F D L B.
constexpr char kOurFaceToEngineColour[6] = {'W', 'B', 'R', 'Y', 'G', 'O'};

int faceOfLetter(char c) {
    for (int f = 0; f < kNumFaces; ++f) if (kFaceChars[f] == c) return f;
    return -1;
}

RubiksCubeBitboard toEngineCube(const FaceletCube &fc) {
    char grid[6][3][3];
    for (int ef = 0; ef < 6; ++ef) {
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                char letter = fc[faceletIndex(kEngineFaceToOurs[ef], r, c)];
                grid[ef][r][c] = kOurFaceToEngineColour[faceOfLetter(letter)];
            }
        }
    }
    return RubiksCubeBitboard(grid);
}

Move fromEngineMove(RubiksCube::MOVE m) {
    // Engine order: L L' L2 R R' R2 U U' U2 D D' D2 F F' F2 B B' B2
    static const Axis faces[6] = {Axis::L, Axis::R, Axis::U, Axis::D, Axis::F, Axis::B};
    static const uint8_t turns[3] = {1, 3, 2};
    int i = static_cast<int>(m);
    return Move{faces[i / 3], turns[i % 3]};
}

// Engine corner positions UFR UFL UBL UBR DFR DFL DBR DBL, each listed as the
// facelets of RubiksCube::getCornerColorString (U/D sticker first).
constexpr int kEngineCornerFacelets[8][3] = {
        {8, 20, 9}, {6, 18, 38}, {0, 47, 36}, {2, 45, 11},
        {29, 26, 15}, {27, 24, 44}, {35, 51, 17}, {33, 53, 42},
};
// Matching Kociemba position for each engine position.
constexpr int kEngineToKociembaCorner[8] = {URF, UFL, ULB, UBR, DFR, DLF, DRB, DBL};

// Engine corner id: bit 2 = has the D colour, bit 1 = has B, bit 0 = has L.
uint8_t engineCornerId(int kociembaCubie) {
    uint8_t id = 0;
    for (int f: kCornerColors[static_cast<size_t>(kociembaCubie)]) {
        if (f == 3) id |= 4;
        if (f == 5) id |= 2;
        if (f == 4) id |= 1;
    }
    return id;
}

// Engine orientation label at engine position i, given Kociemba twist o of the
// piece in the matching position: where the U/D sticker lands in the engine's
// facelet order.
uint8_t engineOrientation(int i, int o) {
    int facelet = kCornerFacelets[static_cast<size_t>(kEngineToKociembaCorner[i])][static_cast<size_t>(o)];
    for (uint8_t k = 0; k < 3; ++k) if (kEngineCornerFacelets[i][k] == facelet) return k;
    return 0;
}

uint32_t enginePermRank(const CubieCube &c) {
    uint8_t perm[8];
    for (int i = 0; i < 8; ++i) perm[i] = engineCornerId(c.cp[static_cast<size_t>(kEngineToKociembaCorner[i])]);
    return static_cast<uint32_t>(permutationRank(perm, 8));
}

uint32_t engineOrientNumber(const CubieCube &c) {
    uint32_t n = 0;
    for (int i = 0; i < 7; ++i) n = n * 3 + engineOrientation(i, c.co[static_cast<size_t>(kEngineToKociembaCorner[i])]);
    return n;
}

}  // namespace

uint32_t engineCornerIndex(const CubieCube &c) { return enginePermRank(c) * 2187 + engineOrientNumber(c); }

OptimalSolver::OptimalSolver(std::shared_ptr<const CornerPatternDatabase> db) : db_(std::move(db)) {}

std::shared_ptr<const CornerPatternDatabase> OptimalSolver::loadDatabase(const std::string &path) {
    auto db = std::make_shared<CornerPatternDatabase>();
    try {
        if (!db->fromFile(path)) return nullptr;
    } catch (const std::exception &) {
        return nullptr;
    }
    return db;
}

OptimalResult OptimalSolver::solve(const FaceletCube &cube, const SearchBudget &budget) const {
    OptimalResult r;
    if (cube.isSolved()) {
        r.found = true;
        return r;
    }
    IDAstarSolver<RubiksCubeBitboard, HashBitboard> solver(toEngineCube(cube), db_);
    IDAstarSolver<RubiksCubeBitboard, HashBitboard>::Budget b;
    b.maxNodes = budget.maxNodes;
    b.deadline = budget.deadline;
    if (budget.cancel) {
        const std::atomic<bool> *flag = budget.cancel;
        b.cancelled = [flag] { return flag->load(std::memory_order_relaxed); };
    }
    solver.setBudget(b);
    auto moves = solver.solve();
    r.nodes = solver.getStats().nodesExpanded;
    r.timedOut = solver.getStats().exhausted;
    r.found = !r.timedOut && solver.rubiksCube.isSolved();
    for (auto m: moves) r.moves.push_back(fromEngineMove(m));
    return r;
}

CornerDbBuildStats buildCornerDatabase(const KociembaTables &t, const std::string &path) {
    auto start = std::chrono::steady_clock::now();
    constexpr uint32_t kTwist = KociembaTables::kTwist;
    constexpr uint64_t kStates = static_cast<uint64_t>(KociembaTables::kCornerPerm) * kTwist;

    // 4 bits per state, 0xF = not reached yet.
    std::vector<uint8_t> dist(kStates / 2 + 1, 0xFF);
    auto get = [&](uint64_t i) -> uint8_t { return (dist[i >> 1] >> ((i & 1) * 4)) & 0xF; };
    auto set = [&](uint64_t i, uint8_t v) {
        uint8_t &b = dist[i >> 1];
        b = static_cast<uint8_t>((b & ~(0xF << ((i & 1) * 4))) | (v << ((i & 1) * 4)));
    };

    set(0, 0);  // solved: corner perm 0, twist 0
    uint64_t reached = 1;
    int depth = 0;
    while (reached < kStates) {
        uint64_t added = 0;
        for (uint64_t i = 0; i < kStates; ++i) {
            if (get(i) != depth) continue;
            uint32_t cp = static_cast<uint32_t>(i / kTwist), tw = static_cast<uint32_t>(i % kTwist);
            for (int m = 0; m < 18; ++m) {
                uint64_t n = static_cast<uint64_t>(t.cornerPermMove[static_cast<size_t>(cp) * 18 + m]) * kTwist +
                             t.twistMove[static_cast<size_t>(tw) * 18 + m];
                if (get(n) == 0xF) {
                    set(n, static_cast<uint8_t>(depth + 1));
                    ++added;
                }
            }
        }
        if (added == 0) break;
        reached += added;
        ++depth;
    }

    // Re-index into the engine's layout. The engine index factors into a part
    // that depends only on the permutation and one that depends only on twist.
    std::vector<uint32_t> permPart(KociembaTables::kCornerPerm), orientPart(kTwist);
    for (int cp = 0; cp < KociembaTables::kCornerPerm; ++cp) {
        CubieCube c;
        c.setCornerPerm(cp);
        permPart[static_cast<size_t>(cp)] = enginePermRank(c) * 2187;
    }
    for (uint32_t tw = 0; tw < kTwist; ++tw) {
        CubieCube c;
        c.setTwist(static_cast<int>(tw));
        orientPart[tw] = engineOrientNumber(c);
    }
    CornerPatternDatabase db;
    for (uint64_t i = 0; i < kStates; ++i) {
        uint32_t idx = permPart[i / kTwist] + orientPart[i % kTwist];
        db.setNumMoves(idx, get(i));
    }
    db.toFile(path);

    CornerDbBuildStats stats;
    stats.states = reached;
    stats.maxDepth = depth;
    stats.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return stats;
}

}  // namespace zc
