#include <iostream>

#include "BfsSolver.h"
#include "DfsSolver.h"
#include "IdaStarSolver.h"
using namespace std;
#include "Model/RubiksCube3dArray.cpp"
#include "Model/RubiksCube1dArray.cpp"

int main() {
    //testing models and their functionalities:

    // RubiksCube3dArray obj1;
    // RubiksCube1dArray obj2;
    // // obj.rotateFace(2); this is inaccessible because it is private scoped
    // // cout<<(obj1==obj2)<<endl;
    // // obj1.u();
    // // cout<<obj1.isSolved();
    // // cout<<endl;
    // obj1.b();
    // obj1.b2();
    // obj1.print();
    // obj1.d();
    // obj1.f();
    // obj1.f2();
    // obj1.d2();
    // obj1.l();
    // obj1.l2();
    // obj1.r();
    // obj1.r2();
    // obj1.u();
    // obj1.fPrime();
    // obj2.l2();
    // obj2.r();
    // obj2.print();
    // obj2.bPrime();
    // obj2.f();
    // obj2.uPrime();


    // RubiksCube3dArray cube;
    // cube.print();
    // vector<RubiksCube::MOVE> shuffle_moves = cube.randomShuffleCube(6);
    // for (auto move: shuffle_moves) cout << cube.getMove(move) << " ";
    // cout << "\n";
    // cube.print();
    //testing for dfs solver

    // DFSSolver<RubiksCube3dArray, Hash3d> dfsSolver(cube, 8);
    // vector<RubiksCube::MOVE> solve_moves = dfsSolver.solve();
    // cout<<"Steps to solve cube-> ";
    // for (auto move: solve_moves) cout << cube.getMove(move) << " ";
    // cout << "\n";
    // dfsSolver.rubiksCube.print();

    // testing for bfs solver
 char customCube[6][3][3] = {
  // UP face (0)
  {{'W','W','O'},
   {'W','W','W'},
   {'B','B','W'}},
  // LEFT face (1)
  {{'Y','B','B'},
   {'O','O','O'},
   {'O','O','O'}},
  // FRONT face (2)
  {{'O','O','G'},
   {'G','G','G'},
   {'G','G','G'}},
  // RIGHT face (3)
  {{'W','R','R'},
   {'G','R','R'},
   {'G','R','R'}},
  // BACK face (4)
  {{'W','R','R'},
   {'W','B','B'},
   {'B','B','B'}},
  // DOWN face (5)
  {{'Y','Y','R'},
   {'Y','Y','Y'},
   {'Y','Y','Y'}}
 };


    RubiksCube3dArray cube;
    vector<RubiksCube3dArray::MOVE> moves=cube.randomShuffleCube(8);
    cube.print();
    RubiksCube3dArray cube2=cube;
    DFSSolver<RubiksCube3dArray,Hash3d>dfssolver(cube2);
    vector<RubiksCube::MOVE>solve_moves=dfssolver.solve();
    for (auto move:solve_moves)cout<<cube2.getMove(move)<<" ";
    cube2.print();
    cout<<"\n";
    return 0;
}
