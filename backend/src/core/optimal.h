#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/budget.h"
#include "core/cubie.h"
#include "core/facelet.h"
#include "core/kociemba.h"
#include "core/moves.h"

// From the original engine (engine/Pattern_Database).
class CornerPatternDatabase;

namespace zc {

struct OptimalResult {
    bool found = false;
    bool timedOut = false;
    std::vector<Move> moves;
    uint64_t nodes = 0;
};

// Adapter around the original engine's IDA* solver (engine/Solvers/IdaStarSolver.h)
// running on its bitboard cube model with Korf's corner pattern database as the
// heuristic. Finds a shortest solution, which is only practical for lightly
// scrambled cubes (roughly <= 9 moves): callers give it a budget and fall back
// to Kociemba when it runs out.
class OptimalSolver {
public:
    explicit OptimalSolver(std::shared_ptr<const CornerPatternDatabase> db);

    // Loads a database written by buildCornerDatabase or the engine's CornerDBMaker.
    // Returns nullptr if the file does not exist or has the wrong size.
    static std::shared_ptr<const CornerPatternDatabase> loadDatabase(const std::string &path);

    OptimalResult solve(const FaceletCube &cube, const SearchBudget &budget) const;

private:
    std::shared_ptr<const CornerPatternDatabase> db_;
};

struct CornerDbBuildStats {
    uint64_t states = 0;
    int maxDepth = 0;
    double seconds = 0;
};

// Computes the complete corner database (all 88,179,840 corner states; the
// deepest is 11 moves) and writes it in the engine's file format.
//
// The engine's CornerDBMaker keeps whole cube objects in a BFS queue and has to
// stop at depth ~8. This builder runs the BFS over integer coordinates
// (corner permutation x twist) using the Kociemba move tables, on a 4-bit-per-
// state array, then re-indexes into the engine's layout: ~10 s and ~90 MB.
CornerDbBuildStats buildCornerDatabase(const KociembaTables &tables, const std::string &path);

// Engine database index for the corners of `c` (exposed for tests).
uint32_t engineCornerIndex(const CubieCube &c);

}  // namespace zc
