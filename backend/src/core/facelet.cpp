#include "core/facelet.h"

#include <map>
#include <stdexcept>

namespace zc {

const std::array<std::array<uint8_t, 3>, 8> kCornerFacelets = {{
        {8, 9, 20},    // URF: U9 R1 F3
        {6, 18, 38},   // UFL: U7 F1 L3
        {0, 36, 47},   // ULB: U1 L1 B3
        {2, 45, 11},   // UBR: U3 B1 R3
        {29, 26, 15},  // DFR: D3 F9 R7
        {27, 44, 24},  // DLF: D1 L9 F7
        {33, 53, 42},  // DBL: D7 B9 L7
        {35, 17, 51},  // DRB: D9 R9 B7
}};

const std::array<std::array<uint8_t, 2>, 12> kEdgeFacelets = {{
        {5, 10},   // UR
        {7, 19},   // UF
        {3, 37},   // UL
        {1, 46},   // UB
        {32, 16},  // DR
        {28, 25},  // DF
        {30, 43},  // DL
        {34, 52},  // DB
        {23, 12},  // FR
        {21, 41},  // FL
        {50, 39},  // BL
        {48, 14},  // BR
}};

// U=0 R=1 F=2 D=3 L=4 B=5
const std::array<std::array<uint8_t, 3>, 8> kCornerColors = {{
        {0, 1, 2}, {0, 2, 4}, {0, 4, 5}, {0, 5, 1},
        {3, 2, 1}, {3, 4, 2}, {3, 5, 4}, {3, 1, 5},
}};

const std::array<std::array<uint8_t, 2>, 12> kEdgeColors = {{
        {0, 1}, {0, 2}, {0, 4}, {0, 5}, {3, 1}, {3, 2},
        {3, 4}, {3, 5}, {2, 1}, {2, 4}, {5, 4}, {5, 1},
}};

const std::array<const char *, 8> kCornerNames = {"URF", "UFL", "ULB", "UBR", "DFR", "DLF", "DBL", "DRB"};
const std::array<const char *, 12> kEdgeNames = {"UR", "UF", "UL", "UB", "DR", "DF",
                                                 "DL", "DB", "FR", "FL", "BL", "BR"};

namespace {

using Vec = std::array<int, 3>;

// Outward normal of each face, and of the rotation axes x (as R), y (as U), z (as F).
constexpr Vec kAxisNormal[9] = {
        {0, 1, 0}, {1, 0, 0}, {0, 0, 1}, {0, -1, 0}, {-1, 0, 0}, {0, 0, -1},  // U R F D L B
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1},                                      // x y z
};

StickerGeometry geometryFor(int index) {
    int face = index / 9, r = (index % 9) / 3, c = index % 3;
    StickerGeometry g{};
    auto set = [&](int x, int y, int z, int nx, int ny, int nz) {
        g.pos = {static_cast<int8_t>(x), static_cast<int8_t>(y), static_cast<int8_t>(z)};
        g.normal = {static_cast<int8_t>(nx), static_cast<int8_t>(ny), static_cast<int8_t>(nz)};
    };
    switch (face) {
        case 0: set(c - 1, 1, r - 1, 0, 1, 0); break;      // U: back row first
        case 1: set(1, 1 - r, 1 - c, 1, 0, 0); break;      // R: front column first
        case 2: set(c - 1, 1 - r, 1, 0, 0, 1); break;      // F
        case 3: set(c - 1, -1, 1 - r, 0, -1, 0); break;    // D: front row first
        case 4: set(-1, 1 - r, c - 1, -1, 0, 0); break;    // L: back column first
        default: set(1 - c, 1 - r, -1, 0, 0, -1); break;   // B: right column first
    }
    return g;
}

const std::array<StickerGeometry, kStickers> &allGeometry() {
    static const std::array<StickerGeometry, kStickers> g = [] {
        std::array<StickerGeometry, kStickers> a{};
        for (int i = 0; i < kStickers; ++i) a[static_cast<size_t>(i)] = geometryFor(i);
        return a;
    }();
    return g;
}

// Clockwise quarter turn as seen looking at the face with outward normal a,
// i.e. a rotation by -90 degrees about a:  v' = -(a x v) + a (a . v).
Vec rotateCw(const Vec &a, const Vec &v) {
    Vec cross = {a[1] * v[2] - a[2] * v[1], a[2] * v[0] - a[0] * v[2], a[0] * v[1] - a[1] * v[0]};
    int dot = a[0] * v[0] + a[1] * v[1] + a[2] * v[2];
    return {-cross[0] + a[0] * dot, -cross[1] + a[1] * dot, -cross[2] + a[2] * dot};
}

StickerPerm quarterTurnPerm(int axis) {
    const auto &geo = allGeometry();
    std::map<std::pair<Vec, Vec>, int> lookup;
    for (int i = 0; i < kStickers; ++i) {
        const auto &g = geo[static_cast<size_t>(i)];
        lookup[{{g.pos[0], g.pos[1], g.pos[2]}, {g.normal[0], g.normal[1], g.normal[2]}}] = i;
    }
    const Vec &a = kAxisNormal[axis];
    bool wholeCube = axis >= 6;
    StickerPerm perm{};
    for (int i = 0; i < kStickers; ++i) perm[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
    for (int i = 0; i < kStickers; ++i) {
        const auto &g = geo[static_cast<size_t>(i)];
        Vec p = {g.pos[0], g.pos[1], g.pos[2]};
        Vec n = {g.normal[0], g.normal[1], g.normal[2]};
        int layer = a[0] * p[0] + a[1] * p[1] + a[2] * p[2];
        if (!wholeCube && layer != 1) continue;
        int j = lookup.at({rotateCw(a, p), rotateCw(a, n)});
        perm[static_cast<size_t>(j)] = static_cast<uint8_t>(i);  // sticker i moves to j
    }
    return perm;
}

StickerPerm compose(const StickerPerm &first, const StickerPerm &second) {
    // Apply `first`, then `second`.
    StickerPerm out{};
    for (int j = 0; j < kStickers; ++j) out[static_cast<size_t>(j)] = first[second[static_cast<size_t>(j)]];
    return out;
}

}  // namespace

const StickerGeometry &stickerGeometry(int index) { return allGeometry().at(static_cast<size_t>(index)); }

const StickerPerm &movePermutation(const Move &m) {
    // [axis][turns - 1]
    static const auto table = [] {
        std::array<std::array<StickerPerm, 3>, 9> t{};
        for (int axis = 0; axis < 9; ++axis) {
            StickerPerm q = quarterTurnPerm(axis);
            t[static_cast<size_t>(axis)][0] = q;
            t[static_cast<size_t>(axis)][1] = compose(q, q);
            t[static_cast<size_t>(axis)][2] = compose(t[static_cast<size_t>(axis)][1], q);
        }
        return t;
    }();
    return table.at(static_cast<size_t>(m.axis)).at(static_cast<size_t>(m.turns - 1));
}

FaceletCube::FaceletCube() {
    for (int i = 0; i < kStickers; ++i) s_[static_cast<size_t>(i)] = kFaceChars[i / 9];
}

FaceletCube FaceletCube::fromString(std::string_view s) {
    if (s.size() != kStickers) {
        throw std::invalid_argument("expected 54 facelets, got " + std::to_string(s.size()));
    }
    FaceletCube c;
    for (int i = 0; i < kStickers; ++i) c.s_[static_cast<size_t>(i)] = s[static_cast<size_t>(i)];
    return c;
}

void FaceletCube::apply(const Move &m) {
    const StickerPerm &p = movePermutation(m);
    std::array<char, kStickers> next{};
    for (int j = 0; j < kStickers; ++j) next[static_cast<size_t>(j)] = s_[p[static_cast<size_t>(j)]];
    s_ = next;
}

void FaceletCube::apply(const std::vector<Move> &moves) {
    for (const auto &m: moves) apply(m);
}

FaceletCube FaceletCube::after(const std::vector<Move> &moves) const {
    FaceletCube c = *this;
    c.apply(moves);
    return c;
}

bool FaceletCube::isSolved() const {
    for (int f = 0; f < kNumFaces; ++f) {
        char c = center(f);
        for (int i = 0; i < 9; ++i) {
            if (s_[static_cast<size_t>(f * 9 + i)] != c) return false;
        }
    }
    return true;
}

}  // namespace zc
