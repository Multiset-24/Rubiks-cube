# Algorithms

## Cube representations

There are three representations, each used where it fits.

**Facelets** (`backend/src/core/facelet.cpp`, `frontend/src/lib/cube/cube.ts`). 54 stickers, which is the format the camera produces and the API accepts. A move is a permutation of the 54 positions. The permutations are not typed in by hand; they are generated from geometry:

- Each sticker gets a 3D position (the cubie it sits on, coordinates in {-1, 0, 1}) and an outward normal.
- A clockwise quarter turn of a face is a -90° rotation about that face's normal, applied to every sticker in the layer: `v' = -(a × v) + a(a · v)`.
- The rotated sticker is looked up by its new position and normal, which gives the permutation.

The same code gives whole-cube rotations (`x`, `y`, `z`) for free. Both the C++ and the TypeScript versions use this method and are tested against the same known states, so the server and the 3D view can't disagree.

Generating rather than typing matters. The original engine's bitboard model had a hand-written `R` move that swapped two stickers on the back face. Every single-move test passed because those stickers share a colour on a solved cube (see [ENGINE_FIXES.md](ENGINE_FIXES.md)).

**Cubies** (`backend/src/core/cubie.cpp`). Which corner and edge sits in each slot, and how it is twisted or flipped. This is the model Kociemba's algorithm and the validator use. Converting from facelets also checks that each slot holds a real piece.

**The original models** (`engine/Model`): a 3D array, a 1D array and a bitboard. They are used by the engine's own solvers and by the Optimal mode.

## Validation

A sticker pattern is a reachable cube only if all of the following hold:

1. There are nine stickers of each colour and the six centres are different.
2. Every corner and edge slot holds a real piece, each piece appears once, and no corner's colours are in mirror order.
3. The corner twists sum to 0 mod 3.
4. The edge flips sum to 0 mod 2.
5. The corner permutation parity equals the edge permutation parity.

Checks 3–5 are the classic invariants: no sequence of turns changes them. A scan error usually breaks check 1 or 2. A physically tampered cube breaks 3, 4 or 5. Each failure maps to a message and a set of stickers to highlight (`validation.cpp`).

## Beginner tutor (layer by layer)

`backend/src/core/beginner.cpp`. The cube is held with white on the bottom and yellow on top. The nine stages are:

| Stage | Goal | Recognition | Algorithm |
| --- | --- | --- | --- |
| Hold the cube | white down, yellow up | find the two centres | whole-cube rotation |
| Daisy | 4 white edges around the yellow centre | where is each white edge: top (flipped?), middle, bottom | short searched sequence |
| White cross | petals down onto matching centres | side colour of each petal | U turns, then that face twice |
| First-layer corners | white face + first row | where the white sticker points: right / up / towards you | `R U R' U'` × 1 / 3 / 5 |
| Second layer | middle edges | upside-down T, then the top colour picks left or right | `U R U' R' U' F' U F` / mirror |
| Yellow cross | edges only | dot, L or line | `F R U R' U' F'` |
| Yellow edges | match side centres | matched pair adjacent or opposite | `R U R' U R U2 R' U` |
| Position yellow corners | right spot, any twist | none / one / all in place | `U R U' L' U R' U' L` |
| Twist yellow corners | yellow up | yellow points right or towards you | `R' D' R D` × 2 / 4 |

How it's implemented:

- **Rule-driven, then checked.** Each stage follows the human procedure: find the piece, apply the setup turns, apply the algorithm. Every step is applied to a simulated cube, and the stage's invariant is asserted afterwards (for example "the cross and all solved corners are still solved"). If a rule ever produced a wrong step, the solver would throw instead of returning a broken guide. 400 random cubes in the tests, plus 500 in the benchmark, all solve.
- **Counts come from simulation.** Answers like "3 times" are found by trying, not looked up in a table. The step text and the quiz answers come from the same simulation, so they can't contradict the moves.
- **The daisy uses a small search.** Finding the shortest sequence that adds one petal without knocking any off (iterative deepening, depth ≤ 6, same-face and opposite-face pruning) gives sensible moves for every case. The text then describes the case: flipped on top, middle layer, bottom facing down or sideways.
- **Colour names** come from the request. If the cube was scanned with white on a different face, the tutor still finds it and orients the cube first.

The result is about 160 face turns (mean 163, max 217 over 500 random cubes) and takes about 0.25 ms to compute.

## Kociemba two-phase

`backend/src/core/kociemba.cpp`. The idea is to split the cube group with the subgroup G1 = ⟨U, D, R2, L2, F2, B2⟩:

- **Phase 1** gets the cube into G1: every corner twist and edge flip is zero, and the four middle-layer edges are in the middle layer. The state is three coordinates: twist (2187), flip (2048) and slice (495).
- **Phase 2** solves inside G1 using only G1 moves. The coordinates are corner permutation (40320), U/D-edge permutation (40320) and slice permutation (24).

Each phase is an IDA\* search over its coordinates.

- **Move tables** map (coordinate, move) to a new coordinate, so a node costs a few array lookups instead of cube manipulation.
- **Pruning tables** give the exact number of moves needed to solve a pair of coordinates, for example (twist, slice), computed once by breadth-first search. The heuristic is the max of two such tables per phase.

Total table size is 6.5 MB and they build in about 0.25 s at startup.

The search asks for 21 moves or fewer first, and relaxes to 23 and then 26 if needed. Over 500 random cubes: mean 20.8 moves, max 21, median 3.4 ms, p95 64 ms.

## IDA\* with a corner pattern database (Optimal)

This is the original engine (`engine/Solvers/IdaStarSolver.h`), after the fixes:

- **The search.** Depth-first, bounded by f = g + h, where g is moves so far and h is a lower bound on moves left. Each iteration raises the bound to the smallest f that exceeded it. Memory is O(depth). Two pruning rules cut the branching factor from 18 to about 13.35: never turn the same face twice in a row, and turn opposite faces in a fixed order.
- **The heuristic** is Korf's corner pattern database: for each of the 8! × 3^7 = 88,179,840 corner configurations, the exact number of moves to solve the corners. That is a lower bound for the whole cube, so IDA\* returns optimal solutions. Each entry is a 4-bit nibble, so 44 MB.

**Building the database.** The engine's own builder (`CornerDBMaker`) does a BFS that stores whole cube objects in a queue. It runs out of memory past depth 8, so its output is capped (with unreached entries set to depth + 1, still a valid lower bound).

The backend builds the full table differently (`buildCornerDatabase` in `core/optimal.cpp`):

1. BFS over integer coordinates (corner permutation × twist) using the Kociemba move tables, with a 4-bit distance array (44 MB) instead of a queue.
2. Re-index into the engine's own layout. That index factors into a part that depends only on the permutation and a part that depends only on the twist, so the conversion is two small lookup tables.

This takes about 9 s. The result is 88,179,840 states, 11 moves deep. A test compares it with the engine's BFS on every single entry.

Performance with the full table (median time over 20 scrambles):

| Scramble length | Nodes (median) | Time (median) | Time (max) |
| --- | --- | --- | --- |
| 6 | 47 | 0.6 ms | 7 ms |
| 7 | 101 | 1.0 ms | 80 ms |
| 8 | 1,177 | 10.7 ms | 533 ms |
| 9 | 5,296 | 48 ms | 274 ms (n=5) |

The cost grows exponentially with depth, so the service gives this search a 3 s budget and falls back to Kociemba for deeper cubes.

## Colour classification

`frontend/src/lib/vision`.

1. **Sampling.** The camera frame is drawn to a canvas and a centred guide square is split into 3×3 cells. Each cell's colour is the per-channel median of its central area. That ignores glare spots, logos and the black plastic between stickers.
2. **Colour space.** RGB is converted to CIELAB, where lightness is separate from hue. The distance metric down-weights lightness (by 0.5), because uneven lighting mostly changes brightness, while red vs orange and white vs yellow differ in the a/b channels.
3. **Matching against the centres.** After all six faces are captured, each of the 48 non-centre stickers is assigned to one of the six scanned centres. The centres saw the same camera and light, which is far more reliable than fixed reference colours.
4. **Global assignment.** The assignment is solved with the Hungarian algorithm on a 48×48 cost matrix (8 slots per colour), so every colour gets exactly nine stickers. A per-sticker nearest-colour rule can label ten stickers red and eight orange; the global assignment can't.
5. **Confidence.** The gap between each sticker's best and second-best match is kept. Low-confidence stickers are outlined on the review screen.

The scan protocol uses centre colours, not left/right ("red centre facing the camera, white on top"). That makes it work the same with a phone's rear camera and with a mirrored laptop webcam. Samples are taken from the raw, unmirrored frame, which shows each face the way the facelet format expects.
