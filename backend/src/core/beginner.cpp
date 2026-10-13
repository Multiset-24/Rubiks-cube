#include "core/beginner.h"

#include <algorithm>
#include <array>
#include <functional>
#include <set>
#include <stdexcept>

namespace zc {

namespace {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

int faceOfLetter(char c) {
    for (int f = 0; f < kNumFaces; ++f) if (kFaceChars[f] == c) return f;
    return -1;
}

int faceOfSticker(int index) { return index / 9; }

std::vector<Move> turns(Axis axis, int k) {
    k = ((k % 4) + 4) % 4;
    if (k == 0) return {};
    return {Move{axis, static_cast<uint8_t>(k)}};
}

std::vector<Move> concat(std::vector<Move> a, const std::vector<Move> &b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

std::string capitalize(std::string s) {
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

const char *kFaceWord[kNumFaces] = {"top", "right", "front", "bottom", "left", "back"};

const std::vector<Move> kSexy = parseMoves("R U R' U'");
const std::vector<Move> kRightInsert = parseMoves("U R U' R' U' F' U F");
const std::vector<Move> kLeftInsert = parseMoves("U' L' U L U F U' F'");
const std::vector<Move> kYellowCross = parseMoves("F R U R' U' F'");
const std::vector<Move> kSuneU = parseMoves("R U R' U R U2 R' U");
const std::vector<Move> kCornerCycle = parseMoves("U R U' L' U R' U' L");
const std::vector<Move> kCornerTwist = parseMoves("R' D' R D");

const AlgorithmRef kSexyRef{"Sexy move", "R U R' U'", 1};
const AlgorithmRef kRightRef{"Right insert", "U R U' R' U' F' U F", 1};
const AlgorithmRef kLeftRef{"Left insert", "U' L' U L U F U' F'", 1};
const AlgorithmRef kYellowCrossRef{"Yellow cross", "F R U R' U' F'", 1};
const AlgorithmRef kSuneRef{"Sune (edge swap)", "R U R' U R U2 R' U", 1};
const AlgorithmRef kCornerCycleRef{"Corner cycle", "U R U' L' U R' U' L", 1};
const AlgorithmRef kCornerTwistRef{"Corner twist", "R' D' R D", 1};

constexpr int kULayerEdges[4] = {UR, UF, UL, UB};
constexpr int kDLayerEdges[4] = {DR, DF, DL, DB};
constexpr int kMiddleEdges[4] = {FR, FL, BL, BR};
constexpr int kULayerCorners[4] = {URF, UFL, ULB, UBR};
constexpr int kDLayerCorners[4] = {DFR, DLF, DBL, DRB};

bool isULayerEdge(int p) { return p <= UB; }
bool isDLayerEdge(int p) { return p >= DR && p <= DB; }
bool isULayerCorner(int p) { return p <= UBR; }

// The corner position directly above / below another.
int cornerAbove(int dPos) {
    switch (dPos) {
        case DFR: return URF;
        case DLF: return UFL;
        case DBL: return ULB;
        default: return UBR;  // DRB
    }
}

bool edgeSolved(const FaceletCube &c, int pos) {
    for (int k = 0; k < 2; ++k) {
        int f = kEdgeFacelets[static_cast<size_t>(pos)][static_cast<size_t>(k)];
        if (c[f] != c.center(faceOfSticker(f))) return false;
    }
    return true;
}

bool cornerSolved(const FaceletCube &c, int pos) {
    for (int k = 0; k < 3; ++k) {
        int f = kCornerFacelets[static_cast<size_t>(pos)][static_cast<size_t>(k)];
        if (c[f] != c.center(faceOfSticker(f))) return false;
    }
    return true;
}

// A corner is "in place" when its colours match the three centres around it,
// whatever its twist.
bool cornerInPlace(const FaceletCube &c, int pos) {
    std::multiset<char> piece, centres;
    for (int k = 0; k < 3; ++k) {
        int f = kCornerFacelets[static_cast<size_t>(pos)][static_cast<size_t>(k)];
        piece.insert(c[f]);
        centres.insert(c.center(faceOfSticker(f)));
    }
    return piece == centres;
}

struct EdgeLoc {
    int pos = -1;
    int facelet[2] = {-1, -1};  // facelet holding colour a, colour b
};

EdgeLoc findEdge(const FaceletCube &c, char a, char b) {
    for (int p = 0; p < 12; ++p) {
        int f0 = kEdgeFacelets[static_cast<size_t>(p)][0], f1 = kEdgeFacelets[static_cast<size_t>(p)][1];
        if (c[f0] == a && c[f1] == b) return EdgeLoc{p, {f0, f1}};
        if (c[f0] == b && c[f1] == a) return EdgeLoc{p, {f1, f0}};
    }
    throw std::runtime_error("edge not found");
}

struct CornerLoc {
    int pos = -1;
    int facelet[3] = {-1, -1, -1};  // facelet holding colour a, b, c
};

CornerLoc findCorner(const FaceletCube &c, char a, char b, char d) {
    for (int p = 0; p < 8; ++p) {
        CornerLoc loc;
        loc.pos = p;
        int found = 0;
        for (int k = 0; k < 3; ++k) {
            int f = kCornerFacelets[static_cast<size_t>(p)][static_cast<size_t>(k)];
            if (c[f] == a) { loc.facelet[0] = f; ++found; }
            else if (c[f] == b) { loc.facelet[1] = f; ++found; }
            else if (c[f] == d) { loc.facelet[2] = f; ++found; }
        }
        if (found == 3) return loc;
    }
    throw std::runtime_error("corner not found");
}

// Smallest k in 0..3 such that pred(cube after `axis`^k) holds, or -1.
int findTurns(const FaceletCube &c, Axis axis, const std::function<bool(const FaceletCube &)> &pred) {
    for (int k: {0, 1, 3, 2}) {  // prefer fewer quarter turns
        if (pred(c.after(turns(axis, k)))) return k;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Tutor
// ---------------------------------------------------------------------------

class BeginnerTutor {
public:
    BeginnerTutor(const FaceletCube &cube, const ColorNames &names, const SearchBudget &budget)
            : cube_(cube), names_(names), budget_(budget) {
        solution_.start = cube;
        int whiteFace = -1;
        for (int f = 0; f < kNumFaces; ++f) if (names_[static_cast<size_t>(f)] == "white") whiteFace = f;
        // Colours are identified by the letter of the face they were scanned on.
        white_ = whiteFace >= 0 ? kFaceChars[whiteFace] : cube_.center(3);
        yellow_ = oppositeOf(white_);
    }

    TutorSolution run() {
        stageHold();
        if (!crossSolved()) {
            stageDaisy();
            stageCross();
        } else {
            skipStage("daisy", "The daisy", "Your white cross is already solved, so there is no need for a daisy.");
            skipStage("white-cross", "White cross", "Your white cross is already solved.");
        }
        stageWhiteCorners();
        stageMiddleLayer();
        stageYellowCross();
        stageYellowEdges();
        stagePositionCorners();
        stageOrientCorners();
        if (!cube_.isSolved()) throw std::runtime_error("tutor finished without solving the cube");
        return std::move(solution_);
    }

private:
    // ---- colour words --------------------------------------------------------

    std::string name(char letter) const {
        int f = faceOfLetter(letter);
        return f >= 0 ? names_[static_cast<size_t>(f)] : std::string("?");
    }

    char oppositeOf(char letter) const {
        // Opposite centres never move relative to each other, so look it up on
        // the scanned cube: U-D, R-L, F-B.
        static const int opp[6] = {3, 4, 5, 0, 1, 2};
        for (int f = 0; f < kNumFaces; ++f) {
            if (solution_.start.center(f) == letter) return solution_.start.center(opp[f]);
        }
        return '?';
    }

    std::string pieceName(std::initializer_list<char> colours) const {
        std::string s;
        for (char c: colours) {
            if (!s.empty()) s += "-";
            s += name(c);
        }
        return s;
    }

    // "the red (right) face"
    std::string faceDesc(int face) const {
        return "the " + name(cube_.center(face)) + " (" + kFaceWord[face] + ") face";
    }

    // ---- stage / step bookkeeping ---------------------------------------------

    TutorStage &beginStage(std::string id, std::string stageName, std::string goal, std::string recognize,
                           std::vector<AlgorithmRef> algorithms = {}) {
        TutorStage s;
        s.id = std::move(id);
        s.name = std::move(stageName);
        s.goal = std::move(goal);
        s.recognize = std::move(recognize);
        s.algorithms = std::move(algorithms);
        solution_.stages.push_back(std::move(s));
        return solution_.stages.back();
    }

    void skipStage(std::string id, std::string stageName, std::string note) {
        TutorStage &s = beginStage(std::move(id), std::move(stageName), "", "");
        s.skippedNote = std::move(note);
    }

    void addStep(TutorStep step) {
        checkBudget();
        step.moves = step.setup;
        if (step.algorithm) {
            for (int i = 0; i < step.algorithm->repeat; ++i) {
                auto alg = parseMoves(step.algorithm->notation);
                step.moves.insert(step.moves.end(), alg.begin(), alg.end());
            }
        }
        cube_.apply(step.moves);
        solution_.moves.insert(solution_.moves.end(), step.moves.begin(), step.moves.end());
        solution_.stages.back().steps.push_back(std::move(step));
    }

    void checkBudget() {
        if (budget_.expired(0)) throw std::runtime_error("beginner solver ran out of time");
    }

    void require(bool ok, const char *what) const {
        if (!ok) throw std::runtime_error(std::string("tutor invariant failed: ") + what);
    }

    // ---- predicates -------------------------------------------------------------

    std::vector<char> sideColours() const {
        std::vector<char> out;
        for (int f = 0; f < kNumFaces; ++f) {
            char c = solution_.start.center(f);
            if (c != white_ && c != yellow_) out.push_back(c);
        }
        return out;
    }

    bool crossSolved() const {
        if (cube_.center(3) != white_) {
            // Not oriented yet: check on a copy held white-down.
            return false;
        }
        for (int p: kDLayerEdges) if (!edgeSolved(cube_, p)) return false;
        return true;
    }

    int petalCount(const FaceletCube &c) const {
        int n = 0;
        for (int p: kULayerEdges) n += c[kEdgeFacelets[static_cast<size_t>(p)][0]] == white_;
        return n;
    }

    bool firstLayerSolved() const {
        for (int p: kDLayerEdges) if (!edgeSolved(cube_, p)) return false;
        for (int p: kDLayerCorners) if (!cornerSolved(cube_, p)) return false;
        return true;
    }

    bool middleLayerSolved() const {
        for (int p: kMiddleEdges) if (!edgeSolved(cube_, p)) return false;
        return true;
    }

    bool firstTwoLayersSolved() const { return firstLayerSolved() && middleLayerSolved(); }

    // Describes where an edge is, e.g. "in the middle layer between the green and
    // red faces, with white facing the red side".
    std::string describeEdge(const EdgeLoc &loc, char primary) const {
        int pf = faceOfSticker(loc.facelet[0]);  // face the primary colour points at
        int of = faceOfSticker(loc.facelet[1]);
        std::string facing = (pf == 0 ? "up" : pf == 3 ? "down" : "towards " + faceDesc(pf));
        std::string layer;
        if (isULayerEdge(loc.pos)) layer = "in the top layer above " + faceDesc(pf == 0 ? of : pf);
        else if (isDLayerEdge(loc.pos)) layer = "in the bottom layer under " + faceDesc(pf == 3 ? of : pf);
        else layer = "in the middle layer between " + faceDesc(pf) + " and " + faceDesc(of);
        return layer + ", with " + name(primary) + " facing " + facing;
    }

    // ---- stage 0: hold the cube -------------------------------------------------

    void stageHold() {
        beginStage("hold", "Hold the cube",
                   "Hold the cube with the " + name(yellow_) + " centre on top and " + name(white_) +
                   " on the bottom. Keep holding it this way unless a step asks you to turn the whole cube.",
                   "The centres never move relative to each other, so they tell you where every colour belongs.");
        // Find the shortest whole-cube rotation that puts white on the bottom,
        // preferring green in front (the most common way tutorials hold a cube).
        static const std::vector<std::vector<Move>> kCandidates = [] {
            std::vector<std::vector<Move>> c = {{}};
            const Axis axes[3] = {Axis::X, Axis::Y, Axis::Z};
            for (Axis a: axes) for (uint8_t t = 1; t <= 3; ++t) c.push_back({Move{a, t}});
            for (Axis a: axes) for (uint8_t t = 1; t <= 3; ++t)
                    for (Axis b: axes) for (uint8_t u = 1; u <= 3; ++u)
                            if (a != b) c.push_back({Move{a, t}, Move{b, u}});
            return c;
        }();
        const std::vector<Move> *best = nullptr;
        int bestScore = 1 << 30;
        for (const auto &cand: kCandidates) {
            FaceletCube c = cube_.after(cand);
            if (c.center(3) != white_) continue;
            int score = static_cast<int>(cand.size()) * 10 + (name(c.center(2)) == "green" ? 0 : 5);
            if (score < bestScore) {
                bestScore = score;
                best = &cand;
            }
        }
        require(best != nullptr, "orientation");
        FaceletCube held = cube_.after(*best);
        TutorStep step;
        step.title = best->empty() ? "You're already holding it right" : "Turn the whole cube";
        step.observe = "Find the " + name(white_) + " centre and the " + name(yellow_) + " centre: they are opposite each other.";
        step.instruction = "Hold the cube with " + name(yellow_) + " on top, " + name(white_) + " on the bottom and " +
                           name(held.center(2)) + " facing you.";
        step.tip = "x, y and z turn the whole cube, not a single layer.";
        step.setup = *best;
        step.focus = {4, 31, 22};
        addStep(std::move(step));
    }

    // ---- stage 1: daisy ------------------------------------------------------------

    // Shortest face-turn sequence that makes `edge` a petal and adds exactly one
    // petal (existing petals may move around the top but stay petals).
    bool searchPetal(FaceletCube &c, char other, int target, int depth, int lastFace, std::vector<Move> &path,
                     uint64_t &nodes) {
        if (depth == 0) {
            if (petalCount(c) != target) return false;
            EdgeLoc loc = findEdge(c, white_, other);
            return isULayerEdge(loc.pos) && faceOfSticker(loc.facelet[0]) == 0;
        }
        for (int m = 0; m < 18; ++m) {
            int face = m / 3;
            // Same face twice, or opposite faces in the non-canonical order, never helps.
            if (face == lastFace || (lastFace >= 0 && face % 3 == lastFace % 3 && face < lastFace)) continue;
            if ((++nodes & 1023) == 0) checkBudget();
            Move mv = Move::fromFaceMoveIndex(m);
            FaceletCube next = c.after({mv});
            path.push_back(mv);
            if (searchPetal(next, other, target, depth - 1, face, path, nodes)) return true;
            path.pop_back();
        }
        return false;
    }

    void stageDaisy() {
        beginStage("daisy", "The daisy",
                   "Bring the four " + name(white_) + " edges up around the " + name(yellow_) +
                   " centre, " + name(white_) + " facing up, like petals around a flower.",
                   "Find the edges that have a " + name(white_) + " sticker. Each is in the top layer (maybe flipped), "
                   "the middle layer or the bottom layer, and each place has its own quick fix.");
        if (petalCount(cube_) == 4) {
            solution_.stages.back().skippedNote = "The daisy is already complete.";
            return;
        }
        while (petalCount(cube_) < 4) {
            int target = petalCount(cube_) + 1;
            bool done = false;
            for (int depth = 1; depth <= 6 && !done; ++depth) {
                for (char side: sideColours()) {
                    EdgeLoc loc = findEdge(cube_, white_, side);
                    if (isULayerEdge(loc.pos) && faceOfSticker(loc.facelet[0]) == 0) continue;  // already a petal
                    std::vector<Move> path;
                    uint64_t nodes = 0;
                    FaceletCube c = cube_;
                    if (!searchPetal(c, side, target, depth, -1, path, nodes)) continue;
                    addStep(daisyStep(loc, side, path));
                    done = true;
                    break;
                }
            }
            require(done, "daisy petal");
        }
    }

    TutorStep daisyStep(const EdgeLoc &loc, char side, const std::vector<Move> &path) const {
        TutorStep s;
        s.title = "Bring the " + pieceName({white_, side}) + " edge up";
        s.observe = capitalize("the " + pieceName({white_, side}) + " edge is " + describeEdge(loc, white_) + ".");
        int wf = faceOfSticker(loc.facelet[0]);
        if (isULayerEdge(loc.pos)) {
            s.instruction = "It's on top but flipped. Turn its side face once to drop it into the middle layer, "
                            "then lift it back up with " + name(white_) + " facing up.";
        } else if (isDLayerEdge(loc.pos) && wf == 3) {
            s.instruction = "It's on the bottom with " + name(white_) + " facing down. A half turn of its side face "
                            "lifts it straight up with " + name(white_) + " facing up.";
        } else if (isDLayerEdge(loc.pos)) {
            s.instruction = "It's on the bottom with " + name(white_) + " facing sideways. Turn its side face once "
                            "to bring it into the middle layer, then lift it up like a middle-layer edge.";
        } else {
            s.instruction = "It's in the middle layer. One turn of the side face next to " + name(white_) +
                            " lifts it straight up to the top.";
        }
        s.tip = "If a petal is already where the new one will land, turn the top (U) first to make room. "
                "Petals can slide around freely on top.";
        s.setup = path;
        s.focus = {loc.facelet[0], loc.facelet[1]};
        return s;
    }

    // ---- stage 2: white cross ---------------------------------------------------------

    void stageCross() {
        beginStage("white-cross", "White cross",
                   "Move each petal down to the bottom so the " + name(white_) +
                   " cross lines up with the side centres.",
                   "Look at the side colour of a petal, then find the centre with the same colour.");
        for (int i = 0; i < 4; ++i) {
            // Pick the petal that needs the fewest top turns to line up.
            int bestK = -1, bestFace = -1;
            char bestSide = 0;
            for (char side: sideColours()) {
                EdgeLoc loc = findEdge(cube_, white_, side);
                if (!isULayerEdge(loc.pos) || faceOfSticker(loc.facelet[0]) != 0) continue;
                int face = -1;
                int k = findTurns(cube_, Axis::U, [&](const FaceletCube &c) {
                    EdgeLoc l = findEdge(c, white_, side);
                    int f = faceOfSticker(l.facelet[1]);
                    if (c.center(f) != side) return false;
                    face = f;
                    return true;
                });
                if (k < 0) continue;
                int cost = k == 2 ? 2 : (k == 0 ? 0 : 1);
                int bestCost = bestK == 2 ? 2 : (bestK == 0 ? 0 : 1);
                if (bestK < 0 || cost < bestCost) {
                    bestK = k;
                    bestFace = face;
                    bestSide = side;
                }
            }
            require(bestK >= 0, "cross petal");
            EdgeLoc loc = findEdge(cube_, white_, bestSide);
            TutorStep s;
            s.title = "Bring the " + pieceName({white_, bestSide}) + " petal down";
            s.observe = "This petal's side colour is " + name(bestSide) + ". " +
                        (bestK == 0 ? "It is already sitting right above the " + name(bestSide) + " centre."
                                    : "Turn the top until it sits right above the " + name(bestSide) +
                                      " centre: the edge and centre then make a matching column.");
            s.instruction = "Turn the " + name(bestSide) + " face twice. The edge drops to the bottom with " +
                            name(white_) + " facing down.";
            s.setup = concat(turns(Axis::U, bestK), {Move{static_cast<Axis>(bestFace), 2}});
            s.focus = {loc.facelet[0], loc.facelet[1]};
            addStep(std::move(s));
        }
        for (int p: kDLayerEdges) require(edgeSolved(cube_, p), "white cross");
    }

    // ---- stage 3: first-layer corners ---------------------------------------------------

    struct CornerColours {
        char w, a, b;
    };

    std::vector<CornerColours> whiteCorners() const {
        std::vector<CornerColours> out;
        int wf = faceOfLetter(white_);
        for (const auto &cc: kCornerColors) {
            std::vector<char> others;
            bool hasWhite = false;
            for (auto f: cc) {
                if (f == wf) hasWhite = true;
                else others.push_back(kFaceChars[f]);
            }
            if (hasWhite) out.push_back({white_, others[0], others[1]});
        }
        return out;
    }

    // Bottom-layer position whose side centres are colours a and b.
    int homeOf(char a, char b) const {
        for (int p: kDLayerCorners) {
            std::set<char> centres;
            for (int k = 1; k < 3; ++k)
                centres.insert(cube_.center(faceOfSticker(kCornerFacelets[static_cast<size_t>(p)][static_cast<size_t>(k)])));
            if (centres == std::set<char>{a, b}) return p;
        }
        return -1;
    }

    void stageWhiteCorners() {
        beginStage("white-corners", "First layer corners",
                   "Fill the four bottom corners so the whole " + name(white_) +
                   " face, and the first row of every side, is solved.",
                   "A " + name(white_) + " corner is either in the top layer (ready to insert) or in the bottom "
                   "layer in the wrong spot or twisted (pop it out first). Where its " + name(white_) +
                   " sticker points tells you how many times to repeat the algorithm.",
                   {kSexyRef});
        bool any = false;
        for (int guard = 0; guard < 12; ++guard) {
            std::vector<CornerColours> todo;
            for (const auto &c: whiteCorners()) {
                int home = homeOf(c.a, c.b);
                CornerLoc loc = findCorner(cube_, c.w, c.a, c.b);
                if (loc.pos != home || !cornerSolved(cube_, home)) todo.push_back(c);
            }
            if (todo.empty()) break;
            any = true;
            // Prefer a corner already in the top layer.
            auto it = std::find_if(todo.begin(), todo.end(), [&](const CornerColours &c) {
                return isULayerCorner(findCorner(cube_, c.w, c.a, c.b).pos);
            });
            CornerColours c = it != todo.end() ? *it : todo.front();
            CornerLoc loc = findCorner(cube_, c.w, c.a, c.b);
            if (!isULayerCorner(loc.pos)) popCornerOut(c, loc);
            insertCorner(c);
        }
        if (!any) solution_.stages.back().skippedNote = "All four bottom corners are already in place.";
        require(firstLayerSolved(), "first layer");
    }

    void popCornerOut(const CornerColours &c, const CornerLoc &loc) {
        int k = findTurns(cube_, Axis::Y, [&](const FaceletCube &cc) {
            return findCorner(cc, c.w, c.a, c.b).pos == DFR;
        });
        require(k >= 0, "pop corner");
        TutorStep s;
        s.title = "Pop out the " + pieceName({c.w, c.a, c.b}) + " corner";
        s.observe = "This corner is in the bottom layer, but in the wrong spot or twisted.";
        s.instruction = "Turn the whole cube so it is at the front-right of the bottom, then do R U R' U' once. "
                        "That lifts it into the top layer.";
        s.setup = turns(Axis::Y, k);
        s.algorithm = kSexyRef;
        s.focus = {loc.facelet[0], loc.facelet[1], loc.facelet[2]};
        addStep(std::move(s));
    }

    void insertCorner(const CornerColours &c) {
        int home = homeOf(c.a, c.b);
        CornerLoc start = findCorner(cube_, c.w, c.a, c.b);
        // 1. Turn the top so the corner sits above its home.
        int j = findTurns(cube_, Axis::U, [&](const FaceletCube &cc) {
            return findCorner(cc, c.w, c.a, c.b).pos == cornerAbove(home);
        });
        require(j >= 0, "corner above home");
        FaceletCube afterU = cube_.after(turns(Axis::U, j));
        // 2. Turn the whole cube so that home is front-right.
        int k = findTurns(afterU, Axis::Y, [&](const FaceletCube &cc) {
            return findCorner(cc, c.w, c.a, c.b).pos == URF;
        });
        require(k >= 0, "corner to front-right");
        std::vector<Move> setup = concat(turns(Axis::U, j), turns(Axis::Y, k));
        FaceletCube ready = cube_.after(setup);
        CornerLoc at = findCorner(ready, c.w, c.a, c.b);
        int wf = faceOfSticker(at.facelet[0]);
        std::string dir = wf == 0 ? "up" : wf == 1 ? "to the right" : "towards you";
        // 3. Count repetitions by simulation (1, 3 or 5).
        int reps = 0;
        FaceletCube sim = ready;
        while (!cornerSolved(sim, DFR)) {
            sim.apply(kSexy);
            ++reps;
            require(reps <= 5, "corner repetitions");
        }

        TutorStep s;
        s.title = "Insert the " + pieceName({c.w, c.a, c.b}) + " corner";
        s.observe = "Its home is the bottom corner between the " + name(c.a) + " and " + name(c.b) +
                    " centres. Once it sits right above that spot, its " + name(c.w) + " sticker points " + dir + ".";
        s.instruction = std::string(j ? "Turn the top until the corner is above its home. " : "") +
                        (k ? "Turn the whole cube so that spot is front-right. " : "") +
                        "Then repeat R U R' U' until " + name(c.w) + " faces down: " + std::to_string(reps) +
                        (reps == 1 ? " time." : " times.");
        s.tip = "White pointing right needs 1 repeat, towards you needs 5, and up needs 3. "
                "Learn that pattern and you will never need to count.";
        s.setup = setup;
        AlgorithmRef alg = kSexyRef;
        alg.repeat = reps;
        s.algorithm = alg;
        s.focus = {start.facelet[0], start.facelet[1], start.facelet[2]};
        QuizItem q;
        q.question = "The " + name(c.w) + " sticker points " + dir + ". How many times will you repeat R U R' U'?";
        q.options = {"1", "3", "5"};
        q.answer = reps == 1 ? 0 : reps == 3 ? 1 : 2;
        q.explanation = "Each R U R' U' twists the corner by one step: right = 1, up = 3, towards you = 5.";
        s.quiz = q;
        addStep(std::move(s));
    }

    // ---- stage 4: middle layer --------------------------------------------------------------

    void stageMiddleLayer() {
        beginStage("middle-layer", "Second layer edges",
                   "Place the four edges that have no " + name(yellow_) + " into the middle layer.",
                   "Find a top edge with no " + name(yellow_) + ". Turn the top until its front colour matches the "
                   "centre below it (an upside-down T), then check its top colour: it goes towards the side with "
                   "that centre.",
                   {kRightRef, kLeftRef});
        if (middleLayerSolved()) {
            solution_.stages.back().skippedNote = "The middle layer is already solved.";
            return;
        }
        for (int guard = 0; guard < 12 && !middleLayerSolved(); ++guard) {
            // Candidate: top-layer edge without yellow, needing the fewest top turns.
            int bestJ = -1;
            int bestPos = -1;
            for (int p: kULayerEdges) {
                int f0 = kEdgeFacelets[static_cast<size_t>(p)][0], f1 = kEdgeFacelets[static_cast<size_t>(p)][1];
                if (cube_[f0] == yellow_ || cube_[f1] == yellow_) continue;
                char side = cube_[f1], top = cube_[f0];
                int j = findTurns(cube_, Axis::U, [&](const FaceletCube &c) {
                    EdgeLoc l = findEdge(c, side, top);
                    return c.center(faceOfSticker(l.facelet[0])) == side;
                });
                int cost = j == 0 ? 0 : (j == 2 ? 2 : 1);
                int bestCost = bestJ == 0 ? 0 : (bestJ == 2 ? 2 : 1);
                if (j >= 0 && (bestJ < 0 || cost < bestCost)) {
                    bestJ = j;
                    bestPos = p;
                }
            }
            if (bestPos >= 0) insertMiddleEdge(bestPos, bestJ);
            else freeStuckEdge();
        }
        require(firstTwoLayersSolved(), "second layer");
    }

    void insertMiddleEdge(int pos, int j) {
        int f0 = kEdgeFacelets[static_cast<size_t>(pos)][0], f1 = kEdgeFacelets[static_cast<size_t>(pos)][1];
        char side = cube_[f1], top = cube_[f0];
        FaceletCube afterU = cube_.after(turns(Axis::U, j));
        int k = findTurns(afterU, Axis::Y, [&](const FaceletCube &c) {
            EdgeLoc l = findEdge(c, side, top);
            return l.pos == UF && faceOfSticker(l.facelet[0]) == 2;
        });
        require(k >= 0, "middle edge to front");
        std::vector<Move> setup = concat(turns(Axis::U, j), turns(Axis::Y, k));
        FaceletCube ready = cube_.after(setup);
        bool right = ready.center(1) == top;
        require(right || ready.center(4) == top, "middle edge direction");

        TutorStep s;
        s.title = "Insert the " + pieceName({side, top}) + " edge";
        s.observe = "The " + pieceName({side, top}) + " edge has no " + name(yellow_) + ". " +
                    (j ? "Turn the top until its " + name(side) + " side sits above the " + name(side) + " centre (an upside-down T)."
                       : "Its " + name(side) + " side already sits above the " + name(side) + " centre (an upside-down T).") +
                    " Its top colour is " + name(top) + ".";
        s.instruction = std::string(k ? "Turn the whole cube so the T faces you. " : "") + "The " + name(top) +
                        " centre is on the " + (right ? "right" : "left") + ", so the edge goes " +
                        (right ? "right" : "left") + ": use the " + (right ? "right" : "left") + " insert.";
        s.tip = "Both inserts are mirror images: move the top away from the target, take the slot out, put it back, "
                "then fix the corner.";
        s.setup = setup;
        s.algorithm = right ? kRightRef : kLeftRef;
        EdgeLoc loc = findEdge(cube_, side, top);
        s.focus = {loc.facelet[0], loc.facelet[1]};
        QuizItem q;
        q.question = "The top colour of this edge is " + name(top) + ". Which way does it go?";
        q.options = {"Left", "Right"};
        q.answer = right ? 1 : 0;
        q.explanation = "It goes towards the side whose centre matches its top colour.";
        s.quiz = q;
        addStep(std::move(s));
    }

    void freeStuckEdge() {
        int stuck = -1;
        for (int p: kMiddleEdges) if (!edgeSolved(cube_, p)) { stuck = p; break; }
        require(stuck >= 0, "stuck edge");
        int f0 = kEdgeFacelets[static_cast<size_t>(stuck)][0], f1 = kEdgeFacelets[static_cast<size_t>(stuck)][1];
        char a = cube_[f0], b = cube_[f1];
        int k = findTurns(cube_, Axis::Y, [&](const FaceletCube &c) { return findEdge(c, a, b).pos == FR; });
        require(k >= 0, "stuck edge to front-right");
        TutorStep s;
        s.title = "Free a stuck edge";
        s.observe = "Every top edge has " + name(yellow_) + ", but the " + pieceName({a, b}) +
                    " edge is stuck in the middle layer (wrong slot or flipped).";
        s.instruction = "Turn the whole cube so that slot is front-right, then do the right insert once. "
                        "It swaps the stuck edge out to the top so you can insert it properly.";
        s.setup = turns(Axis::Y, k);
        s.algorithm = kRightRef;
        s.focus = {f0, f1};
        addStep(std::move(s));
    }

    // ---- stage 5: yellow cross ------------------------------------------------------------

    enum class CrossShape { Dot, L, Line, Cross };

    CrossShape crossShape(const FaceletCube &c) const {
        bool up[4];
        int n = 0;
        for (int i = 0; i < 4; ++i) {
            up[i] = c[kEdgeFacelets[static_cast<size_t>(kULayerEdges[i])][0]] == yellow_;
            n += up[i];
        }
        if (n == 4) return CrossShape::Cross;
        if (n == 0) return CrossShape::Dot;
        // kULayerEdges = UR UF UL UB: opposite pairs are (0,2) and (1,3).
        if ((up[0] && up[2]) || (up[1] && up[3])) return CrossShape::Line;
        return CrossShape::L;
    }

    static const char *shapeName(CrossShape s) {
        switch (s) {
            case CrossShape::Dot: return "a dot";
            case CrossShape::L: return "an L";
            case CrossShape::Line: return "a line";
            default: return "a cross";
        }
    }

    void stageYellowCross() {
        beginStage("yellow-cross", "Yellow cross",
                   "Make a " + name(yellow_) + " cross on top. Only the edges count; ignore the corners for now.",
                   "Look only at the four edge stickers around the " + name(yellow_) +
                   " centre. You will see a dot, an L, a line or a cross.",
                   {kYellowCrossRef});
        if (crossShape(cube_) == CrossShape::Cross) {
            solution_.stages.back().skippedNote = "The yellow cross is already there.";
            return;
        }
        for (int guard = 0; guard < 4 && crossShape(cube_) != CrossShape::Cross; ++guard) {
            CrossShape shape = crossShape(cube_);
            auto progressed = [&](const FaceletCube &c) {
                CrossShape after = crossShape(c.after(kYellowCross));
                return static_cast<int>(after) > static_cast<int>(shape);
            };
            // Canonical holds: L at back-left, line left-to-right. Fall back to any
            // top turn that makes progress (they are equivalent up to rotation).
            int j = findTurns(cube_, Axis::U, [&](const FaceletCube &c) {
                if (shape == CrossShape::L) {
                    bool ub = c[kEdgeFacelets[UB][0]] == yellow_, ul = c[kEdgeFacelets[UL][0]] == yellow_;
                    return ub && ul && progressed(c);
                }
                if (shape == CrossShape::Line) {
                    return c[kEdgeFacelets[UL][0]] == yellow_ && c[kEdgeFacelets[UR][0]] == yellow_ && progressed(c);
                }
                return progressed(c);
            });
            if (j < 0) j = findTurns(cube_, Axis::U, progressed);
            require(j >= 0, "yellow cross progress");

            TutorStep s;
            s.title = shape == CrossShape::Dot ? "From dot to L" : shape == CrossShape::L ? "From L to line" : "From line to cross";
            s.observe = "The top shows " + std::string(shapeName(shape)) + ".";
            if (shape == CrossShape::Dot) {
                s.instruction = "Do F R U R' U' F' from any angle. You'll get an L or a line.";
            } else if (shape == CrossShape::L) {
                s.instruction = std::string(j ? "Turn the top so the L sits at the back-left (pointing to 9 and 12 o'clock). " : "The L already sits at the back-left. ") +
                                "Then do F R U R' U' F'.";
            } else {
                s.instruction = std::string(j ? "Turn the top so the line runs left to right. " : "The line already runs left to right. ") +
                                "Then do F R U R' U' F'.";
            }
            s.tip = "Same algorithm every time: dot, then L, then line, then cross.";
            s.setup = turns(Axis::U, j);
            s.algorithm = kYellowCrossRef;
            for (int p: kULayerEdges) s.focus.push_back(kEdgeFacelets[static_cast<size_t>(p)][0]);
            QuizItem q;
            q.question = "Ignoring the corners, which shape do the " + name(yellow_) + " edges make on top?";
            q.options = {"Dot", "L", "Line", "Cross"};
            q.answer = static_cast<int>(shape);
            q.explanation = "Count the " + name(yellow_) + " edge stickers: none = dot, two next to each other = L, "
                            "two opposite = line.";
            s.quiz = q;
            addStep(std::move(s));
        }
        require(crossShape(cube_) == CrossShape::Cross && firstTwoLayersSolved(), "yellow cross");
    }

    // ---- stage 6: match yellow edges -----------------------------------------------------

    std::array<bool, 4> edgeMatches(const FaceletCube &c) const {
        std::array<bool, 4> m{};
        for (int i = 0; i < 4; ++i) {
            int f = kEdgeFacelets[static_cast<size_t>(kULayerEdges[i])][1];
            m[static_cast<size_t>(i)] = c[f] == c.center(faceOfSticker(f));
        }
        return m;
    }

    static int countTrue(const std::array<bool, 4> &a) {
        return static_cast<int>(std::count(a.begin(), a.end(), true));
    }

    bool yellowEdgesDone(const FaceletCube &c) const { return countTrue(edgeMatches(c)) == 4; }

    void stageYellowEdges() {
        beginStage("yellow-edges", "Match the yellow edges",
                   "Make the side colour of every " + name(yellow_) + " edge match the centre below it.",
                   "Turn the top until at least two edges match their centres. Then check: are the two matching "
                   "edges next to each other, or opposite?",
                   {kSuneRef});
        for (int guard = 0; guard < 6; ++guard) {
            // Best top turn.
            int bestJ = 0, bestCount = -1;
            for (int j: {0, 1, 3, 2}) {
                int n = countTrue(edgeMatches(cube_.after(turns(Axis::U, j))));
                if (n > bestCount) { bestCount = n; bestJ = j; }
            }
            if (bestCount == 4) {
                if (bestJ != 0) {
                    TutorStep s;
                    s.title = "Line up the edges";
                    s.observe = "One turn of the top makes all four edges match.";
                    s.instruction = "Turn the top until every edge matches the centre below it.";
                    s.setup = turns(Axis::U, bestJ);
                    for (int p: kULayerEdges) s.focus.push_back(kEdgeFacelets[static_cast<size_t>(p)][1]);
                    addStep(std::move(s));
                } else if (guard == 0) {
                    solution_.stages.back().skippedNote = "All yellow edges already match their centres.";
                }
                break;
            }
            FaceletCube afterU = cube_.after(turns(Axis::U, bestJ));
            auto m = edgeMatches(afterU);
            // Adjacent pair (UR+UF, UF+UL, UL+UB, UB+UR) vs opposite.
            bool adjacent = false;
            for (int i = 0; i < 4; ++i) adjacent |= m[static_cast<size_t>(i)] && m[static_cast<size_t>((i + 1) % 4)];
            int k = 0;
            if (adjacent) {
                // Hold the matched pair at the back and right (UB and UR).
                k = findTurns(afterU, Axis::Y, [&](const FaceletCube &c) {
                    auto mm = edgeMatches(c);
                    return mm[0] && mm[3];  // UR and UB
                });
                require(k >= 0, "yellow edges hold");
            }
            std::vector<Move> setup = concat(turns(Axis::U, bestJ), turns(Axis::Y, k));
            TutorStep s;
            s.title = adjacent ? "Swap the front and left edges" : "Break up the opposite pair";
            s.observe = bestCount == 2
                        ? std::string("After turning the top, two edges match and they are ") +
                          (adjacent ? "next to each other." : "opposite each other.")
                        : "No turn of the top makes two edges match yet.";
            s.instruction = adjacent
                            ? std::string(k ? "Turn the whole cube so the matching pair is at the back and right. " : "The matching pair is already at the back and right. ") +
                              "Do R U R' U R U2 R' U."
                            : "Do R U R' U R U2 R' U from here. Afterwards you'll find a side-by-side pair.";
            s.tip = "This is the Sune with an extra U at the end. It swaps the front and left edges.";
            s.setup = setup;
            s.algorithm = kSuneRef;
            for (int p: kULayerEdges) s.focus.push_back(kEdgeFacelets[static_cast<size_t>(p)][1]);
            QuizItem q;
            q.question = "After turning the top for the most matches, where are the matching edges?";
            q.options = {"Next to each other", "Opposite each other"};
            q.answer = adjacent ? 0 : 1;
            q.explanation = "Next to each other: hold them back and right. Opposite: do the algorithm once from anywhere.";
            s.quiz = q;
            addStep(std::move(s));
        }
        require(yellowEdgesDone(cube_) && firstTwoLayersSolved(), "yellow edges");
    }

    // ---- stage 7: position yellow corners ----------------------------------------------

    int cornersInPlace(const FaceletCube &c) const {
        int n = 0;
        for (int p: kULayerCorners) n += cornerInPlace(c, p);
        return n;
    }

    void stagePositionCorners() {
        beginStage("yellow-corners-position", "Position the yellow corners",
                   "Move every top corner to its correct spot. It may still be twisted; that's fine for now.",
                   "A corner is in the right spot when its three colours match the three centres around it, "
                   "in any order.",
                   {kCornerCycleRef});
        if (cornersInPlace(cube_) == 4) {
            solution_.stages.back().skippedNote = "All yellow corners are already in their spots.";
            return;
        }
        for (int guard = 0; guard < 6 && cornersInPlace(cube_) < 4; ++guard) {
            int n = cornersInPlace(cube_);
            int k = 0;
            if (n >= 1) {
                k = findTurns(cube_, Axis::Y, [&](const FaceletCube &c) { return cornerInPlace(c, URF); });
                require(k >= 0, "corner hold");
            }
            TutorStep s;
            s.title = n == 0 ? "Get one corner into place" : "Cycle the other three corners";
            s.observe = n == 0 ? "No corner is in its correct spot yet."
                               : "One corner is already in the right spot (colours match the centres around it).";
            s.instruction = n == 0 ? "Do U R U' L' U R' U' L from any angle. Afterwards exactly one corner will be in place."
                                   : std::string(k ? "Turn the whole cube so that corner is at the front-right of the top. " : "That corner is already front-right on top. ") +
                                     "Do U R U' L' U R' U' L. It keeps that corner and cycles the other three.";
            s.setup = turns(Axis::Y, k);
            s.algorithm = kCornerCycleRef;
            for (int p: kULayerCorners)
                for (int f: kCornerFacelets[static_cast<size_t>(p)]) s.focus.push_back(f);
            QuizItem q;
            q.question = "How many top corners are already in the right spot (twist doesn't matter)?";
            q.options = {"None", "One", "All four"};
            q.answer = n == 0 ? 0 : 1;
            q.explanation = "It is always none, one or all four. Two can't happen once the edges match.";
            s.quiz = q;
            addStep(std::move(s));
        }
        require(cornersInPlace(cube_) == 4, "corner positions");
    }

    // ---- stage 8: orient yellow corners -------------------------------------------------

    bool yellowUp(const FaceletCube &c, int pos) const {
        return c[kCornerFacelets[static_cast<size_t>(pos)][0]] == yellow_;
    }

    void stageOrientCorners() {
        beginStage("yellow-corners-orient", "Twist the yellow corners",
                   "Twist each top corner until " + name(yellow_) + " faces up. Then the cube is solved!",
                   "Hold an unsolved corner at the front-right of the top. Between corners, turn only the top layer, "
                   "never the whole cube. The bottom will look scrambled halfway; it fixes itself.",
                   {kCornerTwistRef});
        bool anyTwisted = false;
        for (int p: kULayerCorners) anyTwisted |= !yellowUp(cube_, p);
        if (!anyTwisted) {
            alignTop("The corners are already twisted correctly.");
            return;
        }
        // Start with an unsolved corner front-right; this is the last whole-cube turn.
        int k = findTurns(cube_, Axis::Y, [&](const FaceletCube &c) { return !yellowUp(c, URF); });
        require(k >= 0, "orient start");
        std::vector<Move> pending = turns(Axis::Y, k);
        for (int guard = 0; guard < 4; ++guard) {
            FaceletCube ready = cube_.after(pending);
            int reps = 0;
            FaceletCube sim = ready;
            while (!yellowUp(sim, URF)) {
                sim.apply(kCornerTwist);
                ++reps;
                require(reps <= 4, "corner twist reps");
            }
            int yf = faceOfSticker(ready[kCornerFacelets[URF][1]] == yellow_ ? kCornerFacelets[URF][1]
                                                                              : kCornerFacelets[URF][2]);
            std::string dir = yf == 1 ? "to the right" : "towards you";
            TutorStep s;
            s.title = "Twist a corner";
            s.observe = "The front-right top corner has " + name(yellow_) + " pointing " + dir + ".";
            s.instruction = std::string(pending.empty() ? "" : guard == 0 ? "Turn the whole cube to bring an unsolved corner to the front-right of the top. "
                                                                           : "Turn only the top to bring the next unsolved corner to the front-right. ") +
                            "Repeat R' D' R D until " + name(yellow_) + " faces up: " + std::to_string(reps) + " times.";
            s.tip = "Never turn the whole cube during this stage, only the top. The scrambled bottom will fix itself.";
            s.setup = pending;
            AlgorithmRef alg = kCornerTwistRef;
            alg.repeat = reps;
            s.algorithm = alg;
            for (int f: kCornerFacelets[URF]) s.focus.push_back(f);
            QuizItem q;
            q.question = "The " + name(yellow_) + " sticker points " + dir + ". How many times will you repeat R' D' R D?";
            q.options = {"2", "4"};
            q.answer = reps == 2 ? 0 : 1;
            q.explanation = "Pointing right needs 2 repeats; pointing towards you needs 4.";
            s.quiz = q;
            // Focus is captured before the setup turns, so map it to where the corner starts.
            s.focus.clear();
            {
                CornerLoc loc = findCorner(cube_, ready[kCornerFacelets[URF][0]], ready[kCornerFacelets[URF][1]],
                                           ready[kCornerFacelets[URF][2]]);
                s.focus = {loc.facelet[0], loc.facelet[1], loc.facelet[2]};
            }
            addStep(std::move(s));
            // Next unsolved corner: turn only U.
            int j = findTurns(cube_, Axis::U, [&](const FaceletCube &c) { return !yellowUp(c, URF); });
            if (j < 0) break;
            pending = turns(Axis::U, j);
        }
        alignTop("");
    }

    void alignTop(const std::string &note) {
        int j = findTurns(cube_, Axis::U, [](const FaceletCube &c) { return c.isSolved(); });
        require(j >= 0, "final alignment");
        if (j == 0) {
            if (!note.empty()) solution_.stages.back().skippedNote = note;
            return;
        }
        TutorStep s;
        s.title = "Final turn";
        s.observe = "Every piece is right; only the top layer is turned.";
        s.instruction = "Turn the top to line it up. Solved!";
        s.setup = turns(Axis::U, j);
        addStep(std::move(s));
    }

    FaceletCube cube_;
    ColorNames names_;
    const SearchBudget &budget_;
    char white_ = 'U';
    char yellow_ = 'D';
    TutorSolution solution_;
};

}  // namespace

TutorSolution solveBeginner(const FaceletCube &cube, const ColorNames &names, const SearchBudget &budget) {
    return BeginnerTutor(cube, names, budget).run();
}

}  // namespace zc
