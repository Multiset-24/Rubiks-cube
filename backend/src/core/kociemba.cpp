#include "core/kociemba.h"

#include <algorithm>

namespace zc {

namespace {

constexpr int kNoFace = -1;

// Faces are U R F D L B (0..5); opposite pairs are (0,3) (1,4) (2,5).
// Skip a turn of the same face as the previous move, and for commuting opposite
// faces allow only one order (e.g. U D but not D U).
inline bool skipMove(int face, int lastFace) {
    if (lastFace == kNoFace) return false;
    if (face == lastFace) return true;
    return face % 3 == lastFace % 3 && face < lastFace;
}

// Breadth-first fill of a pruning table over a pair of coordinates (a, b) with
// index a * nb + b. `next(a, b, k, &na, &nb)` applies move k.
template<typename Next>
std::vector<uint8_t> buildPrune(int na, int nb, int numMoves, Next next) {
    std::vector<uint8_t> table(static_cast<size_t>(na) * nb, 0xFF);
    table[0] = 0;
    size_t filled = 1, total = table.size();
    for (uint8_t depth = 0; filled < total; ++depth) {
        for (int a = 0; a < na; ++a) {
            for (int b = 0; b < nb; ++b) {
                if (table[static_cast<size_t>(a) * nb + b] != depth) continue;
                for (int k = 0; k < numMoves; ++k) {
                    int a2, b2;
                    next(a, b, k, &a2, &b2);
                    size_t idx = static_cast<size_t>(a2) * nb + b2;
                    if (table[idx] == 0xFF) {
                        table[idx] = static_cast<uint8_t>(depth + 1);
                        ++filled;
                    }
                }
            }
        }
    }
    return table;
}

template<typename Get, typename Set>
std::vector<uint16_t> buildMoveTable(int n, Get get, Set set) {
    std::vector<uint16_t> table(static_cast<size_t>(n) * 18);
    for (int i = 0; i < n; ++i) {
        CubieCube c;
        set(c, i);
        for (int face = 0; face < 6; ++face) {
            for (int k = 0; k < 3; ++k) {
                c.multiply(CubieCube::basicMove(face));
                table[static_cast<size_t>(i) * 18 + face * 3 + k] = static_cast<uint16_t>(get(c));
            }
            c.multiply(CubieCube::basicMove(face));  // fourth turn restores the state
        }
    }
    return table;
}

}  // namespace

std::shared_ptr<const KociembaTables> KociembaTables::build() {
    auto t = std::make_shared<KociembaTables>();

    t->twistMove = buildMoveTable(kTwist, [](const CubieCube &c) { return c.twist(); },
                                  [](CubieCube &c, int v) { c.setTwist(v); });
    t->flipMove = buildMoveTable(kFlip, [](const CubieCube &c) { return c.flip(); },
                                 [](CubieCube &c, int v) { c.setFlip(v); });
    t->sliceMove = buildMoveTable(kSlice, [](const CubieCube &c) { return c.slice(); },
                                  [](CubieCube &c, int v) { c.setSlice(v); });
    t->cornerPermMove = buildMoveTable(kCornerPerm, [](const CubieCube &c) { return c.cornerPerm(); },
                                       [](CubieCube &c, int v) { c.setCornerPerm(v); });

    // Phase-2 edge tables are only defined for the 10 phase-2 moves.
    t->udEdgePermMove.resize(static_cast<size_t>(kUdEdgePerm) * kPhase2Moves);
    for (int i = 0; i < kUdEdgePerm; ++i) {
        for (int k = 0; k < kPhase2Moves; ++k) {
            CubieCube c;
            c.setUdEdgePerm(i);
            c.applyFaceMove(kPhase2MoveList[k]);
            t->udEdgePermMove[static_cast<size_t>(i) * kPhase2Moves + k] = static_cast<uint16_t>(c.udEdgePerm());
        }
    }
    t->sliceSortedMove.resize(static_cast<size_t>(kSliceSorted) * kPhase2Moves);
    for (int i = 0; i < kSliceSorted; ++i) {
        for (int k = 0; k < kPhase2Moves; ++k) {
            CubieCube c;
            c.setSliceSorted(i);
            c.applyFaceMove(kPhase2MoveList[k]);
            t->sliceSortedMove[static_cast<size_t>(i) * kPhase2Moves + k] = static_cast<uint8_t>(c.sliceSorted());
        }
    }

    const KociembaTables &T = *t;
    t->twistSlicePrune = buildPrune(kTwist, kSlice, 18, [&](int a, int b, int m, int *a2, int *b2) {
        *a2 = T.twistMove[static_cast<size_t>(a) * 18 + m];
        *b2 = T.sliceMove[static_cast<size_t>(b) * 18 + m];
    });
    t->flipSlicePrune = buildPrune(kFlip, kSlice, 18, [&](int a, int b, int m, int *a2, int *b2) {
        *a2 = T.flipMove[static_cast<size_t>(a) * 18 + m];
        *b2 = T.sliceMove[static_cast<size_t>(b) * 18 + m];
    });
    t->cornerSlicePrune = buildPrune(kCornerPerm, kSliceSorted, kPhase2Moves, [&](int a, int b, int k, int *a2, int *b2) {
        *a2 = T.cornerPermMove[static_cast<size_t>(a) * 18 + kPhase2MoveList[k]];
        *b2 = T.sliceSortedMove[static_cast<size_t>(b) * kPhase2Moves + k];
    });
    t->edgeSlicePrune = buildPrune(kUdEdgePerm, kSliceSorted, kPhase2Moves, [&](int a, int b, int k, int *a2, int *b2) {
        *a2 = T.udEdgePermMove[static_cast<size_t>(a) * kPhase2Moves + k];
        *b2 = T.sliceSortedMove[static_cast<size_t>(b) * kPhase2Moves + k];
    });
    return t;
}

size_t KociembaTables::memoryBytes() const {
    return twistMove.size() * 2 + flipMove.size() * 2 + sliceMove.size() * 2 + cornerPermMove.size() * 2 +
           udEdgePermMove.size() * 2 + sliceSortedMove.size() + twistSlicePrune.size() + flipSlicePrune.size() +
           cornerSlicePrune.size() + edgeSlicePrune.size();
}

namespace {

class TwoPhaseSearch {
public:
    TwoPhaseSearch(const KociembaTables &t, const CubieCube &cube, int maxLength, const SearchBudget &budget)
            : t_(t), cube_(cube), maxLength_(maxLength), budget_(budget) {}

    KociembaResult run() {
        KociembaResult r;
        int tw = cube_.twist(), fl = cube_.flip(), sl = cube_.slice();
        for (int depth1 = phase1Heuristic(tw, fl, sl); depth1 <= maxLength_ && !stopped_; ++depth1) {
            if (phase1(tw, fl, sl, depth1, 0, kNoFace)) {
                r.found = true;
                r.phase1Length = depth1;
                for (int i = 0; i < depth1; ++i) r.moves.push_back(Move::fromFaceMoveIndex(path_[i]));
                for (int i = 0; i < solutionLength2_; ++i) r.moves.push_back(Move::fromFaceMoveIndex(path2_[i]));
                r.moves = simplifyMoves(r.moves);
                break;
            }
        }
        r.nodes = nodes_;
        r.timedOut = stopped_;
        return r;
    }

private:
    int phase1Heuristic(int tw, int fl, int sl) const {
        return std::max(t_.twistSlicePrune[static_cast<size_t>(tw) * KociembaTables::kSlice + sl],
                        t_.flipSlicePrune[static_cast<size_t>(fl) * KociembaTables::kSlice + sl]);
    }

    int phase2Heuristic(int cp, int ud, int ss) const {
        return std::max(t_.cornerSlicePrune[static_cast<size_t>(cp) * KociembaTables::kSliceSorted + ss],
                        t_.edgeSlicePrune[static_cast<size_t>(ud) * KociembaTables::kSliceSorted + ss]);
    }

    bool tick() {
        if ((++nodes_ & 4095) == 0 && budget_.expired(nodes_)) stopped_ = true;
        return stopped_;
    }

    bool phase1(int tw, int fl, int sl, int togo, int n, int lastFace) {
        if (togo == 0) return startPhase2(n);
        for (int m = 0; m < 18; ++m) {
            int face = m / 3;
            if (skipMove(face, lastFace)) continue;
            if (tick()) return false;
            int tw2 = t_.twistMove[static_cast<size_t>(tw) * 18 + m];
            int fl2 = t_.flipMove[static_cast<size_t>(fl) * 18 + m];
            int sl2 = t_.sliceMove[static_cast<size_t>(sl) * 18 + m];
            if (phase1Heuristic(tw2, fl2, sl2) > togo - 1) continue;
            path_[n] = m;
            if (phase1(tw2, fl2, sl2, togo - 1, n + 1, face)) return true;
            if (stopped_) return false;
        }
        return false;
    }

    bool startPhase2(int n1) {
        // A phase-1 solution ending in a phase-2 move is redundant: a shorter phase-1
        // solution (without that move) was already tried at a smaller depth.
        if (n1 > 0) {
            int last = path_[n1 - 1];
            for (int k: KociembaTables::kPhase2MoveList) if (k == last) return false;
        }
        CubieCube c = cube_;
        for (int i = 0; i < n1; ++i) c.applyFaceMove(path_[i]);
        int cp = c.cornerPerm(), ud = c.udEdgePerm(), ss = c.sliceSorted();
        int limit = std::min(maxLength_ - n1, 18);
        int h = phase2Heuristic(cp, ud, ss);
        int lastFace = n1 > 0 ? path_[n1 - 1] / 3 : kNoFace;
        for (int depth2 = h; depth2 <= limit; ++depth2) {
            if (phase2(cp, ud, ss, depth2, 0, lastFace)) {
                solutionLength2_ = depth2;
                return true;
            }
            if (stopped_) return false;
        }
        return false;
    }

    bool phase2(int cp, int ud, int ss, int togo, int n, int lastFace) {
        if (togo == 0) return cp == 0 && ud == 0 && ss == 0;
        for (int k = 0; k < KociembaTables::kPhase2Moves; ++k) {
            int m = KociembaTables::kPhase2MoveList[k];
            int face = m / 3;
            if (skipMove(face, lastFace)) continue;
            if (tick()) return false;
            int cp2 = t_.cornerPermMove[static_cast<size_t>(cp) * 18 + m];
            int ud2 = t_.udEdgePermMove[static_cast<size_t>(ud) * KociembaTables::kPhase2Moves + k];
            int ss2 = t_.sliceSortedMove[static_cast<size_t>(ss) * KociembaTables::kPhase2Moves + k];
            if (phase2Heuristic(cp2, ud2, ss2) > togo - 1) continue;
            path2_[n] = m;
            if (phase2(cp2, ud2, ss2, togo - 1, n + 1, face)) return true;
            if (stopped_) return false;
        }
        return false;
    }

    const KociembaTables &t_;
    CubieCube cube_;
    int maxLength_;
    const SearchBudget &budget_;
    std::array<int, 32> path_{};
    std::array<int, 32> path2_{};
    int solutionLength2_ = 0;
    uint64_t nodes_ = 0;
    bool stopped_ = false;
};

}  // namespace

KociembaResult solveKociemba(const KociembaTables &tables, const CubieCube &cube, int maxLength,
                             const SearchBudget &budget) {
    if (cube.isSolved()) {
        KociembaResult r;
        r.found = true;
        return r;
    }
    return TwoPhaseSearch(tables, cube, maxLength, budget).run();
}

}  // namespace zc
