#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace zc {

// Faces in the order used by the 54-character facelet string (Kociemba's
// convention): U R F D L B.
inline constexpr int kNumFaces = 6;
inline constexpr char kFaceChars[kNumFaces + 1] = "URFDLB";

// A move turns one face layer, or rotates the whole cube (x, y, z).
enum class Axis : uint8_t { U, R, F, D, L, B, X, Y, Z };

struct Move {
    Axis axis = Axis::U;
    uint8_t turns = 1;  // 1 = clockwise quarter turn, 2 = half turn, 3 = counter-clockwise

    bool isRotation() const { return axis >= Axis::X; }
    int face() const { return static_cast<int>(axis); }  // 0..5 for face turns

    // Index among the 18 face turns: face * 3 + (turns - 1), i.e. U U2 U' R R2 R' ...
    int faceMoveIndex() const { return face() * 3 + (turns - 1); }
    static Move fromFaceMoveIndex(int m) {
        return Move{static_cast<Axis>(m / 3), static_cast<uint8_t>(m % 3 + 1)};
    }

    Move inverse() const { return Move{axis, static_cast<uint8_t>(4 - turns)}; }
    std::string str() const;

    bool operator==(const Move &o) const { return axis == o.axis && turns == o.turns; }
    bool operator!=(const Move &o) const { return !(*this == o); }
};

// Parses standard notation such as "R U R' U' y2". Accepts ' and the typographic
// right quote as prime. Throws std::invalid_argument on an unknown token.
std::vector<Move> parseMoves(std::string_view text);

std::string formatMoves(const std::vector<Move> &moves);

std::vector<Move> invertMoves(const std::vector<Move> &moves);

// Merges consecutive turns of the same layer (R R -> R2, R R' -> nothing).
std::vector<Move> simplifyMoves(const std::vector<Move> &moves);

// Number of face turns (half-turn metric); whole-cube rotations do not count.
int countFaceTurns(const std::vector<Move> &moves);

// Repeats `moves` n times.
std::vector<Move> repeatMoves(const std::vector<Move> &moves, int n);

}  // namespace zc
