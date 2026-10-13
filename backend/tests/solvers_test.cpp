#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <random>

#include "Model/RubiksCubeBitboard.h"
#include "Pattern_Database/CornerDBMaker.h"
#include "Pattern_Database/CornerPatternDatabase.h"
#include "core/beginner.h"
#include "core/kociemba.h"
#include "core/optimal.h"
#include "core/validation.h"
#include "test_util.h"

using namespace zc;

namespace {

const KociembaTables &tables() {
    static auto t = KociembaTables::build();
    return *t;
}

}  // namespace

TEST(Kociemba, SolvesRandomStates) {
    std::mt19937 rng(42);
    int longest = 0;
    for (int i = 0; i < 300; ++i) {
        CubieCube c = randomCubieCube(rng);
        auto r = solveKociemba(tables(), c, 23, SearchBudget::withTimeout(std::chrono::seconds(10)));
        ASSERT_TRUE(r.found) << c.toFacelets().str();
        FaceletCube f = c.toFacelets();
        f.apply(r.moves);
        ASSERT_TRUE(f.isSolved());
        longest = std::max(longest, static_cast<int>(r.moves.size()));
    }
    EXPECT_LE(longest, 23);
}

TEST(Kociemba, SolvedCubeNeedsNoMoves) {
    auto r = solveKociemba(tables(), CubieCube{}, 23, SearchBudget{});
    EXPECT_TRUE(r.found);
    EXPECT_TRUE(r.moves.empty());
}

TEST(Kociemba, RespectsBudget) {
    std::mt19937 rng(1);
    CubieCube c = randomCubieCube(rng);
    SearchBudget b;
    b.maxNodes = 10;
    auto r = solveKociemba(tables(), c, 12, b);  // too short to succeed quickly
    EXPECT_FALSE(r.found);
}

TEST(Beginner, SolvesRandomStatesAndExplainsEverySteps) {
    std::mt19937 rng(5);
    for (int i = 0; i < 400; ++i) {
        FaceletCube start = randomCubieCube(rng).toFacelets();
        auto sol = solveBeginner(start, defaultColorNames(), SearchBudget::withTimeout(std::chrono::seconds(10)));
        FaceletCube c = start;
        std::vector<Move> all;
        for (const auto &stage: sol.stages) {
            ASSERT_FALSE(stage.id.empty());
            for (const auto &step: stage.steps) {
                EXPECT_FALSE(step.title.empty());
                EXPECT_FALSE(step.observe.empty());
                EXPECT_FALSE(step.instruction.empty());
                all.insert(all.end(), step.moves.begin(), step.moves.end());
                if (step.quiz) {
                    ASSERT_GE(step.quiz->answer, 0);
                    ASSERT_LT(step.quiz->answer, static_cast<int>(step.quiz->options.size()));
                }
            }
        }
        ASSERT_EQ(all.size(), sol.moves.size());
        c.apply(all);
        ASSERT_TRUE(c.isSolved()) << start.str();
    }
}

TEST(Beginner, HandlesSolvedAndNearlySolvedCubes) {
    for (const char *alg: {"", "U", "R U R' U'", "x", "y2 R2", "F R U R' U' F'"}) {
        FaceletCube start = FaceletCube().after(parseMoves(alg));
        auto sol = solveBeginner(start, defaultColorNames(), SearchBudget{});
        EXPECT_TRUE(start.after(sol.moves).isSolved()) << alg;
    }
}

TEST(Beginner, UsesStageOrderOfTheMethod) {
    std::mt19937 rng(9);
    auto sol = solveBeginner(randomCubieCube(rng).toFacelets(), defaultColorNames(), SearchBudget{});
    std::vector<std::string> ids;
    for (const auto &s: sol.stages) ids.push_back(s.id);
    std::vector<std::string> expected = {"hold", "daisy", "white-cross", "white-corners", "middle-layer",
                                         "yellow-cross", "yellow-edges", "yellow-corners-position",
                                         "yellow-corners-orient"};
    EXPECT_EQ(ids, expected);
}

TEST(Beginner, WorksWhenWhiteIsScannedOnAnotherFace) {
    // A cube scanned with white on the front: names say face F is white.
    ColorNames names = {"green", "red", "white", "blue", "orange", "yellow"};
    std::mt19937 rng(13);
    for (int i = 0; i < 20; ++i) {
        FaceletCube start = randomCubieCube(rng).toFacelets();
        auto sol = solveBeginner(start, names, SearchBudget{});
        EXPECT_TRUE(start.after(sol.moves).isSolved());
        EXPECT_NE(sol.stages[1].goal.find("white"), std::string::npos);
    }
}

TEST(Beginner, HoldStepHighlightsTheCentresItNames) {
    // White scanned on the front, blue on top: the hold step needs a rotation
    // other than z2, and its highlight must still land on top, bottom and front.
    ColorNames names = {"blue", "red", "white", "green", "orange", "yellow"};
    std::mt19937 rng(17);
    FaceletCube start = randomCubieCube(rng).toFacelets();
    auto sol = solveBeginner(start, names, SearchBudget{});
    const auto &hold = sol.stages.at(0).steps.at(0);
    FaceletCube marked = FaceletCube::fromString(std::string(54, '.'));
    std::string s = marked.str();
    for (int f: hold.focus) s[static_cast<size_t>(f)] = '#';
    FaceletCube after = FaceletCube::fromString(s).after(hold.setup);
    for (int centre: {4, 31, 22}) EXPECT_EQ(after[centre], '#') << centre;  // U, D and F centres
}

TEST(CornerDatabase, FastIndexMatchesEngineIndex) {
    CornerPatternDatabase engineDb(0);
    std::mt19937 rng(21);
    for (int i = 0; i < 500; ++i) {
        CubieCube c = randomCubieCube(rng);
        FaceletCube f = c.toFacelets();
        // Build the engine bitboard via the same mapping the adapter uses.
        const int engineToOurs[6] = {0, 4, 2, 1, 5, 3};
        const char faceToColour[6] = {'W', 'B', 'R', 'Y', 'G', 'O'};
        char grid[6][3][3];
        for (int ef = 0; ef < 6; ++ef)
            for (int r = 0; r < 3; ++r)
                for (int col = 0; col < 3; ++col) {
                    char letter = f[faceletIndex(engineToOurs[ef], r, col)];
                    grid[ef][r][col] = faceToColour[std::string("URFDLB").find(letter)];
                }
        RubiksCubeBitboard bb(grid);
        ASSERT_EQ(engineCornerIndex(c), engineDb.getDatabaseIndex(bb));
    }
}

TEST(CornerDatabase, FullBuildAgreesWithEngineBfsAndSolvesOptimally) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "zencube_test_db";
    fs::create_directories(dir);
    std::string fast = (dir / "corner_full.db").string();
    std::string slow = (dir / "corner_d5.db").string();

    auto stats = buildCornerDatabase(tables(), fast);
    EXPECT_EQ(stats.states, 88179840u);
    EXPECT_EQ(stats.maxDepth, 11);

    // The engine's own (depth-limited) BFS must agree on every state:
    // exact up to depth 5, and "6" (= at least 6) beyond.
    CornerDBMaker maker(slow, 0xFF);
    maker.bfsAndStore(5);
    auto full = OptimalSolver::loadDatabase(fast);
    ASSERT_TRUE(full);
    const auto &limited = maker.database();
    for (uint32_t i = 0; i < CornerPatternDatabase::kSize; ++i) {
        uint8_t a = full->getNumMoves(i), b = limited.getNumMoves(i);
        ASSERT_EQ(std::min<uint8_t>(a, 6), b) << "index " << i;
    }

    OptimalSolver solver(full);
    std::mt19937 rng(77);
    for (int i = 0; i < 20; ++i) {
        auto scramble = test::randomFaceMoves(rng, 7);
        FaceletCube c = FaceletCube().after(scramble);
        auto r = solver.solve(c, SearchBudget::withTimeout(std::chrono::seconds(20)));
        ASSERT_TRUE(r.found);
        EXPECT_LE(r.moves.size(), scramble.size());  // optimal is never longer than the scramble
        EXPECT_TRUE(c.after(r.moves).isSolved());
    }
    fs::remove_all(dir);
}
