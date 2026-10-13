//
// Created by Saurav on 24-06-2025.
//

#include "CornerDBMaker.h"

#include <queue>

using namespace std;

CornerDBMaker::CornerDBMaker(string _fileName) {
    fileName = _fileName;
}

CornerDBMaker::CornerDBMaker(string _fileName, uint8_t init_val) {
    fileName = _fileName;
    cornerDB = CornerPatternDatabase(init_val);
}

bool CornerDBMaker::bfsAndStore(int maxDepth) {
    RubiksCubeBitboard cube;
    queue<RubiksCubeBitboard> q;
    q.push(cube);
    cornerDB.setNumMoves(cube, 0);
    int curr_depth = 0;
    while (!q.empty() && curr_depth < maxDepth) {
        size_t n = q.size();
        curr_depth++;
        for (size_t counter = 0; counter < n; counter++) {
            RubiksCubeBitboard node = q.front();
            q.pop();
            for (int i = 0; i < 18; i++) {
                auto curr_move = RubiksCube::MOVE(i);
                node.move(curr_move);
                if ((int) cornerDB.getNumMoves(node) > curr_depth) {
                    cornerDB.setNumMoves(node, curr_depth);
                    // The last level is recorded but never expanded, so it does not
                    // need to sit in the queue (it is the largest level by far).
                    if (curr_depth < maxDepth) q.push(node);
                }
                node.invert(curr_move);
            }
        }
    }

    // Previously unreached entries kept the 0xF initial value, i.e. a heuristic of
    // 15 moves for states that are at most 11 away: an over-estimate that made
    // IDA* return non-optimal solutions.
    cornerDB.fillUnset(static_cast<uint8_t>(curr_depth + 1));

    cornerDB.toFile(fileName);
    return true;
}
