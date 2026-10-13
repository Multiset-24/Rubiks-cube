#include "core/validation.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace zc {

const ColorNames &defaultColorNames() {
    static const ColorNames names = {"white", "red", "green", "yellow", "orange", "blue"};
    return names;
}

namespace {

const char *kFaceWords[kNumFaces] = {"top", "right", "front", "bottom", "left", "back"};

int faceOf(char c) {
    for (int f = 0; f < kNumFaces; ++f) if (kFaceChars[f] == c) return f;
    return -1;
}

std::string positionWords(const char *name) {
    // "URF" -> "top-right-front"
    std::string out;
    for (const char *p = name; *p; ++p) {
        if (!out.empty()) out += '-';
        out += kFaceWords[faceOf(*p)];
    }
    return out;
}

std::string joinColors(const std::vector<int> &faces, const ColorNames &names) {
    std::string out;
    for (size_t i = 0; i < faces.size(); ++i) {
        if (i) out += i + 1 == faces.size() ? " and " : ", ";
        out += faces[i] >= 0 ? names[static_cast<size_t>(faces[i])] : "unknown";
    }
    return out;
}

ValidationIssue issue(std::string code, std::string message, std::string hint, std::vector<int> facelets = {}) {
    return ValidationIssue{std::move(code), std::move(message), std::move(hint), std::move(facelets)};
}

}  // namespace

ValidationResult validateCube(const std::string &rawInput, const ColorNames &names) {
    ValidationResult result;
    std::string input;
    for (char c: rawInput) if (!std::isspace(static_cast<unsigned char>(c))) input += c;

    if (input.size() != kStickers) {
        result.issues.push_back(issue("LENGTH",
                                      "Expected 54 stickers but got " + std::to_string(input.size()) + ".",
                                      "Send 9 stickers for each of the 6 faces, in the order U R F D L B."));
        return result;
    }

    // Colour string -> facelet string, using the centres to identify faces.
    std::map<char, int> centreFace;
    for (int f = 0; f < kNumFaces; ++f) {
        char c = input[static_cast<size_t>(f * 9 + 4)];
        if (centreFace.count(c)) {
            result.issues.push_back(issue("CENTERS",
                                          "Two faces have the same centre colour.",
                                          "Each face must be scanned once; check that you did not scan the same face twice.",
                                          {f * 9 + 4, centreFace[c] * 9 + 4}));
            return result;
        }
        centreFace[c] = f;
    }
    std::string facelets(kStickers, '?');
    std::vector<int> unknown;
    for (int i = 0; i < kStickers; ++i) {
        auto it = centreFace.find(input[static_cast<size_t>(i)]);
        if (it == centreFace.end()) unknown.push_back(i);
        else facelets[static_cast<size_t>(i)] = kFaceChars[it->second];
    }
    if (!unknown.empty()) {
        result.issues.push_back(issue("UNKNOWN_COLOR",
                                      std::to_string(unknown.size()) + " sticker(s) have a colour that matches no centre.",
                                      "Re-check the highlighted stickers.", unknown));
        return result;
    }
    FaceletCube fc = FaceletCube::fromString(facelets);
    result.facelets = fc;

    // 9 of each colour.
    std::array<int, kNumFaces> count{};
    for (int i = 0; i < kStickers; ++i) ++count[static_cast<size_t>(faceOf(fc[i]))];
    for (int f = 0; f < kNumFaces; ++f) {
        if (count[static_cast<size_t>(f)] != 9) {
            std::vector<int> where;
            for (int i = 0; i < kStickers; ++i) if (faceOf(fc[i]) == f) where.push_back(i);
            result.issues.push_back(issue(
                    "COLOR_COUNT",
                    "There are " + std::to_string(count[static_cast<size_t>(f)]) + " " +
                    names[static_cast<size_t>(f)] + " stickers; a cube has exactly 9.",
                    count[static_cast<size_t>(f)] > 9
                    ? "One of the highlighted " + names[static_cast<size_t>(f)] +
                      " stickers is probably another colour (often red/orange or white/yellow mix-ups)."
                    : "Some " + names[static_cast<size_t>(f)] + " stickers were read as another colour.",
                    where));
        }
    }
    if (!result.issues.empty()) return result;

    // Every corner/edge must be a real piece, appearing once.
    std::map<std::set<int>, int> cornerSeenAt, edgeSeenAt;
    std::set<std::set<int>> validCorners, validEdges;
    for (const auto &c: kCornerColors) validCorners.insert({c[0], c[1], c[2]});
    for (const auto &e: kEdgeColors) validEdges.insert({e[0], e[1]});

    for (int i = 0; i < 8; ++i) {
        std::vector<int> faces;
        std::vector<int> where;
        for (int k = 0; k < 3; ++k) {
            faces.push_back(faceOf(fc[kCornerFacelets[i][k]]));
            where.push_back(kCornerFacelets[i][k]);
        }
        std::set<int> key(faces.begin(), faces.end());
        if (!validCorners.count(key)) {
            result.issues.push_back(issue("INVALID_CORNER",
                                          "The " + positionWords(kCornerNames[i]) + " corner shows " +
                                          joinColors(faces, names) + ", which no real corner has.",
                                          "One of these three stickers was mis-read.", where));
        } else if (cornerSeenAt.count(key)) {
            std::vector<int> both = where;
            for (int k = 0; k < 3; ++k) both.push_back(kCornerFacelets[cornerSeenAt[key]][k]);
            result.issues.push_back(issue("DUPLICATE_CORNER",
                                          "The " + joinColors(faces, names) + " corner appears twice.",
                                          "One of the two highlighted corners has a mis-read sticker.", both));
        } else {
            cornerSeenAt[key] = i;
        }
    }
    for (int i = 0; i < 12; ++i) {
        std::vector<int> faces = {faceOf(fc[kEdgeFacelets[i][0]]), faceOf(fc[kEdgeFacelets[i][1]])};
        std::vector<int> where = {kEdgeFacelets[i][0], kEdgeFacelets[i][1]};
        std::set<int> key(faces.begin(), faces.end());
        if (!validEdges.count(key)) {
            result.issues.push_back(issue("INVALID_EDGE",
                                          "The " + positionWords(kEdgeNames[i]) + " edge shows " +
                                          joinColors(faces, names) + ", which no real edge has.",
                                          faces[0] == faces[1] ? "An edge can never have the same colour twice."
                                                               : "Opposite colours never share an edge; one sticker was mis-read.",
                                          where));
        } else if (edgeSeenAt.count(key)) {
            result.issues.push_back(issue("DUPLICATE_EDGE",
                                          "The " + joinColors(faces, names) + " edge appears twice.",
                                          "One of the two highlighted edges has a mis-read sticker.",
                                          {where[0], where[1], kEdgeFacelets[edgeSeenAt[key]][0],
                                           kEdgeFacelets[edgeSeenAt[key]][1]}));
        } else {
            edgeSeenAt[key] = i;
        }
    }
    if (!result.issues.empty()) return result;

    auto cubie = CubieCube::fromFacelets(fc);
    if (!cubie) {
        // All pieces exist but a corner's colours run the wrong way round (mirror
        // image), which is what a face scanned upside down or mirrored produces.
        result.issues.push_back(issue("MIRRORED_CORNER",
                                      "A corner's colours are in an impossible order (mirror image).",
                                      "A face was probably scanned rotated or mirrored. Re-scan following the on-screen orientation."));
        return result;
    }
    if (cubie->twistSum() % 3 != 0) {
        result.issues.push_back(issue("TWISTED_CORNER",
                                      "The corners are twisted in a way turning cannot produce.",
                                      "Usually one corner sticker was mis-read. If the scan is right, a corner was twisted "
                                      "by hand: twist it back (or pop and re-seat it)."));
    }
    if (cubie->flipSum() % 2 != 0) {
        result.issues.push_back(issue("FLIPPED_EDGE",
                                      "One edge is flipped, which turning cannot produce.",
                                      "Usually one edge sticker was mis-read. Otherwise the cube was reassembled with an edge flipped."));
    }
    if (cubie->cornerParity() != cubie->edgeParity()) {
        result.issues.push_back(issue("PARITY",
                                      "Two pieces are swapped, which turning cannot produce.",
                                      "Check for two stickers whose colours were swapped during scanning."));
    }
    if (!result.issues.empty()) return result;

    result.valid = true;
    result.cubie = cubie;
    return result;
}

}  // namespace zc
