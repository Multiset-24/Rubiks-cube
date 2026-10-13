#pragma once

#include <random>

namespace zc {

template<typename Rng>
CubieCube randomCubieCube(Rng &rng) {
    CubieCube c;
    std::uniform_int_distribution<int> cpDist(0, 40319), twistDist(0, 2186), flipDist(0, 2047);
    std::uniform_int_distribution<int> epDist(0, 479001599);  // 12! - 1
    c.setCornerPerm(cpDist(rng));
    permutationUnrank(epDist(rng), c.ep.data(), 12);
    // Corner and edge permutation parities must match; fix by swapping two edges.
    if (c.cornerParity() != c.edgeParity()) std::swap(c.ep[0], c.ep[1]);
    c.setTwist(twistDist(rng));
    c.setFlip(flipDist(rng));
    return c;
}

}  // namespace zc
