//
// Created by Saurav on 24-06-2025.
//

#ifndef RUBIKS_CUBE_SOLVER_CORNERDBMAKER_H
#define RUBIKS_CUBE_SOLVER_CORNERDBMAKER_H
#include "CornerPatternDatabase.h"
#include "../Model/RubiksCubeBitboard.h"
using namespace std;

class CornerDBMaker {
private:
    string fileName;
    CornerPatternDatabase cornerDB;

public:
    CornerDBMaker(string _fileName);
    CornerDBMaker(string _fileName, uint8_t init_val);

    // Breadth-first search from the solved cube, recording the depth at which each
    // corner configuration is first reached, then writes the table to fileName.
    //
    // The full corner space is 11 moves deep (88,179,840 states). Holding every
    // frontier cube in a std::queue does not fit in memory past depth ~8, so the
    // search is depth-limited and every unreached entry is set to maxDepth + 1:
    // still a valid lower bound, so IDA* stays optimal. The backend ships a
    // coordinate-based builder that computes the full table in seconds.
    bool bfsAndStore(int maxDepth = 8);

    const CornerPatternDatabase &database() const { return cornerDB; }
};


#endif //RUBIKS_CUBE_SOLVER_CORNERDBMAKER_H
