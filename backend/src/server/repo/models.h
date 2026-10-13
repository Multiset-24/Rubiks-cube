#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace zc::server {

struct User {
    int64_t id = 0;
    std::string email;
    std::string displayName;
    std::string passwordHash;
    int64_t createdAtMs = 0;
};

// One attempt by a user: a tutor session, a fast/optimal solve they followed, or
// a timed solve.
struct Run {
    int64_t id = 0;
    int64_t userId = 0;
    std::string kind;    // tutor | fast | optimal | timer
    std::string method;  // solver used (beginner | kociemba | optimal), optional
    std::string facelets;
    std::string scramble;
    int moveCount = 0;
    int64_t durationMs = 0;
    bool completed = false;
    int quizCorrect = 0;
    int quizTotal = 0;
    int stepsCompleted = 0;
    int totalSteps = 0;
    std::string clientId;  // idempotency key chosen by the client (optional)
    int64_t createdAtMs = 0;
};

struct RunCursor {
    int64_t createdAtMs;
    int64_t id;
};

struct RunPage {
    std::vector<Run> runs;
    std::optional<RunCursor> next;
};

struct KindTotals {
    std::string kind;
    int64_t total = 0;
    int64_t completed = 0;
};

struct RunTotals {
    std::vector<KindTotals> byKind;
    int64_t quizCorrect = 0;
    int64_t quizTotal = 0;
    int64_t movesFollowed = 0;
    std::optional<int64_t> bestTimerMs;
};

struct LeaderboardEntry {
    std::string displayName;
    int64_t bestMs = 0;
    int64_t solves = 0;
};

}  // namespace zc::server
