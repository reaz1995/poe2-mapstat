#pragma once
// Pixel rules and small image helpers shared by outline.cpp and outline_graph.cpp (not part of the interface).
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace outline::detail {

// The overlay outline: white to light blue (with HDR the core is pure white and only the edge is blue).
inline bool isLine(const uint8_t* p) {
    int r = p[0], g = p[1], b = p[2];
    return std::min({r, g, b}) >= 200 && b >= r - 5 && b - r <= 75 && std::abs(r - g) <= 18;
}

inline bool isWhite(const uint8_t* p) {  // label text
    int mx = std::max({p[0], p[1], p[2]}), mn = std::min({p[0], p[1], p[2]});
    return mn >= 215 && mx - mn <= 22;
}

const uint8_t kNone[3] = {1, 1, 1};  // not seen by any screenshot

inline bool isNone(const uint8_t* p) { return p[0] == 1 && p[1] == 1 && p[2] == 1; }
inline bool isEmpty(const uint8_t* p) { return p[0] <= 1 && p[1] <= 1 && p[2] <= 1; }  // black or not seen

struct Box {
    int x, y, w, h;
};

// Connected groups of set pixels (8 neighbours). Calls f(pixel indices, bounding box) for each.
template <class F>
void components(const std::vector<uint8_t>& m, int w, int h, F f) {
    std::vector<uint8_t> seen(m.size(), 0);
    std::vector<int> stack, comp;
    for (int i = 0; i < w * h; ++i) {
        if (!m[i] || seen[i]) continue;
        comp.clear();
        stack.assign(1, i);
        seen[i] = 1;
        Box b = {i % w, i / w, 1, 1};
        int x1 = b.x, y1 = b.y;
        while (!stack.empty()) {
            int k = stack.back();
            stack.pop_back();
            comp.push_back(k);
            int x = k % w, y = k / w;
            b.x = std::min(b.x, x), b.y = std::min(b.y, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
            for (int yy = std::max(0, y - 1); yy <= std::min(h - 1, y + 1); ++yy)
                for (int xx = std::max(0, x - 1); xx <= std::min(w - 1, x + 1); ++xx) {
                    int n = yy * w + xx;
                    if (m[n] && !seen[n]) seen[n] = 1, stack.push_back(n);
                }
        }
        b.w = x1 - b.x + 1, b.h = y1 - b.y + 1;
        f(comp, b);
    }
}

// Max filter over a (2 * rx + 1) x (2 * ry + 1) window.
inline std::vector<uint8_t> dilate(const std::vector<uint8_t>& m, int w, int h, int rx, int ry) {
    std::vector<uint8_t> t(m.size(), 0), r(m.size(), 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (m[y * w + x])
                for (int xx = std::max(0, x - rx); xx <= std::min(w - 1, x + rx); ++xx) t[y * w + xx] = 1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (t[y * w + x])
                for (int yy = std::max(0, y - ry); yy <= std::min(h - 1, y + ry); ++yy) r[yy * w + x] = 1;
    return r;
}

inline bool isExitIcon(const uint8_t* p) { return p[0] >= 230 && p[1] >= 170 && p[2] <= 140; }  // yellow door
inline bool isBlueIcon(const uint8_t* p) { return p[2] >= 210 && p[0] <= 150 && p[1] >= 110; }  // checkpoint, waypoint

// Ring drawn around icons so they are easy to find on the whole map (colours no screenshot pixel is read as).
const uint8_t kExitRing[3] = {255, 190, 0}, kBlueRing[3] = {0, 190, 255};
const int kRing = 16;

inline bool isRing(const uint8_t* p) {
    return (p[0] == kExitRing[0] && p[1] == kExitRing[1] && p[2] == kExitRing[2]) ||
           (p[0] == kBlueRing[0] && p[1] == kBlueRing[1] && p[2] == kBlueRing[2]);
}

}  // namespace outline::detail
