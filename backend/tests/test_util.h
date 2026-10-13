#pragma once

#include <random>
#include <vector>

#include "core/facelet.h"
#include "core/moves.h"

namespace zc::test {

inline std::vector<Move> randomFaceMoves(std::mt19937 &rng, int n) {
    std::vector<Move> out;
    std::uniform_int_distribution<int> d(0, 17);
    int lastFace = -1;
    while (static_cast<int>(out.size()) < n) {
        Move m = Move::fromFaceMoveIndex(d(rng));
        if (m.face() == lastFace) continue;
        lastFace = m.face();
        out.push_back(m);
    }
    return out;
}

inline FaceletCube scrambled(std::mt19937 &rng, int n) {
    FaceletCube c;
    c.apply(randomFaceMoves(rng, n));
    return c;
}

}  // namespace zc::test
