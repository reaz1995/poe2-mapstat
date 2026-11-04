#pragma once
// Where the named points of a zone lie across runs: outline pictures (outline.h) of different instances are lined
// up on the exit found in most of them (the same label = the same point) and their points drawn on one outline.
#include <string>
#include <vector>

#include "outline.h"

namespace outline {

struct GraphText {  // to write on the graph (the app has the fonts), UTF-8
    int x = 0, y = 0;
    std::string text;
};

struct Graph {
    Image img;  // empty when fewer than 2 runs share a named exit
    std::vector<GraphText> texts;  // legend lines
    int runs = 0;  // outlines given
    int used = 0;  // outlines lined up and drawn
    std::vector<int> usedRuns;  // which (indices into the outlines given)
    std::vector<double> fits;  // per outline given: share of its walls on the drawn one's (-1 = not lined up)
};

// minFit: an outline whose walls lie less than this on the drawn one's is another layout and is left out.
Graph pointsGraph(const std::vector<Outline>& outlines, double minFit = 0.3);

// A zone is made from a few base layouts that the game turns, mirrors and changes a little. Outlines of the same base
// have about the same shape (the ground inside the outer walls), turned or mirrored or not: groups of them, largest
// first (indices into outlines). minSame: how much two shapes must overlap (0..1) to be the same base.
std::vector<std::vector<int>> layoutFamilies(const std::vector<Outline>& outlines, double minSame = 0.8);

}  // namespace outline
