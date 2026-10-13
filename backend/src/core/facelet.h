#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/moves.h"

namespace zc {

// Sticker layout (Kociemba's facelet string): 54 characters, faces in the order
// U R F D L B, each face read row by row as you look straight at it, laid out as
// the usual net:
//
//              U1 U2 U3
//              U4 U5 U6
//              U7 U8 U9
//   L1 L2 L3   F1 F2 F3   R1 R2 R3   B1 B2 B3
//   L4 L5 L6   F4 F5 F6   R4 R5 R6   B4 B5 B6
//   L7 L8 L9   F7 F8 F9   R7 R8 R9   B7 B8 B9
//              D1 D2 D3
//              D4 D5 D6
//              D7 D8 D9
//
// Each character is the face letter of the centre that sticker's colour belongs
// to, so the format is independent of the cube's colour scheme.
inline constexpr int kStickers = 54;

constexpr int faceletIndex(int face, int row, int col) { return face * 9 + row * 3 + col; }

// Facelets of each corner/edge position, in Kociemba order. The first facelet of
// a corner is on U or D; the rest follow clockwise.
enum Corner : uint8_t { URF, UFL, ULB, UBR, DFR, DLF, DBL, DRB };
enum Edge : uint8_t { UR, UF, UL, UB, DR, DF, DL, DB, FR, FL, BL, BR };

extern const std::array<std::array<uint8_t, 3>, 8> kCornerFacelets;
extern const std::array<std::array<uint8_t, 2>, 12> kEdgeFacelets;
// Home colours (as face indices U=0..B=5) of each corner and edge.
extern const std::array<std::array<uint8_t, 3>, 8> kCornerColors;
extern const std::array<std::array<uint8_t, 2>, 12> kEdgeColors;
extern const std::array<const char *, 8> kCornerNames;
extern const std::array<const char *, 12> kEdgeNames;

// 3D position of a sticker: the cubie it sits on (each coordinate in {-1, 0, 1};
// x = right, y = up, z = front) and the direction its face points.
struct StickerGeometry {
    std::array<int8_t, 3> pos;
    std::array<int8_t, 3> normal;
};
const StickerGeometry &stickerGeometry(int index);

// Permutation for a move: after the move, sticker j holds what sticker perm[j] held.
// Generated from the 3D geometry above rather than written by hand.
using StickerPerm = std::array<uint8_t, kStickers>;
const StickerPerm &movePermutation(const Move &m);

class FaceletCube {
public:
    FaceletCube();  // solved

    // `s` must be 54 characters; no other validation (see validation.h).
    static FaceletCube fromString(std::string_view s);

    char operator[](int i) const { return s_[static_cast<size_t>(i)]; }
    char center(int face) const { return s_[static_cast<size_t>(face * 9 + 4)]; }
    std::string str() const { return std::string(s_.begin(), s_.end()); }

    void apply(const Move &m);
    void apply(const std::vector<Move> &moves);
    FaceletCube after(const std::vector<Move> &moves) const;

    // Every face is a single colour (in any orientation of the whole cube).
    bool isSolved() const;

    bool operator==(const FaceletCube &o) const { return s_ == o.s_; }
    bool operator!=(const FaceletCube &o) const { return s_ != o.s_; }

private:
    std::array<char, kStickers> s_{};
};

}  // namespace zc
