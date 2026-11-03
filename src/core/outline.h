#pragma once
// Layout outlines from screenshots of the in-game map overlay: the light-blue area outline and the labelled points
// of interest (Checkpoint, Waypoint, zone exits) are taken from each screenshot, and screenshots of the same
// instance are joined into one picture where their outlines overlap.
#include <cstdint>
#include <string>
#include <vector>

namespace outline {

struct Image {  // 24-bit RGB, rows top to bottom
    int w = 0, h = 0;
    std::vector<uint8_t> px;  // w * h * 3
    bool empty() const { return w <= 0 || h <= 0; }
};

struct Mask {
    int w = 0, h = 0;
    std::vector<uint8_t> m;  // 1 = set
    std::vector<uint8_t> cover;  // 1 = seen by a screenshot (a joined picture has empty corners)
};

// Outline pixels of a screenshot or of an outline picture made by render(); specks are removed.
Mask edges(const Image& img);

// Where `b` lies on `a` (b's top-left at dx, dy in a's coordinates) and how well the outlines agree there
// (0..1: share of the outline pixels in the overlap that line up).
struct Match {
    double score = 0;
    int dx = 0, dy = 0;
};
Match match(const Mask& a, const Mask& b);

const double kMatchScore = 0.65;  // same place above this (measured: 0.80-1.00 for real overlaps, < 0.5 otherwise)

// A label on an outline picture (Checkpoint, Waypoint, an exit's or a place's name): its text box and the icon
// under it, if any (cx < 0: none). Kept next to the picture (saveLabels), as text is hard to find again in it.
struct Label {
    int x = 0, y = 0, w = 0, h = 0;
    int cx = -1, cy = -1;
    bool exit = false;  // yellow icon (zone exit)
    std::vector<uint8_t> text;  // w x h: how bright the letters are (0-255) in the screenshot, background 0
};

struct Outline {
    Image img;
    std::vector<Label> labels;
    bool empty() const { return img.empty(); }
};

// Labels saved by an older version (found less well): the outline is better made again from its screenshots.
inline bool oldLabels(const std::string& saved) { return saved.find("outline labels v4") == std::string::npos; }
std::string saveLabels(const std::vector<Label>& labels);  // text, one label per line
std::vector<Label> loadLabels(const std::string& text);

// New outline picture from one screenshot: black background, outline and labelled points of interest.
// Parts of a joined picture that no screenshot showed are (1, 1, 1): they look black but are not compared.
Outline render(const Image& shot);


// Adds a screenshot to the outline pictures of one instance. It goes into the first outline it overlaps; other
// outlines it overlaps are joined into that one too and left empty (their files can go). No overlap: a new
// outline is appended. Returns the index of the outline that got the screenshot and how many it overlapped.
struct Added {
    int into = -1;
    int overlapped = 0;
};
Added add(std::vector<Outline>& outlines, const Image& shot);
// A picture resized by s (0.5 = half as wide and high).
Image scaled(const Image& im, double s);
Mask scaledMask(const Mask& m, double s);

}  // namespace outline
