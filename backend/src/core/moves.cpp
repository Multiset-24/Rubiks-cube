#include "core/moves.h"

#include <cctype>
#include <stdexcept>

namespace zc {

namespace {
constexpr char kAxisChars[] = "URFDLBxyz";
}

std::string Move::str() const {
    std::string s(1, kAxisChars[static_cast<int>(axis)]);
    if (turns == 2) s += '2';
    else if (turns == 3) s += '\'';
    return s;
}

std::vector<Move> parseMoves(std::string_view text) {
    std::vector<Move> out;
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (std::isspace(c) || c == ',' || c == '(' || c == ')') {
            ++i;
            continue;
        }
        Move m;
        switch (c) {
            case 'U': m.axis = Axis::U; break;
            case 'R': m.axis = Axis::R; break;
            case 'F': m.axis = Axis::F; break;
            case 'D': m.axis = Axis::D; break;
            case 'L': m.axis = Axis::L; break;
            case 'B': m.axis = Axis::B; break;
            case 'x': case 'X': m.axis = Axis::X; break;
            case 'y': case 'Y': m.axis = Axis::Y; break;
            case 'z': case 'Z': m.axis = Axis::Z; break;
            default:
                throw std::invalid_argument("unknown move '" + std::string(1, static_cast<char>(c)) +
                                            "' at position " + std::to_string(i));
        }
        ++i;
        int turns = 1;
        if (i < text.size() && (text[i] == '2' || text[i] == '3')) {
            turns = text[i] - '0';
            ++i;
        }
        if (i < text.size() && text[i] == '\'') {
            turns = 4 - turns;
            ++i;
        } else if (text.substr(i, 3) == "\xE2\x80\x99") {  // U+2019 right single quote
            turns = 4 - turns;
            i += 3;
        }
        m.turns = static_cast<uint8_t>(turns);
        out.push_back(m);
    }
    return out;
}

std::string formatMoves(const std::vector<Move> &moves) {
    std::string s;
    for (const auto &m: moves) {
        if (!s.empty()) s += ' ';
        s += m.str();
    }
    return s;
}

std::vector<Move> invertMoves(const std::vector<Move> &moves) {
    std::vector<Move> out;
    out.reserve(moves.size());
    for (auto it = moves.rbegin(); it != moves.rend(); ++it) out.push_back(it->inverse());
    return out;
}

std::vector<Move> simplifyMoves(const std::vector<Move> &moves) {
    std::vector<Move> out;
    for (const auto &m: moves) {
        if (!out.empty() && out.back().axis == m.axis) {
            int t = (out.back().turns + m.turns) % 4;
            if (t == 0) out.pop_back();
            else out.back().turns = static_cast<uint8_t>(t);
        } else {
            out.push_back(m);
        }
    }
    return out;
}

int countFaceTurns(const std::vector<Move> &moves) {
    int n = 0;
    for (const auto &m: moves) n += m.isRotation() ? 0 : 1;
    return n;
}

std::vector<Move> repeatMoves(const std::vector<Move> &moves, int n) {
    std::vector<Move> out;
    out.reserve(moves.size() * static_cast<size_t>(n > 0 ? n : 0));
    for (int i = 0; i < n; ++i) out.insert(out.end(), moves.begin(), moves.end());
    return out;
}

}  // namespace zc
