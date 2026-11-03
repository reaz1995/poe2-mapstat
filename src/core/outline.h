#pragma once
// Layout outlines from screenshots of the in-game map overlay: the light-blue area outline and the labelled points
// of interest (Checkpoint, Waypoint, zone exits) are taken from each screenshot, and screenshots of the same
// instance are joined into one picture where their outlines overlap.
#include <cstdint>
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

const double kMatchScore = 0.8;  // same place above this (measured: 0.90-1.00 for real overlaps, < 0.5 otherwise)

// New outline picture from one screenshot: black background, outline and labelled points of interest.
// Parts of a joined picture that no screenshot showed are (1, 1, 1): they look black but are not compared.
Image render(const Image& shot);

// `base` (an outline picture) extended with `add` placed at (dx, dy). A screenshot (`addIsShot`) gives its
// outline and points of interest; an outline picture is copied where `base` is still empty.
Image merge(const Image& base, const Image& add, bool addIsShot, int dx, int dy);

// Adds a screenshot to the outline pictures of one instance. It goes into the first outline it overlaps; other
// outlines it overlaps are joined into that one too and left empty (their files can go). No overlap: a new
// outline is appended. Returns the index of the outline that got the screenshot and how many it overlapped.
struct Added {
    int into = -1;
    int overlapped = 0;
};
Added add(std::vector<Image>& outlines, const Image& shot);

}  // namespace outline
