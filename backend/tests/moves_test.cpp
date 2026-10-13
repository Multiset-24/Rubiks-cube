#include <gtest/gtest.h>

#include "core/moves.h"

using namespace zc;

TEST(Moves, ParseAndFormatRoundTrip) {
    auto moves = parseMoves("R U R' U' F2 x y2 z'");
    ASSERT_EQ(moves.size(), 8u);
    EXPECT_EQ(formatMoves(moves), "R U R' U' F2 x y2 z'");
}

TEST(Moves, AcceptsTypographicPrimeAndSeparators) {
    EXPECT_EQ(formatMoves(parseMoves("R\xE2\x80\x99, (U2') D3")), "R' U2 D'");
}

TEST(Moves, RejectsUnknownTokens) {
    EXPECT_THROW(parseMoves("R Q"), std::invalid_argument);
    EXPECT_THROW(parseMoves("M"), std::invalid_argument);
}

TEST(Moves, InvertReversesAndInvertsEachMove) {
    EXPECT_EQ(formatMoves(invertMoves(parseMoves("R U2 F'"))), "F U2 R'");
}

TEST(Moves, SimplifyMergesSameLayer) {
    EXPECT_EQ(formatMoves(simplifyMoves(parseMoves("R R U U' F2 F2 L L L"))), "R2 L'");
}

TEST(Moves, FaceMoveIndexRoundTrip) {
    for (int m = 0; m < 18; ++m) EXPECT_EQ(Move::fromFaceMoveIndex(m).faceMoveIndex(), m);
    EXPECT_EQ(Move::fromFaceMoveIndex(0).str(), "U");
    EXPECT_EQ(Move::fromFaceMoveIndex(4).str(), "R2");
    EXPECT_EQ(Move::fromFaceMoveIndex(17).str(), "B'");
}

TEST(Moves, CountFaceTurnsIgnoresRotations) {
    EXPECT_EQ(countFaceTurns(parseMoves("x R U y R'")), 3);
}
