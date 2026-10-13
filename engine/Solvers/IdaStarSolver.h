//
// Created by Saurav on 26-06-2025.
//

#ifndef RUBIKS_CUBE_SOLVER_IDASTARSOLVER_H
#define RUBIKS_CUBE_SOLVER_IDASTARSOLVER_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "../Model/RubiksCube.h"
#include "../Pattern_Database/CornerPatternDatabase.h"

/*
 * IDA* (Korf 1997): depth-first search bounded by f = g + h, with the corner
 * pattern database as h. Memory is O(depth). Two pruning rules cut the
 * branching factor from 18 to about 13.35: never turn the same face twice in a
 * row, and turn opposite faces in one fixed order only.
 *
 * Typename T: RubiksCube representation (3d, 1d, Bitboard)
 * Typename H: corresponding hash function (unused, kept for API compatibility)
 */
template<typename T, typename H>
class IDAstarSolver {
public:
    struct Budget {
        // 0 = unlimited.
        uint64_t maxNodes = 0;
        std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
        // Optional external cancellation (e.g. client disconnected).
        std::function<bool()> cancelled;
    };

    struct Stats {
        uint64_t nodesExpanded = 0;
        int iterations = 0;
        bool exhausted = false;   // stopped because of the budget
    };

private:
    std::shared_ptr<const CornerPatternDatabase> cornerDB;
    bool heuristicLoaded = false;
    vector<RubiksCube::MOVE> moves;
    Budget budget;
    Stats stats;

    static constexpr int FOUND = -1;
    static constexpr int INF = std::numeric_limits<int>::max();

    int heuristic(const T &cube) const {
        // No database: h = 0, i.e. plain iterative deepening.
        return heuristicLoaded ? cornerDB->getNumMoves(cube) : 0;
    }

    static int faceOf(int move) { return move / 3; }   // L, R, U, D, F, B

    // Faces are ordered L, R, U, D, F, B: (L,R), (U,D), (F,B) are opposite pairs.
    static bool redundant(int lastFace, int face) {
        if (lastFace < 0) return false;
        if (face == lastFace) return true;
        // Opposite faces commute: only allow the lower-indexed face first.
        return (face / 2 == lastFace / 2) && face < lastFace;
    }

    bool outOfBudget() {
        if (budget.maxNodes && stats.nodesExpanded >= budget.maxNodes) return true;
        // Checking the clock on every node is expensive; every 1024 nodes is plenty.
        if ((stats.nodesExpanded & 1023) == 0) {
            if (std::chrono::steady_clock::now() >= budget.deadline) return true;
            if (budget.cancelled && budget.cancelled()) return true;
        }
        return false;
    }

    // Returns FOUND, or the smallest f-value that exceeded the bound.
    int search(int g, int bound, int lastFace) {
        int h = heuristic(rubiksCube);
        int f = g + h;
        if (f > bound) return f;
        if (rubiksCube.isSolved()) return FOUND;

        ++stats.nodesExpanded;
        if (outOfBudget()) {
            stats.exhausted = true;
            return INF;
        }

        int minExceeded = INF;
        for (int i = 0; i < 18; i++) {
            int face = faceOf(i);
            if (redundant(lastFace, face)) continue;
            auto curr_move = RubiksCube::MOVE(i);
            rubiksCube.move(curr_move);
            moves.push_back(curr_move);
            int t = search(g + 1, bound, face);
            if (t == FOUND) return FOUND;
            moves.pop_back();
            rubiksCube.invert(curr_move);
            if (stats.exhausted) return INF;
            if (t < minExceeded) minExceeded = t;
        }
        return minExceeded;
    }

public:
    T rubiksCube;

    // Original API: loads the database from a file (or runs without a heuristic if
    // the file is missing).
    IDAstarSolver(T _rubiksCube, string fileName) : rubiksCube(_rubiksCube) {
        auto db = std::make_shared<CornerPatternDatabase>();
        heuristicLoaded = !fileName.empty() && db->fromFile(fileName);
        cornerDB = db;
    }

    // Shares one loaded database between solves (it is 44 MB).
    IDAstarSolver(T _rubiksCube, std::shared_ptr<const CornerPatternDatabase> db)
            : cornerDB(std::move(db)), rubiksCube(_rubiksCube) {
        heuristicLoaded = cornerDB != nullptr;
    }

    void setBudget(Budget b) { budget = std::move(b); }

    const Stats &getStats() const { return stats; }

    // Returns the optimal move sequence. If the budget runs out, returns an empty
    // vector and getStats().exhausted is true.
    vector<RubiksCube::MOVE> solve() {
        moves.clear();
        stats = Stats{};
        int bound = heuristic(rubiksCube);
        while (true) {
            ++stats.iterations;
            int t = search(0, bound, -1);
            if (t == FOUND) return moves;
            if (t == INF) {
                moves.clear();
                return moves;
            }
            bound = t;
        }
    }
};

#endif //RUBIKS_CUBE_SOLVER_IDASTARSOLVER_H
