# Fixes to the original engine

The solver in `engine/` was written first, as a learning project. Before building the service on top of it, I cross-checked its three cube models against each other on random move sequences, and against a cube model generated from geometry. That turned up the problems below. Each one has a test in `backend/tests` now.

| # | Problem | Effect | Fix |
| --- | --- | --- | --- |
| 1 | Bitboard `R` move copied the back face's left column in the wrong order (indices 7, 6, 0 instead of 6, 7, 0) when writing to D and B | Two stickers swapped on every `R`. Invisible on a solved cube, because the column is one colour, so all single-move tests passed. On real states it created impossible corner arrangements | Corrected the indices; models now agree on 200 random 20-move sequences (`Cubie.MatchesOriginalEngineModel`) |
| 2 | Corner pattern database sized 100,179,840 | The real count is 8! × 3^7 = 88,179,840. The extra 12M entries had been added because bug #1 produced out-of-range indices | Exact size; the index is now always in range |
| 3 | `CornerDBMaker` stopped at depth 9 and left every other entry at 0xF (15) | A heuristic of 15 for states at most 11 moves away over-estimates, so IDA\* was no longer guaranteed optimal | Unreached entries are set to depth + 1 (a valid lower bound); the last level isn't queued, which saves most of the memory |
| 4 | `IDAstarSolver` was a best-first search with a priority queue plus `visited` and `move_done` hash maps | Memory grew with every generated state (it was A\* with a bound, not IDA\*). Without a database every state scored 15, and a 6-move scramble ran over 60 s | True depth-first IDA\*: O(depth) memory, same-face and opposite-face pruning, time/node budget, a database that can be shared between solves, h = 0 when no database is loaded |
| 5 | `RubiksCube1dArray::isSolved` compared each row with its own first sticker | A face with three differently coloured rows counted as solved | Compare every sticker with the face centre |
| 6 | Bitboard constructor from a colour array: `solved_side_config[side] = solved_side_config[side]` | The solved state was never set, so `isSolved()` was always false for custom cubes | The solved state is now a `constexpr` function |
| 7 | Same constructor shifted by `8 * 8 = 64` bits for the centre sticker | Undefined behaviour for a 64-bit integer | Centres are skipped (they are implicit in this model) |
| 8 | Model classes lived in `.cpp` files that were `#include`d, without include guards | Including two of them in one translation unit failed to compile, so they couldn't be reused | Renamed to guarded headers |
| 9 | `#include <bits/stdc++.h>` | GCC-only; the README promised Clang and MSVC support | Standard headers |
| 10 | A project header named `math.h` on the include path | Shadowed the C library's `<math.h>`, which `<cmath>` includes | Renamed to `cube_math.h` |
| 11 | Pattern database read without `std::ios::binary` | Corrupts the data on Windows | Binary mode |
| 12 | `randomShuffleCube` called `srand(time(0))` on every call | Two shuffles in the same second were identical | One `std::mt19937`, seeded once |
| 13 | Missing `return` at the end of several `switch` functions | Undefined behaviour if reached; compiler warnings | Explicit fallback returns |

Also:

- The bitboard model's constant members (`arr`, masks, solved state) are now `static constexpr`, which shrinks each cube from about 160 to 56 bytes. That matters when a BFS keeps millions of them in a queue.
- The committed `cmake-build-debug/` directory, which contained Windows binaries, was removed.

Results with the fixed engine:

- A 7-move scramble now solves optimally in about 35 ms, using a depth-6 database.
- With the full database (built by the backend's coordinate BFS in about 9 s), 8-move scrambles take a median of 11 ms.
- The fast builder's output matches the engine's own BFS on all 88,179,840 entries.
