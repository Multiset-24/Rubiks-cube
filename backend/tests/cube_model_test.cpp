#include <gtest/gtest.h>

#include <random>

#include "Model/RubiksCube3dArray.h"
#include "core/cubie.h"
#include "core/facelet.h"
#include "test_util.h"

using namespace zc;

TEST(Facelet, PieceFaceletsShareOneCubie) {
    for (const auto &corner: kCornerFacelets) {
        const auto &a = stickerGeometry(corner[0]).pos;
        EXPECT_EQ(stickerGeometry(corner[1]).pos, a);
        EXPECT_EQ(stickerGeometry(corner[2]).pos, a);
    }
    for (const auto &edge: kEdgeFacelets) {
        EXPECT_EQ(stickerGeometry(edge[0]).pos, stickerGeometry(edge[1]).pos);
    }
}

TEST(Facelet, EveryMoveHasOrderFourAndKeepsCentres) {
    std::mt19937 rng(1);
    for (int axis = 0; axis < 9; ++axis) {
        FaceletCube c = test::scrambled(rng, 20);
        FaceletCube start = c;
        Move m{static_cast<Axis>(axis), 1};
        for (int i = 0; i < 4; ++i) c.apply(m);
        EXPECT_EQ(c, start) << m.str();
        if (axis < 6) {
            FaceletCube s = start.after({m});
            for (int f = 0; f < 6; ++f) EXPECT_EQ(s.center(f), start.center(f));
        }
    }
}

TEST(Facelet, SexyMoveHasOrderSix) {
    FaceletCube c;
    auto alg = parseMoves("R U R' U'");
    for (int i = 0; i < 6; ++i) c.apply(alg);
    EXPECT_TRUE(c.isSolved());
    c.apply(alg);
    EXPECT_FALSE(c.isSolved());
}

TEST(Facelet, KnownStateAfterSingleMoves) {
    EXPECT_EQ(FaceletCube().after(parseMoves("U")).str(),
              "UUUUUUUUUBBBRRRRRRRRRFFFFFFDDDDDDDDDFFFLLLLLLLLLBBBBBB");
    EXPECT_EQ(FaceletCube().after(parseMoves("R")).str(),
              "UUFUUFUUFRRRRRRRRRFFDFFDFFDDDBDDBDDBLLLLLLLLLUBBUBBUBB");
}

TEST(Facelet, RotationsAreFaceTurnCombinations) {
    // y == U D' (as layers) plus the middle slice; check on centres only via whole state:
    // x maps the cube the same way R does, applied to all three layers.
    FaceletCube c = FaceletCube().after(parseMoves("x"));
    EXPECT_EQ(c.center(0), 'F');  // front comes up
    EXPECT_EQ(c.center(2), 'D');
    c = FaceletCube().after(parseMoves("y"));
    EXPECT_EQ(c.center(2), 'R');  // right comes to the front
    c = FaceletCube().after(parseMoves("z"));
    EXPECT_EQ(c.center(1), 'U');  // top goes to the right
    EXPECT_TRUE(FaceletCube().after(parseMoves("x y z x' z2")).isSolved());
}

TEST(Cubie, MatchesFaceletModelOnRandomSequences) {
    std::mt19937 rng(7);
    for (int trial = 0; trial < 300; ++trial) {
        auto moves = test::randomFaceMoves(rng, 25);
        CubieCube cc;
        cc.apply(moves);
        EXPECT_EQ(cc.toFacelets(), FaceletCube().after(moves));
        auto back = CubieCube::fromFacelets(cc.toFacelets());
        ASSERT_TRUE(back.has_value());
        EXPECT_EQ(*back, cc);
    }
}

TEST(Cubie, MatchesOriginalEngineModel) {
    // Original engine order: UP LEFT FRONT RIGHT BACK DOWN, colours W G R B O Y.
    const int engineToOurs[6] = {0, 4, 2, 1, 5, 3};
    const char faceToColour[6] = {'W', 'B', 'R', 'Y', 'G', 'O'};
    std::mt19937 rng(11);
    for (int trial = 0; trial < 200; ++trial) {
        auto moves = test::randomFaceMoves(rng, 20);
        RubiksCube3dArray engine;
        for (const auto &m: moves) {
            // engine enum: L R U D F B, each cw / prime / double
            static const int engineFace[6] = {2, 1, 4, 3, 0, 5};  // ours U R F D L B -> engine index
            static const int engineTurn[4] = {0, 0, 2, 1};
            engine.move(static_cast<RubiksCube::MOVE>(engineFace[m.face()] * 3 + engineTurn[m.turns]));
        }
        FaceletCube ours = FaceletCube().after(moves);
        for (int ef = 0; ef < 6; ++ef)
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) {
                    char letter = ours[faceletIndex(engineToOurs[ef], r, c)];
                    int f = static_cast<int>(std::string("URFDLB").find(letter));
                    ASSERT_EQ(engine.cube[ef][r][c], faceToColour[f]);
                }
    }
}

TEST(Cubie, CoordinatesRoundTrip) {
    for (int v = 0; v < 2187; ++v) { CubieCube c; c.setTwist(v); EXPECT_EQ(c.twist(), v); EXPECT_EQ(c.twistSum() % 3, 0); }
    for (int v = 0; v < 2048; ++v) { CubieCube c; c.setFlip(v); EXPECT_EQ(c.flip(), v); EXPECT_EQ(c.flipSum() % 2, 0); }
    for (int v = 0; v < 495; ++v) { CubieCube c; c.setSlice(v); EXPECT_EQ(c.slice(), v); }
    for (int v = 0; v < 40320; v += 7) { CubieCube c; c.setCornerPerm(v); EXPECT_EQ(c.cornerPerm(), v); }
    for (int v = 0; v < 40320; v += 7) { CubieCube c; c.setUdEdgePerm(v); EXPECT_EQ(c.udEdgePerm(), v); }
    for (int v = 0; v < 24; ++v) { CubieCube c; c.setSliceSorted(v); EXPECT_EQ(c.sliceSorted(), v); }
    EXPECT_EQ(CubieCube().twist(), 0);
    EXPECT_EQ(CubieCube().slice(), 0);
}

TEST(Cubie, RandomStatesAreSolvable) {
    std::mt19937 rng(3);
    for (int i = 0; i < 200; ++i) {
        CubieCube c = randomCubieCube(rng);
        EXPECT_EQ(c.cornerParity(), c.edgeParity());
        EXPECT_EQ(c.twistSum() % 3, 0);
        EXPECT_EQ(c.flipSum() % 2, 0);
        EXPECT_TRUE(CubieCube::fromFacelets(c.toFacelets()).has_value());
    }
}
