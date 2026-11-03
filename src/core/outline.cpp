// Outline extraction and joining of map-overlay screenshots (see outline.h).
#include "outline.h"

#include <algorithm>
#include <cstdlib>

namespace outline {

namespace {

const uint8_t kLine[3] = {220, 220, 255};  // outline colour in rendered pictures (passes isLine)

// The overlay outline: light blue-white, blue at full strength.
bool isLine(const uint8_t* p) {
    int r = p[0], g = p[1], b = p[2];
    return b >= 235 && std::abs(r - g) <= 18 && b - r >= 12 && b - r <= 75 && r >= 170;
}

bool isWhite(const uint8_t* p) {  // label text
    int mx = std::max({p[0], p[1], p[2]}), mn = std::min({p[0], p[1], p[2]});
    return mn >= 215 && mx - mn <= 22;
}

bool isIcon(const uint8_t* p) {  // label text, yellow exits, blue checkpoints / waypoints
    int r = p[0], g = p[1], b = p[2];
    return isWhite(p) || (r >= 200 && g >= 150 && b <= 110) || (b >= 200 && r <= 170 && g >= 140);
}

const uint8_t kNone[3] = {1, 1, 1};  // not seen by any screenshot

bool isNone(const uint8_t* p) { return p[0] == 1 && p[1] == 1 && p[2] == 1; }
bool isEmpty(const uint8_t* p) { return p[0] <= 1 && p[1] <= 1 && p[2] <= 1; }  // black or not seen
bool isLineColour(const uint8_t* p) { return p[0] == kLine[0] && p[1] == kLine[1] && p[2] == kLine[2]; }

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
std::vector<uint8_t> dilate(const std::vector<uint8_t>& m, int w, int h, int rx, int ry) {
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

// Labels (white text lines) and the pixels to keep around them: the text and the icon next to it.
// Labels cut by the screenshot edge are left out (another screenshot has them whole).
void pointsOfInterest(const Image& img, std::vector<Box>& zones, std::vector<uint8_t>& keep) {
    int w = img.w, h = img.h;
    std::vector<uint8_t> white(w * h);
    for (int i = 0; i < w * h; ++i) white[i] = isWhite(&img.px[i * 3]);
    std::vector<uint8_t> joined = dilate(white, w, h, 4, 1);  // letters of one label join into a line
    zones.clear();
    components(joined, w, h, [&](const std::vector<int>& comp, const Box& b) {
        int text = 0;
        for (int k : comp) text += white[k];
        double fill = (double)text / (b.w * b.h);
        bool label = b.h >= 9 && b.h <= 30 && b.w >= 35 && b.w >= 2.2 * b.h && fill >= 0.10 && fill <= 0.5;
        bool cut = b.x <= 2 || b.y <= 2 || b.x + b.w >= w - 2 || b.y + b.h >= h - 2;
        if (!label || cut) return;
        Box z = {std::max(0, b.x - 24), std::max(0, b.y - 6), 0, 0};  // the icon sits next to or under the text
        z.w = std::min(w, b.x + b.w + 24) - z.x;
        z.h = std::min(h, b.y + b.h + 26) - z.y;
        zones.push_back(z);
    });
    keep.assign(w * h, 0);
    for (const Box& z : zones)
        for (int y = z.y; y < z.y + z.h; ++y)
            for (int x = z.x; x < z.x + z.w; ++x) keep[y * w + x] = isIcon(&img.px[(y * w + x) * 3]);
}

}  // namespace

Mask edges(const Image& img) {
    Mask r;
    r.w = img.w, r.h = img.h;
    r.m.assign((size_t)img.w * img.h, 0);
    r.cover.assign(r.m.size(), 1);
    for (int i = 0; i < img.w * img.h; ++i) {
        r.m[i] = isLine(&img.px[i * 3]);
        r.cover[i] = !isNone(&img.px[i * 3]);
    }
    std::vector<uint8_t> out(r.m.size(), 0);
    components(r.m, r.w, r.h, [&](const std::vector<int>& comp, const Box&) {
        if (comp.size() >= 40)  // specks: sparks, text edges
            for (int k : comp) out[k] = 1;
    });
    r.m.swap(out);
    return r;
}

Match match(const Mask& a, const Mask& b) {
    Match best;
    if (a.m.empty() || b.m.empty()) return best;
    // 1. rough: every pair of outline cells (4 x 4 pixels) votes for the shift that puts one on the other
    const int S = 4;
    auto cells = [&](const Mask& m, int& cw, int& ch) {
        cw = (m.w + S - 1) / S, ch = (m.h + S - 1) / S;
        std::vector<uint8_t> c((size_t)cw * ch, 0);
        for (int y = 0; y < m.h; ++y)
            for (int x = 0; x < m.w; ++x)
                if (m.m[y * m.w + x]) c[(y / S) * cw + x / S] = 1;
        std::vector<int> pts;
        for (int i = 0; i < cw * ch; ++i)
            if (c[i]) pts.push_back(i);
        return pts;
    };
    int aw, ah, bw, bh;
    std::vector<int> pa = cells(a, aw, ah), pb = cells(b, bw, bh);
    if (pa.empty() || pb.empty()) return best;
    int VW = aw + bw, VH = ah + bh;
    std::vector<int> votes((size_t)VW * VH, 0);
    for (int q : pb) {
        int ox = bw - q % bw, oy = bh - q / bw;
        for (int p : pa) ++votes[(p / aw + oy) * VW + p % aw + ox];
    }
    std::vector<int> sm(votes.size(), 0);  // 3 x 3 sum: a shift between cells still gets its votes
    for (int y = 1; y < VH - 1; ++y)
        for (int x = 1; x < VW - 1; ++x) {
            int s = 0;
            for (int yy = -1; yy <= 1; ++yy)
                for (int xx = -1; xx <= 1; ++xx) s += votes[(y + yy) * VW + x + xx];
            sm[y * VW + x] = s;
        }

    // 2. exact: around the best few shifts, count outline pixels of b that land on (near) outline of a
    // Only the parts both pictures show are compared.
    std::vector<uint8_t> near = dilate(a.m, a.w, a.h, 2, 2);
    std::vector<int> ap, bp;
    for (int i = 0; i < a.w * a.h; ++i)
        if (a.m[i]) ap.push_back(i);
    for (int i = 0; i < b.w * b.h; ++i)
        if (b.m[i]) bp.push_back(i);
    std::vector<uint8_t> nearB = dilate(b.m, b.w, b.h, 2, 2);
    // Effects (fire, spells) hide parts of the outline in one picture or the other, so both directions are
    // counted and the better one is taken: share of b's outline lying on a's, and of a's lying on b's.
    auto score = [&](int dx, int dy) {
        int x0 = std::max(0, dx), y0 = std::max(0, dy), x1 = std::min(a.w, dx + b.w), y1 = std::min(a.h, dy + b.h);
        if (x1 <= x0 || y1 <= y0) return 0.0;
        int na = 0, nb = 0, hitA = 0, hitB = 0;
        for (int i : bp) {
            int x = i % b.w + dx, y = i / b.w + dy;
            if (x < x0 || x >= x1 || y < y0 || y >= y1 || !a.cover[y * a.w + x]) continue;
            ++nb;
            hitB += near[y * a.w + x];
        }
        for (int i : ap) {
            int x = i % a.w, y = i / a.w;
            if (x < x0 || x >= x1 || y < y0 || y >= y1) continue;
            int k = (y - dy) * b.w + x - dx;
            if (!b.cover[k]) continue;
            ++na;
            hitA += nearB[k];
        }
        if (std::min(na, nb) < 400) return 0.0;  // too little overlap to tell
        return std::max((double)hitA / na, (double)hitB / nb);
    };
    for (int peak = 0; peak < 8; ++peak) {
        int bi = (int)(std::max_element(sm.begin(), sm.end()) - sm.begin());
        if (sm[bi] <= 0) break;
        int px = bi % VW, py = bi / VW;
        for (int y = std::max(0, py - 3); y <= std::min(VH - 1, py + 3); ++y)
            for (int x = std::max(0, px - 3); x <= std::min(VW - 1, px + 3); ++x) sm[y * VW + x] = 0;
        int cx = (px - bw) * S, cy = (py - bh) * S;
        for (int dy = cy - S; dy <= cy + S; ++dy)
            for (int dx = cx - S; dx <= cx + S; ++dx) {
                double s = score(dx, dy);
                if (s > best.score) best = {s, dx, dy};
            }
    }
    return best;
}

Image merge(const Image& base, const Image& add, bool addIsShot, int dx, int dy) {
    if (base.empty()) dx = dy = 0;
    int x0 = base.empty() ? 0 : std::min(0, dx), y0 = base.empty() ? 0 : std::min(0, dy);
    int x1 = std::max(base.w, dx + add.w), y1 = std::max(base.h, dy + add.h);
    Image out;
    out.w = x1 - x0, out.h = y1 - y0;
    out.px.resize((size_t)out.w * out.h * 3);
    for (size_t i = 0; i < out.px.size(); i += 3) std::copy_n(kNone, 3, &out.px[i]);
    for (int y = 0; y < base.h; ++y)
        std::copy_n(&base.px[(size_t)y * base.w * 3], base.w * 3, &out.px[((size_t)(y - y0) * out.w - x0) * 3]);
    int ox = dx - x0, oy = dy - y0;  // add's top-left in out
    auto at = [&](int x, int y) { return &out.px[((size_t)(y + oy) * out.w + x + ox) * 3]; };
    if (!addIsShot) {
        for (int y = 0; y < add.h; ++y)
            for (int x = 0; x < add.w; ++x) {
                const uint8_t* s = &add.px[((size_t)y * add.w + x) * 3];
                uint8_t* d = at(x, y);
                if (!isNone(s) && (isNone(d) || (isEmpty(d) && !isEmpty(s)))) std::copy_n(s, 3, d);
            }
        return out;
    }
    for (int y = 0; y < add.h; ++y)  // the screenshot's area is now seen
        for (int x = 0; x < add.w; ++x)
            if (isNone(at(x, y))) std::fill_n(at(x, y), 3, 0);
    std::vector<Box> zones;
    std::vector<uint8_t> keep;
    pointsOfInterest(add, zones, keep);
    for (const Box& z : zones) {  // a label already in the picture is not drawn twice (slightly shifted)
        int mine = 0, there = 0;
        for (int y = z.y; y < z.y + z.h; ++y)
            for (int x = z.x; x < z.x + z.w; ++x) {
                mine += keep[y * add.w + x];
                const uint8_t* d = at(x, y);
                there += !isEmpty(d) && !isLineColour(d);
            }
        if (there > 0.3 * mine) continue;
        for (int y = z.y; y < z.y + z.h; ++y)
            for (int x = z.x; x < z.x + z.w; ++x)
                if (keep[y * add.w + x]) std::copy_n(&add.px[((size_t)y * add.w + x) * 3], 3, at(x, y));
    }
    Mask e = edges(add);
    for (int y = 0; y < add.h; ++y)
        for (int x = 0; x < add.w; ++x)
            if (e.m[y * add.w + x]) std::copy_n(kLine, 3, at(x, y));
    return out;
}

Image render(const Image& shot) { return merge(Image{}, shot, true, 0, 0); }

Added add(std::vector<Image>& outlines, const Image& shot) {
    Added r;
    Mask e = edges(shot);
    std::vector<std::pair<int, Match>> hits;
    for (int i = 0; i < (int)outlines.size(); ++i) {
        if (outlines[i].empty()) continue;
        Match m = match(edges(outlines[i]), e);
        if (m.score >= kMatchScore) hits.push_back({i, m});
    }
    if (hits.empty()) {
        outlines.push_back(render(shot));
        r.into = (int)outlines.size() - 1;
        return r;
    }
    r.into = hits[0].first;
    r.overlapped = (int)hits.size();
    Image& img = outlines[r.into];
    const Match& m0 = hits[0].second;
    img = merge(img, shot, true, m0.dx, m0.dy);
    int sx = std::max(0, m0.dx), sy = std::max(0, m0.dy);  // the screenshot's place in the joined picture
    for (size_t k = 1; k < hits.size(); ++k) {  // the screenshot links them: they become one picture
        const Match& m = hits[k].second;
        int px = sx - m.dx, py = sy - m.dy;  // that outline's place in the joined picture
        img = merge(img, outlines[hits[k].first], false, px, py);
        sx -= std::min(0, px), sy -= std::min(0, py);
        outlines[hits[k].first] = Image{};
    }
    return r;
}

}  // namespace outline
