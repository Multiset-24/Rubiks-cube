#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/facelet.h"
#include "core/moves.h"

namespace zc {

// Piece-level cube: which corner/edge sits in each position and how it is
// twisted/flipped. This is the representation Kociemba's algorithm works on.
//   cp[i] = corner cubie at position i, co[i] = its twist (0..2)
//   ep[i] = edge cubie at position i,   eo[i] = its flip (0..1)
struct CubieCube {
    std::array<uint8_t, 8> cp{0, 1, 2, 3, 4, 5, 6, 7};
    std::array<uint8_t, 8> co{};
    std::array<uint8_t, 12> ep{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    std::array<uint8_t, 12> eo{};

    static const CubieCube &basicMove(int face);  // quarter turn of U R F D L B

    // this = this * b (apply b after this)
    void multiply(const CubieCube &b);
    void applyFaceMove(int faceMoveIndex);   // 0..17
    void apply(const std::vector<Move> &moves);  // face turns only

    FaceletCube toFacelets() const;
    // Fails if any position does not hold a real, uniquely-identified piece.
    static std::optional<CubieCube> fromFacelets(const FaceletCube &fc);

    bool isSolved() const;
    int cornerParity() const;
    int edgeParity() const;
    int twistSum() const;  // mod 3 must be 0 for a solvable cube
    int flipSum() const;   // mod 2 must be 0

    // --- coordinates for the two-phase algorithm ---
    int twist() const;           // 0..2186   corner orientation
    void setTwist(int v);
    int flip() const;            // 0..2047   edge orientation
    void setFlip(int v);
    int slice() const;           // 0..494    which positions hold the 4 middle-layer edges
    void setSlice(int v);
    int cornerPerm() const;      // 0..40319
    void setCornerPerm(int v);
    int udEdgePerm() const;      // 0..40319  permutation of the 8 U/D edges (phase 2)
    void setUdEdgePerm(int v);
    int sliceSorted() const;     // 0..23     permutation of the 4 slice edges (phase 2)
    void setSliceSorted(int v);

    bool operator==(const CubieCube &o) const {
        return cp == o.cp && co == o.co && ep == o.ep && eo == o.eo;
    }
};

// Lexicographic (Lehmer) rank of a permutation of {0..n-1}, and its inverse.
int permutationRank(const uint8_t *perm, int n);
void permutationUnrank(int rank, uint8_t *perm, int n);
int binomial(int n, int k);

// A uniformly random solvable cube (used for WCA-style random-state scrambles).
template<typename Rng>
CubieCube randomCubieCube(Rng &rng);

}  // namespace zc

#include "core/cubie_random.inl"
