#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "core/cubie.h"
#include "core/facelet.h"

namespace zc {

// Display name of the colour on each face (indexed U R F D L B), used to write
// messages a person can act on ("10 white stickers") instead of face letters.
using ColorNames = std::array<std::string, kNumFaces>;
const ColorNames &defaultColorNames();  // white, red, green, yellow, orange, blue

struct ValidationIssue {
    std::string code;      // machine-readable, e.g. "TWISTED_CORNER"
    std::string message;   // what is wrong
    std::string hint;      // what to do about it
    std::vector<int> facelets;  // stickers to highlight in the UI
};

struct ValidationResult {
    bool valid = false;
    std::vector<ValidationIssue> issues;
    std::optional<FaceletCube> facelets;  // normalised URFDLB string, if parseable
    std::optional<CubieCube> cubie;       // set when valid
};

// Accepts either a facelet string (URFDLB letters, centres in U R F D L B order)
// or a colour string using any six distinct characters (e.g. "WWWW...") whose
// centres identify the faces. Checks everything a scan can get wrong:
// length, sticker counts, impossible pieces, duplicates, twist, flip, parity.
ValidationResult validateCube(const std::string &input, const ColorNames &names = defaultColorNames());

}  // namespace zc
