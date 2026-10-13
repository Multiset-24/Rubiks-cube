//
// Command-line demo for the solver engine.
//
//   rubiks_cli                                   scramble 6 moves, solve with IDA*
//   rubiks_cli --solver bfs|dfs|iddfs|ida --scramble N --model bitboard|1d|3d
//   rubiks_cli --db DataBase/cornerDepth.db      use a corner pattern database
//   rubiks_cli --build-db DataBase/cornerDepth.db [--depth 8]
//
// The HTTP backend (../backend) builds the full 11-move-deep database much faster
// with `zen_cube_server --build-tables`.
//

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

#include "Model/RubiksCube1dArray.h"
#include "Model/RubiksCube3dArray.h"
#include "Model/RubiksCubeBitboard.h"
#include "Pattern_Database/CornerDBMaker.h"
#include "Solvers/BfsSolver.h"
#include "Solvers/DfsSolver.h"
#include "Solvers/IdaStarSolver.h"
#include "Solvers/IddfsSolver.h"

using namespace std;

namespace {

struct Options {
    string solver = "ida";
    string model = "bitboard";
    string db;
    string buildDb;
    int depth = 8;
    unsigned scramble = 6;
};

void usage() {
    cout << "usage: rubiks_cli [--solver bfs|dfs|iddfs|ida] [--model bitboard|1d|3d]\n"
            "                  [--scramble N] [--db FILE]\n"
            "       rubiks_cli --build-db FILE [--depth D]\n";
}

template<typename T, typename H>
int run(const Options &opt) {
    T cube;
    vector<RubiksCube::MOVE> shuffle = cube.randomShuffleCube(opt.scramble);
    cout << "Scramble: ";
    for (auto m: shuffle) cout << RubiksCube::getMove(m) << " ";
    cout << "\n";
    cube.print();

    auto start = chrono::steady_clock::now();
    vector<RubiksCube::MOVE> solution;
    T solved;
    if (opt.solver == "bfs") {
        BFSSolver<T, H> s(cube);
        solution = s.solve();
        solved = s.rubiksCube;
    } else if (opt.solver == "dfs") {
        DFSSolver<T, H> s(cube, (int) opt.scramble);
        solution = s.solve();
        solved = s.rubiksCube;
    } else if (opt.solver == "iddfs") {
        IDDFSSolver<T, H> s(cube, (int) opt.scramble);
        solution = s.solve();
        solved = s.rubiksCube;
    } else {
        IDAstarSolver<T, H> s(cube, opt.db);
        solution = s.solve();
        solved = s.rubiksCube;
        cout << "Nodes expanded: " << s.getStats().nodesExpanded << "\n";
    }
    auto ms = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start).count();

    cout << "Solution (" << solution.size() << " moves, " << ms << " ms): ";
    for (auto m: solution) cout << RubiksCube::getMove(m) << " ";
    cout << "\n";
    solved.print();
    cout << (solved.isSolved() ? "SOLVED\n" : "NOT SOLVED\n");
    return solved.isSolved() ? 0 : 1;
}

} // namespace

int main(int argc, char **argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        string a = argv[i];
        auto next = [&]() -> string {
            if (i + 1 >= argc) { usage(); exit(2); }
            return argv[++i];
        };
        if (a == "--solver") opt.solver = next();
        else if (a == "--model") opt.model = next();
        else if (a == "--db") opt.db = next();
        else if (a == "--build-db") opt.buildDb = next();
        else if (a == "--depth") opt.depth = stoi(next());
        else if (a == "--scramble") opt.scramble = (unsigned) stoi(next());
        else { usage(); return a == "--help" || a == "-h" ? 0 : 2; }
    }

    if (!opt.buildDb.empty()) {
        cout << "Building corner pattern database to depth " << opt.depth << " ...\n";
        CornerDBMaker maker(opt.buildDb, 0xFF);
        maker.bfsAndStore(opt.depth);
        cout << "Wrote " << opt.buildDb << "\n";
        return 0;
    }

    if (opt.model == "1d") return run<RubiksCube1dArray, Hash1d>(opt);
    if (opt.model == "3d") return run<RubiksCube3dArray, Hash3d>(opt);
    return run<RubiksCubeBitboard, HashBitboard>(opt);
}
