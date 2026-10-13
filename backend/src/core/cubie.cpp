#include "core/cubie.h"

#include <stdexcept>

namespace zc {

namespace {

CubieCube makeMove(std::array<uint8_t, 8> cp, std::array<uint8_t, 8> co,
                   std::array<uint8_t, 12> ep, std::array<uint8_t, 12> eo) {
    CubieCube c;
    c.cp = cp;
    c.co = co;
    c.ep = ep;
    c.eo = eo;
    return c;
}

// The six basic quarter turns ("is replaced by" notation, Kociemba).
const std::array<CubieCube, 6> &basicMoves() {
    static const std::array<CubieCube, 6> moves = {
            // U
            makeMove({UBR, URF, UFL, ULB, DFR, DLF, DBL, DRB}, {0, 0, 0, 0, 0, 0, 0, 0},
                     {UB, UR, UF, UL, DR, DF, DL, DB, FR, FL, BL, BR}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}),
            // R
            makeMove({DFR, UFL, ULB, URF, DRB, DLF, DBL, UBR}, {2, 0, 0, 1, 1, 0, 0, 2},
                     {FR, UF, UL, UB, BR, DF, DL, DB, DR, FL, BL, UR}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}),
            // F
            makeMove({UFL, DLF, ULB, UBR, URF, DFR, DBL, DRB}, {1, 2, 0, 0, 2, 1, 0, 0},
                     {UR, FL, UL, UB, DR, FR, DL, DB, UF, DF, BL, BR}, {0, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0}),
            // D
            makeMove({URF, UFL, ULB, UBR, DLF, DBL, DRB, DFR}, {0, 0, 0, 0, 0, 0, 0, 0},
                     {UR, UF, UL, UB, DF, DL, DB, DR, FR, FL, BL, BR}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}),
            // L
            makeMove({URF, ULB, DBL, UBR, DFR, UFL, DLF, DRB}, {0, 1, 2, 0, 0, 2, 1, 0},
                     {UR, UF, BL, UB, DR, DF, FL, DB, FR, UL, DL, BR}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}),
            // B
            makeMove({URF, UFL, UBR, DRB, DFR, DLF, ULB, DBL}, {0, 0, 1, 2, 0, 0, 2, 1},
                     {UR, UF, UL, BR, DR, DF, DL, BL, FR, FL, UB, DB}, {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1}),
    };
    return moves;
}

int factorial(int n) {
    int f = 1;
    for (int i = 2; i <= n; ++i) f *= i;
    return f;
}

}  // namespace

int binomial(int n, int k) {
    if (k < 0 || n < k) return 0;
    if (k > n / 2) k = n - k;
    int r = 1;
    for (int i = 1; i <= k; ++i) r = r * (n - k + i) / i;
    return r;
}

int permutationRank(const uint8_t *perm, int n) {
    int rank = 0;
    for (int i = 0; i < n; ++i) {
        int smaller = 0;
        for (int j = i + 1; j < n; ++j) smaller += perm[j] < perm[i];
        rank = rank * (n - i) + smaller;
    }
    return rank;
}

void permutationUnrank(int rank, uint8_t *perm, int n) {
    uint8_t pool[12];
    for (int i = 0; i < n; ++i) pool[i] = static_cast<uint8_t>(i);
    int remaining = n;
    for (int i = 0; i < n; ++i) {
        int f = factorial(n - 1 - i);
        int idx = rank / f;
        rank %= f;
        perm[i] = pool[idx];
        for (int k = idx; k < remaining - 1; ++k) pool[k] = pool[k + 1];
        --remaining;
    }
}

const CubieCube &CubieCube::basicMove(int face) { return basicMoves().at(static_cast<size_t>(face)); }

void CubieCube::multiply(const CubieCube &b) {
    std::array<uint8_t, 8> ncp{}, nco{};
    for (int i = 0; i < 8; ++i) {
        ncp[i] = cp[b.cp[i]];
        nco[i] = static_cast<uint8_t>((co[b.cp[i]] + b.co[i]) % 3);
    }
    std::array<uint8_t, 12> nep{}, neo{};
    for (int i = 0; i < 12; ++i) {
        nep[i] = ep[b.ep[i]];
        neo[i] = static_cast<uint8_t>((eo[b.ep[i]] + b.eo[i]) % 2);
    }
    cp = ncp;
    co = nco;
    ep = nep;
    eo = neo;
}

void CubieCube::applyFaceMove(int m) {
    const CubieCube &mv = basicMove(m / 3);
    for (int k = 0; k <= m % 3; ++k) multiply(mv);
}

void CubieCube::apply(const std::vector<Move> &moves) {
    for (const auto &m: moves) {
        if (m.isRotation()) throw std::invalid_argument("CubieCube cannot apply whole-cube rotations");
        applyFaceMove(m.faceMoveIndex());
    }
}

FaceletCube CubieCube::toFacelets() const {
    std::string s(kStickers, ' ');
    for (int f = 0; f < kNumFaces; ++f) s[f * 9 + 4] = kFaceChars[f];
    for (int i = 0; i < 8; ++i) {
        int j = cp[i], ori = co[i];
        for (int n = 0; n < 3; ++n) {
            s[kCornerFacelets[i][(n + ori) % 3]] = kFaceChars[kCornerColors[j][n]];
        }
    }
    for (int i = 0; i < 12; ++i) {
        int j = ep[i], ori = eo[i];
        for (int n = 0; n < 2; ++n) {
            s[kEdgeFacelets[i][(n + ori) % 2]] = kFaceChars[kEdgeColors[j][n]];
        }
    }
    return FaceletCube::fromString(s);
}

std::optional<CubieCube> CubieCube::fromFacelets(const FaceletCube &fc) {
    auto faceOf = [](char c) -> int {
        for (int f = 0; f < kNumFaces; ++f) if (kFaceChars[f] == c) return f;
        return -1;
    };
    CubieCube c;
    std::array<bool, 8> seenCorner{};
    for (int i = 0; i < 8; ++i) {
        int ori = 0;
        for (; ori < 3; ++ori) {
            int col = faceOf(fc[kCornerFacelets[i][ori]]);
            if (col == 0 || col == 3) break;  // U or D colour
        }
        if (ori == 3) return std::nullopt;
        int col1 = faceOf(fc[kCornerFacelets[i][(ori + 1) % 3]]);
        int col2 = faceOf(fc[kCornerFacelets[i][(ori + 2) % 3]]);
        int found = -1;
        for (int j = 0; j < 8; ++j) {
            if (kCornerColors[j][1] == col1 && kCornerColors[j][2] == col2 &&
                kCornerColors[j][0] == faceOf(fc[kCornerFacelets[i][ori]])) {
                found = j;
                break;
            }
        }
        if (found < 0 || seenCorner[found]) return std::nullopt;
        seenCorner[found] = true;
        c.cp[i] = static_cast<uint8_t>(found);
        c.co[i] = static_cast<uint8_t>(ori);
    }
    std::array<bool, 12> seenEdge{};
    for (int i = 0; i < 12; ++i) {
        int a = faceOf(fc[kEdgeFacelets[i][0]]), b = faceOf(fc[kEdgeFacelets[i][1]]);
        int found = -1, ori = 0;
        for (int j = 0; j < 12; ++j) {
            if (kEdgeColors[j][0] == a && kEdgeColors[j][1] == b) { found = j; ori = 0; break; }
            if (kEdgeColors[j][0] == b && kEdgeColors[j][1] == a) { found = j; ori = 1; break; }
        }
        if (found < 0 || seenEdge[found]) return std::nullopt;
        seenEdge[found] = true;
        c.ep[i] = static_cast<uint8_t>(found);
        c.eo[i] = static_cast<uint8_t>(ori);
    }
    return c;
}

bool CubieCube::isSolved() const { return *this == CubieCube{}; }

int CubieCube::cornerParity() const {
    int s = 0;
    for (int i = 7; i > 0; --i)
        for (int j = i - 1; j >= 0; --j) s += cp[j] > cp[i];
    return s % 2;
}

int CubieCube::edgeParity() const {
    int s = 0;
    for (int i = 11; i > 0; --i)
        for (int j = i - 1; j >= 0; --j) s += ep[j] > ep[i];
    return s % 2;
}

int CubieCube::twistSum() const {
    int s = 0;
    for (auto v: co) s += v;
    return s;
}

int CubieCube::flipSum() const {
    int s = 0;
    for (auto v: eo) s += v;
    return s;
}

int CubieCube::twist() const {
    int r = 0;
    for (int i = URF; i < DRB; ++i) r = 3 * r + co[i];
    return r;
}

void CubieCube::setTwist(int v) {
    int parity = 0;
    for (int i = DRB - 1; i >= URF; --i) {
        co[i] = static_cast<uint8_t>(v % 3);
        parity += co[i];
        v /= 3;
    }
    co[DRB] = static_cast<uint8_t>((3 - parity % 3) % 3);
}

int CubieCube::flip() const {
    int r = 0;
    for (int i = UR; i < BR; ++i) r = 2 * r + eo[i];
    return r;
}

void CubieCube::setFlip(int v) {
    int parity = 0;
    for (int i = BR - 1; i >= UR; --i) {
        eo[i] = static_cast<uint8_t>(v % 2);
        parity += eo[i];
        v /= 2;
    }
    eo[BR] = static_cast<uint8_t>((2 - parity % 2) % 2);
}

int CubieCube::slice() const {
    int a = 0, x = 0;
    for (int j = BR; j >= UR; --j) {
        if (ep[j] >= FR) {
            a += binomial(11 - j, x + 1);
            ++x;
        }
    }
    return a;
}

void CubieCube::setSlice(int v) {
    static const uint8_t sliceEdge[4] = {FR, FL, BL, BR};
    static const uint8_t otherEdge[8] = {UR, UF, UL, UB, DR, DF, DL, DB};
    std::array<int, 12> slot;
    slot.fill(-1);
    int x = 4;
    for (int j = UR; j <= BR; ++j) {
        if (x > 0 && v - binomial(11 - j, x) >= 0) {
            slot[j] = sliceEdge[4 - x];
            v -= binomial(11 - j, x);
            --x;
        }
    }
    x = 0;
    for (int j = UR; j <= BR; ++j) {
        if (slot[j] == -1) slot[j] = otherEdge[x++];
        ep[j] = static_cast<uint8_t>(slot[j]);
    }
    eo.fill(0);
}

int CubieCube::cornerPerm() const { return permutationRank(cp.data(), 8); }

void CubieCube::setCornerPerm(int v) { permutationUnrank(v, cp.data(), 8); }

int CubieCube::udEdgePerm() const { return permutationRank(ep.data(), 8); }

void CubieCube::setUdEdgePerm(int v) {
    permutationUnrank(v, ep.data(), 8);
    for (int i = 8; i < 12; ++i) ep[i] = static_cast<uint8_t>(i);
}

int CubieCube::sliceSorted() const {
    uint8_t p[4];
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(ep[8 + i] - 8);
    return permutationRank(p, 4);
}

void CubieCube::setSliceSorted(int v) {
    uint8_t p[4];
    permutationUnrank(v, p, 4);
    for (int i = 0; i < 8; ++i) ep[i] = static_cast<uint8_t>(i);
    for (int i = 0; i < 4; ++i) ep[8 + i] = static_cast<uint8_t>(p[i] + 8);
}

}  // namespace zc
