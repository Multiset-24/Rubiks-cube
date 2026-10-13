#include <gtest/gtest.h>

#include <algorithm>

#include "core/validation.h"

using namespace zc;

namespace {
const std::string kSolved = "UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB";

bool hasCode(const ValidationResult &r, const std::string &code) {
    return std::any_of(r.issues.begin(), r.issues.end(), [&](const ValidationIssue &i) { return i.code == code; });
}
}  // namespace

TEST(Validation, AcceptsSolvedAndScrambledCubes) {
    EXPECT_TRUE(validateCube(kSolved).valid);
    EXPECT_TRUE(validateCube(FaceletCube().after(parseMoves("R U R' U' F2 D L' B")).str()).valid);
}

TEST(Validation, AcceptsColourStringsMappedByCentres) {
    std::string colours = "WWWWWWWWWRRRRRRRRRGGGGGGGGGYYYYYYYYYOOOOOOOOOBBBBBBBBB";
    auto r = validateCube(colours);
    ASSERT_TRUE(r.valid);
    EXPECT_EQ(r.facelets->str(), kSolved);
}

TEST(Validation, RejectsWrongLength) {
    auto r = validateCube("UUU");
    EXPECT_FALSE(r.valid);
    EXPECT_TRUE(hasCode(r, "LENGTH"));
}

TEST(Validation, RejectsDuplicateCentres) {
    std::string s = kSolved;
    s[13] = 'U';
    EXPECT_TRUE(hasCode(validateCube(s), "CENTERS"));
}

TEST(Validation, ReportsColourCountsWithNames) {
    std::string s = kSolved;
    s[0] = 'D';  // one extra yellow, one missing white
    auto r = validateCube(s);
    ASSERT_TRUE(hasCode(r, "COLOR_COUNT"));
    EXPECT_NE(r.issues[0].message.find("white"), std::string::npos);
}

TEST(Validation, DetectsTwistedCorner) {
    // Rotate the URF corner's stickers in place.
    std::string s = kSolved;
    std::swap(s[8], s[9]);   // U9 <-> R1
    std::swap(s[9], s[20]);  // then R1 <-> F3
    auto r = validateCube(s);
    EXPECT_FALSE(r.valid);
    EXPECT_TRUE(hasCode(r, "TWISTED_CORNER"));
}

TEST(Validation, DetectsFlippedEdge) {
    std::string s = kSolved;
    std::swap(s[5], s[10]);  // UR edge
    auto r = validateCube(s);
    EXPECT_TRUE(hasCode(r, "FLIPPED_EDGE"));
}

TEST(Validation, DetectsSwappedPieces) {
    // Swap UR and UF edges (stickers and all).
    std::string s = FaceletCube().str();
    std::swap(s[5], s[7]);
    std::swap(s[10], s[19]);
    auto r = validateCube(s);
    EXPECT_TRUE(hasCode(r, "PARITY"));
}

TEST(Validation, DetectsImpossibleEdge) {
    std::string s = kSolved;
    std::swap(s[10], s[28]);  // UR shows U+D? (R1 sticker swapped with a D sticker)
    auto r = validateCube(s);
    EXPECT_FALSE(r.valid);
    EXPECT_FALSE(r.issues.empty());
    EXPECT_FALSE(r.issues[0].facelets.empty());
}

TEST(Validation, DetectsUnknownColour) {
    std::string s = "WWWWWWWWWRRRRRRRRRGGGGGGGGGYYYYYYYYYOOOOOOOOOBBBBBBBBX";
    EXPECT_TRUE(hasCode(validateCube(s), "UNKNOWN_COLOR"));
}
