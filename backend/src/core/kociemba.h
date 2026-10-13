#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/budget.h"
#include "core/cubie.h"
#include "core/moves.h"

namespace zc {

// Move and pruning tables for Kociemba's two-phase algorithm.
//
// Phase 1 reduces the cube to the subgroup G1 = <U, D, R2, L2, F2, B2>, in which
// every piece is oriented and the middle-layer edges are in the middle layer.
// Phase 2 solves within G1 using only those moves. Each phase is an IDA* search
// over small coordinates, with pruning tables (exact distances in a projection
// of the state space) as admissible heuristics.
class KociembaTables {
public:
    static constexpr int kTwist = 2187, kFlip = 2048, kSlice = 495;
    static constexpr int kCornerPerm = 40320, kUdEdgePerm = 40320, kSliceSorted = 24;
    static constexpr int kPhase2Moves = 10;
    // U U2 U' D D2 D' R2 F2 L2 B2 as face-move indices.
    static constexpr std::array<int, kPhase2Moves> kPhase2MoveList = {0, 1, 2, 9, 10, 11, 4, 7, 13, 16};

    // Builds all tables (~0.3 s on one core, ~6 MB).
    static std::shared_ptr<const KociembaTables> build();

    // Phase 1
    std::vector<uint16_t> twistMove;    // [kTwist * 18]
    std::vector<uint16_t> flipMove;     // [kFlip * 18]
    std::vector<uint16_t> sliceMove;    // [kSlice * 18]
    std::vector<uint8_t> twistSlicePrune;  // [kTwist * kSlice]
    std::vector<uint8_t> flipSlicePrune;   // [kFlip * kSlice]
    // Phase 2 (corner perm table covers all 18 moves; also used by the corner DB builder)
    std::vector<uint16_t> cornerPermMove;   // [kCornerPerm * 18]
    std::vector<uint16_t> udEdgePermMove;   // [kUdEdgePerm * kPhase2Moves]
    std::vector<uint8_t> sliceSortedMove;   // [kSliceSorted * kPhase2Moves]
    std::vector<uint8_t> cornerSlicePrune;  // [kCornerPerm * kSliceSorted]
    std::vector<uint8_t> edgeSlicePrune;    // [kUdEdgePerm * kSliceSorted]

    size_t memoryBytes() const;
};

struct KociembaResult {
    bool found = false;
    bool timedOut = false;
    std::vector<Move> moves;
    uint64_t nodes = 0;
    int phase1Length = 0;
};

// Finds a solution of at most `maxLength` face turns (typically 19-22 moves for a
// random cube within a few milliseconds). Returns found=false if none was found
// within the budget.
KociembaResult solveKociemba(const KociembaTables &tables, const CubieCube &cube, int maxLength,
                             const SearchBudget &budget);

}  // namespace zc
