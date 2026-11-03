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
};

Graph pointsGraph(const std::vector<Outline>& outlines);

}  // namespace outline
