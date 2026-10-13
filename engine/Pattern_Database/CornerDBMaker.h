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

    // BFS from the solved cube up to maxDepth, then writes the table to fileName.
    // Entries not reached are set to maxDepth + 1, which is still a lower bound.
    // (The full table goes to depth 11; keeping cube objects in a queue runs out
    // of memory well before that. backend/ has a faster builder for the full table.)
    bool bfsAndStore(int maxDepth = 8);

    const CornerPatternDatabase &database() const { return cornerDB; }
};


#endif //RUBIKS_CUBE_SOLVER_CORNERDBMAKER_H
