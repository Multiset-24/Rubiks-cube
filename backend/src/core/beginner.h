#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/budget.h"
#include "core/facelet.h"
#include "core/moves.h"
#include "core/validation.h"

namespace zc {

// A multiple-choice question asked before revealing a step, so the learner
// practises recognising the pattern instead of just copying moves.
struct QuizItem {
    std::string question;
    std::vector<std::string> options;
    int answer = 0;
    std::string explanation;
};

struct AlgorithmRef {
    std::string name;
    std::string notation;
    int repeat = 1;
};

struct TutorStep {
    std::string title;
    std::string observe;      // what to look for (pattern recognition)
    std::string instruction;  // what to do, in words
    std::string tip;
    std::vector<Move> setup;  // positioning turns / whole-cube rotations
    std::optional<AlgorithmRef> algorithm;
    std::vector<Move> moves;  // setup + algorithm x repeat: exactly what to play
    std::vector<int> focus;   // stickers of the piece this step is about, at its start
    std::optional<QuizItem> quiz;
};

struct TutorStage {
    std::string id;
    std::string name;
    std::string goal;
    std::string recognize;  // how to read the cube for this stage
    std::vector<AlgorithmRef> algorithms;
    std::vector<TutorStep> steps;
    std::string skippedNote;  // set when the stage was already complete
};

struct TutorSolution {
    std::vector<TutorStage> stages;
    std::vector<Move> moves;  // every step's moves, concatenated
    FaceletCube start;
};

// Layer-by-layer beginner method (daisy, white cross, first-layer corners,
// second layer, yellow cross, yellow edges, yellow corners). Every step is
// simulated and checked, so the returned moves always solve the cube.
// `cube` must be valid (see validateCube). Throws std::runtime_error if the
// budget runs out.
TutorSolution solveBeginner(const FaceletCube &cube, const ColorNames &names, const SearchBudget &budget);

}  // namespace zc
