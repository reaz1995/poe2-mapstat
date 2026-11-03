// Outline extraction and joining of map-overlay screenshots (see outline.h).
#include "outline.h"

#include "outline_util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>

namespace outline {

using namespace detail;

namespace {

// A labelled point: the area with its text and icon, and the icon's centre (cx < 0: label without icon).
struct Poi {
    Box zone;  // text, icon and some room around
    Box text;  // the label's text
    int cx = -1, cy = -1;
    bool exit = false;
};

// Labels (white text with black edges) and the icons next to them. Points cut by
// the screenshot edge are left out (another screenshot has them whole).
std::vector<Poi> pointsOfInterest(const Image& img) {
    int w = img.w, h = img.h;
    auto clip = [&](int x0, int y0, int x1, int y1) {
        Box b = {std::max(0, x0), std::max(0, y0), 0, 0};
        b.w = std::min(w, x1) - b.x, b.h = std::min(h, y1) - b.y;
        return b;
    };
    std::vector<Poi> pois;
    // Text: map labels are drawn in white with a black edge around every letter. Map lines (white on the grey
    // ground) have none, so text pixels are white pixels next to a black one.
    auto px = [&](int x, int y) { return &img.px[((size_t)y * w + x) * 3]; };
    std::vector<uint8_t> text((size_t)w * h, 0);
    for (int y = 1; y < h - 1; ++y)
        for (int x = 1; x < w - 1; ++x) {
            if (!isWhite(px(x, y))) continue;  // map lines touching a label may have taken its letters: all white
            bool edge = false;
            for (int dy = -1; dy <= 1 && !edge; ++dy)
                for (int dx = -1; dx <= 1 && !edge; ++dx) {
                    const uint8_t* q = px(x + dx, y + dy);
                    edge = std::max({q[0], q[1], q[2]}) <= 50;
                }
            text[y * w + x] = edge;
        }
    // Words: text pixels joined sideways over the gaps between letters and words. A group can also hold the icon
    // under the label (they touch): its text is the band of rows where the group is wide.
    std::vector<uint8_t> joined(text.size(), 0);
    for (int y = 0; y < h; ++y) {
        int last = -100;
        for (int x = 0; x < w; ++x)
            if (text[y * w + x]) {
                if (x - last <= 7)
                    for (int k = last + 1; k < x; ++k) joined[y * w + k] = 1;
                joined[y * w + x] = 1, last = x;
            }
    }
    components(joined, w, h, [&](const std::vector<int>& comp, const Box& b) {
        if (comp.size() < 40 || b.w < 25 || b.h > 60) return;
        std::vector<int> x0(b.h, w), x1(b.h, -1), n(b.h, 0);
        for (int k : comp) {
            int y = k / w - b.y, x = k % w;
            if (!text[k]) continue;
            x0[y] = std::min(x0[y], x), x1[y] = std::max(x1[y], x), ++n[y];
        }
        auto wide = [&](int y) { return x1[y] - x0[y] >= 22 && n[y] >= 6; };
        for (int y = 0; y < b.h;) {
            if (!wide(y)) {
                ++y;
                continue;
            }
            int y1 = y;
            while (y1 + 1 < b.h && (wide(y1 + 1) || (y1 + 2 < b.h && wide(y1 + 2)))) ++y1;
            Box t = {w, b.y + y, 0, y1 - y + 1};
            int r = -1;
            for (int yy = y; yy <= y1; ++yy)
                if (x1[yy] >= 0) t.x = std::min(t.x, x0[yy]), r = std::max(r, x1[yy]);
            // letters reach a little above and below the wide band (capitals, descenders)
            for (int k = 0; k < 3 && t.y > 0; ++k) {
                bool any = false;
                for (int x = t.x; x <= r; ++x) any |= text[(t.y - 1) * w + x];
                if (!any) break;
                --t.y, ++t.h;
            }
            for (int k = 0; k < 3 && t.y + t.h < h; ++k) {
                bool any = false;
                for (int x = t.x; x <= r; ++x) any |= text[(t.y + t.h) * w + x];
                if (!any) break;
                ++t.h;
            }
            t.w = r - t.x + 1;
            bool cut = t.x <= 2 || t.y <= 2 || t.x + t.w >= w - 2 || t.y + t.h >= h - 2;
            if (t.w >= 30 && t.h >= 8 && t.h <= 18 && !cut)
                pois.push_back({clip(t.x - 24, t.y - 6, t.x + t.w + 24, t.y + t.h + 26), t});
            y = y1 + 1;
        }
    });
    std::vector<uint8_t> icon(w * h);
    for (int i = 0; i < w * h; ++i) icon[i] = isExitIcon(&img.px[i * 3]) || isBlueIcon(&img.px[i * 3]);
    components(icon, w, h, [&](const std::vector<int>& comp, const Box& b) {
        if (comp.size() < 15 || comp.size() > 600 || b.w > 40 || b.h > 40) return;  // sparks, glows
        int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
        int exits = 0;
        for (int k : comp) exits += isExitIcon(&img.px[k * 3]);
        // A map icon sits right under the middle of its label; sparks and glows of the same colours do not.
        for (Poi& p : pois) {
            int mid = p.text.x + p.text.w / 2, bottom = p.text.y + p.text.h;
            if (p.cx >= 0 || std::abs(cx - mid) > 18 || cy < bottom - 4 || cy > bottom + 24) continue;
            p.cx = cx, p.cy = cy, p.exit = exits * 2 > (int)comp.size();
            int x1 = std::max(p.zone.x + p.zone.w, std::min(w, cx + 20)),
                y1 = std::max(p.zone.y + p.zone.h, std::min(h, cy + 20));
            p.zone.x = std::min(p.zone.x, std::max(0, cx - 20));
            p.zone.w = x1 - p.zone.x, p.zone.h = y1 - p.zone.y;
            return;
        }
    });
    return pois;
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
    // Lines are thin: pixels with a full 5 x 5 block around them belong to glows (spells, light), not lines.
    std::vector<uint8_t> inv(r.m.size()), core(r.m.size());
    for (size_t i = 0; i < r.m.size(); ++i) inv[i] = !r.m[i];
    std::vector<uint8_t> nearGap = dilate(inv, r.w, r.h, 2, 2);
    for (size_t i = 0; i < r.m.size(); ++i) core[i] = r.m[i] && !nearGap[i];
    std::vector<uint8_t> out(r.m.size(), 0);
    components(r.m, r.w, r.h, [&](const std::vector<int>& comp, const Box& b) {
        int thick = 0;
        for (int k : comp) thick += core[k];
        // letters and sparks are small (joined letters of a word: low and short); glows are thick
        bool word = b.h <= 14 && b.w <= 80;
        if (comp.size() >= 40 && std::max(b.w, b.h) >= 20 && !word && thick <= 0.05 * comp.size())
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

namespace {

// Same label seen in two pieces (they line up to a pixel or two): kept once, the more complete (wider) one.
void addLabels(std::vector<Label>& into, const std::vector<Label>& more) {
    for (const Label& m : more) {
        Label* same = nullptr;
        for (Label& e : into)
            if (std::abs((e.x + e.w / 2) - (m.x + m.w / 2)) < 30 && std::abs((e.y + e.h / 2) - (m.y + m.h / 2)) < 8)
                same = &e;
        if (!same) {
            into.push_back(m);
            continue;
        }
        if (m.w > same->w) same->x = m.x, same->y = m.y, same->w = m.w, same->h = m.h, same->text = m.text;
        if (same->cx < 0 && m.cx >= 0) same->cx = m.cx, same->cy = m.cy, same->exit = m.exit;
    }
}

void shiftLabels(std::vector<Label>& ls, int dx, int dy) {
    for (Label& l : ls) {
        l.x += dx, l.y += dy;
        if (l.cx >= 0) l.cx += dx, l.cy += dy;
    }
}

// `base` (an outline picture) extended with `add` placed at (dx, dy). A screenshot (`addIsShot`) gives its
// outline and points of interest (their labels go to `found`, in the new picture's coordinates); an outline
// picture is copied where `base` is still empty. `bx, by`: where base's top-left ended up; `ax, ay`: add's.
Image merge(const Image& base, const Image& add, bool addIsShot, int dx, int dy, int& bx, int& by, int& ax, int& ay,
            std::vector<Label>* found = nullptr) {
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
    bx = -x0, by = -y0, ax = ox, ay = oy;
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
    Mask e = edges(add);
    std::vector<Poi> pois = pointsOfInterest(add);
    std::vector<const Poi*> rings;
    for (const Poi& p : pois) {
        if (found) {
            Label l{p.text.x + ox, p.text.y + oy, p.text.w, p.text.h, p.cx < 0 ? -1 : p.cx + ox,
                    p.cx < 0 ? -1 : p.cy + oy, p.exit, {}};
            l.text.resize((size_t)l.w * l.h);
            for (int y = 0; y < l.h; ++y)
                for (int x = 0; x < l.w; ++x) {
                    const uint8_t* s = &add.px[((size_t)(p.text.y + y) * add.w + p.text.x + x) * 3];
                    int mx = std::max({s[0], s[1], s[2]}), mn = std::min({s[0], s[1], s[2]});
                    // letters are white with a dark edge: grey ground and coloured light fade out
                    int v = mx - mn > 60 ? 0 : (mn - 100) * 255 / 155;
                    l.text[(size_t)y * l.w + x] = (uint8_t)std::clamp(v, 0, 255);
                }
            found->push_back(std::move(l));
        }
        // The label as this screenshot shows it, replacing what earlier ones left there (pieces line up to a pixel
        // or two: letters drawn over each other blur). Icons: kept where already set.
        for (int y = p.text.y - 1; y <= p.text.y + p.text.h; ++y)
            for (int x = p.text.x - 1; x <= p.text.x + p.text.w; ++x) {
                if (x < 0 || y < 0 || x >= add.w || y >= add.h) continue;
                const uint8_t* s = &add.px[((size_t)y * add.w + x) * 3];
                int mx = std::max({s[0], s[1], s[2]}), mn = std::min({s[0], s[1], s[2]});
                if (mn >= 170 && mx - mn <= 50) std::copy_n(s, 3, at(x, y));
                else if (!isRing(at(x, y))) std::fill_n(at(x, y), 3, 0);
            }
        if (p.cx >= 0)  // the icon only, not glows of its colour around it
            for (int y = std::max(0, p.cy - 14); y <= std::min(add.h - 1, p.cy + 14); ++y)
                for (int x = std::max(0, p.cx - 14); x <= std::min(add.w - 1, p.cx + 14); ++x) {
                    const uint8_t* s = &add.px[((size_t)y * add.w + x) * 3];
                    if ((isExitIcon(s) || isBlueIcon(s)) && isEmpty(at(x, y))) std::copy_n(s, 3, at(x, y));
                }
        if (p.cx < 0) continue;
        bool ringed = false;  // the icon got its ring from an earlier piece
        for (int y = p.cy - kRing - 6; y <= p.cy + kRing + 6 && !ringed; ++y)
            for (int x = p.cx - kRing - 6; x <= p.cx + kRing + 6 && !ringed; ++x)
                ringed = x >= -ox && y >= -oy && x < out.w - ox && y < out.h - oy && isRing(at(x, y));
        if (!ringed) rings.push_back(&p);
    }
    for (int y = 0; y < add.h; ++y)  // the outline in its own colours (text touching it stays readable)
        for (int x = 0; x < add.w; ++x)
            if (e.m[y * add.w + x] && !isRing(at(x, y))) std::copy_n(&add.px[((size_t)y * add.w + x) * 3], 3, at(x, y));
    for (const Poi* p : rings)
        for (int y = p->cy - kRing - 2; y <= p->cy + kRing + 2; ++y)
            for (int x = p->cx - kRing - 2; x <= p->cx + kRing + 2; ++x) {
                double d = std::hypot(x - p->cx, y - p->cy);
                if (d >= kRing - 1.2 && d <= kRing + 1.2 && x >= -ox && y >= -oy && x < out.w - ox && y < out.h - oy &&
                    isEmpty(at(x, y)))  // under the label text, so its letters stay whole
                    std::copy_n(p->exit ? kExitRing : kBlueRing, 3, at(x, y));
            }
    return out;
}

}  // namespace

Outline render(const Image& shot) {
    Outline o;
    int bx, by, ax, ay;
    std::vector<Label> found;
    o.img = merge(Image{}, shot, true, 0, 0, bx, by, ax, ay, &found);
    addLabels(o.labels, found);
    return o;
}

std::string saveLabels(const std::vector<Label>& labels) {
    std::string s = "# Poe2 | MapStat outline labels v4: x y w h iconX iconY exit text(w*h hex, brightness/16)\n";
    char b[96];
    for (const Label& l : labels) {
        snprintf(b, sizeof b, "%d %d %d %d %d %d %d ", l.x, l.y, l.w, l.h, l.cx, l.cy, l.exit ? 1 : 0);
        s += b;
        for (uint8_t v : l.text) s += "0123456789abcdef"[v >> 4];
        s += '\n';
    }
    return s;
}

std::vector<Label> loadLabels(const std::string& text) {
    std::vector<Label> r;
    size_t i = 0;
    while (i < text.size()) {
        size_t e = text.find('\n', i);
        std::string line = text.substr(i, e == std::string::npos ? std::string::npos : e - i);
        i = e == std::string::npos ? text.size() : e + 1;
        Label l;
        int ex = 0;
        if (!line.empty() && line[0] != '#' &&
            sscanf(line.c_str(), "%d %d %d %d %d %d %d", &l.x, &l.y, &l.w, &l.h, &l.cx, &l.cy, &ex) == 7) {
            l.exit = ex != 0;
            size_t sp = 0;
            for (int k = 0; k < 7 && sp != std::string::npos; ++k) sp = line.find(' ', sp + (k ? 1 : 0));
            if (sp != std::string::npos && line.size() >= sp + 1 + (size_t)l.w * l.h && l.w > 0 && l.h > 0) {
                l.text.resize((size_t)l.w * l.h);
                for (size_t k = 0; k < l.text.size(); ++k) {
                    char c = line[sp + 1 + k];
                    int v = c >= 'a' ? c - 'a' + 10 : c - '0';
                    l.text[k] = (uint8_t)(std::clamp(v, 0, 15) * 17);
                }
            }
            r.push_back(l);
        }
    }
    return r;
}

Mask scaledMask(const Mask& a, double s) {
    Mask r;
    r.w = std::max(1, (int)std::lround(a.w * s)), r.h = std::max(1, (int)std::lround(a.h * s));
    r.m.assign((size_t)r.w * r.h, 0);
    r.cover.assign(r.m.size(), 0);
    for (int y = 0; y < a.h; ++y)  // every set pixel marks where it lands (lines stay unbroken when shrunk)
        for (int x = 0; x < a.w; ++x)
            if (a.m[y * a.w + x]) {
                int X = std::min(r.w - 1, (int)(x * s)), Y = std::min(r.h - 1, (int)(y * s));
                r.m[Y * r.w + X] = 1;
            }
    for (int y = 0; y < r.h; ++y)
        for (int x = 0; x < r.w; ++x) {
            int X = std::min(a.w - 1, (int)((x + 0.5) / s)), Y = std::min(a.h - 1, (int)((y + 0.5) / s));
            r.cover[y * r.w + x] = a.cover[Y * a.w + X];
            if (s > 1 && a.m[Y * a.w + X]) r.m[y * r.w + x] = 1;
        }
    return r;
}

Image scaled(const Image& im, double s) {
    Image r;
    r.w = std::max(1, (int)std::lround(im.w * s)), r.h = std::max(1, (int)std::lround(im.h * s));
    r.px.resize((size_t)r.w * r.h * 3);
    for (int y = 0; y < r.h; ++y)
        for (int x = 0; x < r.w; ++x) {
            // the source pixels this one covers (smaller) or the nearest four (larger), averaged
            double fx0 = x / s, fy0 = y / s, fx1 = (x + 1) / s, fy1 = (y + 1) / s;
            int x0 = (int)fx0, y0 = (int)fy0, x1 = std::max(x0 + 1, (int)std::ceil(fx1)), y1 = std::max(y0 + 1, (int)std::ceil(fy1));
            if (s > 1) x0 = (int)(fx0 + 0.5 / s - 0.5), y0 = (int)(fy0 + 0.5 / s - 0.5), x1 = x0 + 2, y1 = y0 + 2;
            int sum[3] = {0, 0, 0}, n = 0;
            for (int yy = std::max(0, y0); yy < std::min(im.h, y1); ++yy)
                for (int xx = std::max(0, x0); xx < std::min(im.w, x1); ++xx, ++n)
                    for (int c = 0; c < 3; ++c) sum[c] += im.px[((size_t)yy * im.w + xx) * 3 + c];
            for (int c = 0; c < 3; ++c) r.px[((size_t)y * r.w + x) * 3 + c] = (uint8_t)(n ? sum[c] / n : 0);
        }
    return r;
}

// Names the game writes over things in the world (a checkpoint's big "CHECKPOINT" on a black box) are not on the
// map overlay, and they stay put on the screen while the map moves between screenshots. Their letters are about
// twice the size of map labels, with thick strokes: those and the thing under them are blacked out.
void hideWorldLabels(Image& img) {
    int w = img.w, h = img.h;
    auto px = [&](int x, int y) { return &img.px[((size_t)y * w + x) * 3]; };
    std::vector<uint8_t> white((size_t)w * h, 0);
    for (int i = 0; i < w * h; ++i) white[i] = isWhite(&img.px[(size_t)i * 3]);
    auto nearDark = [&](int k) {
        int x = k % w, y = k / w;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                int X = x + dx, Y = y + dy;
                if (X < 0 || Y < 0 || X >= w || Y >= h) continue;
                const uint8_t* q = px(X, Y);
                if (std::max({q[0], q[1], q[2]}) <= 50) return true;
            }
        return false;
    };
    std::vector<Box> big;
    components(white, w, h, [&](const std::vector<int>& comp, const Box& b) {
        if (b.h < 13 || b.h > 34 || b.w > 45 || comp.size() < 30) return;
        int runs = 0, edge = 0;  // stroke width: pixels per horizontal run
        for (int k : comp) runs += !(k % w) || !white[k - 1], edge += nearDark(k);
        if (runs && (double)comp.size() / runs >= 2.5 && edge * 3 >= (int)comp.size()) big.push_back(b);
    });
    std::sort(big.begin(), big.end(), [](const Box& a, const Box& b) { return a.x < b.x; });
    std::vector<bool> used(big.size(), false);
    for (size_t i = 0; i < big.size(); ++i) {
        if (used[i]) continue;
        Box r = big[i];
        int n = 1;
        for (size_t j = i + 1; j < big.size(); ++j) {
            const Box& b = big[j];
            if (used[j] || b.x > r.x + r.w + 30 || std::abs((b.y + b.h / 2) - (r.y + r.h / 2)) > 10) continue;
            int x1 = std::max(r.x + r.w, b.x + b.w), y1 = std::max(r.y + r.h, b.y + b.h);
            r.x = std::min(r.x, b.x), r.y = std::min(r.y, b.y), r.w = x1 - r.x, r.h = y1 - r.y;
            used[j] = true, ++n;
        }
        if (n < 3) continue;
        for (int y = std::max(0, r.y - 12); y < std::min(h, r.y + r.h + 220); ++y)
            for (int x = std::max(0, r.x - 30); x < std::min(w, r.x + r.w + 30); ++x) std::fill_n(px(x, y), 3, 0);
    }
}

Added add(std::vector<Outline>& outlines, const Image& shotRaw) {
    Added r;
    Image shotIn = shotRaw;
    hideWorldLabels(shotIn);
    Image shot = shotIn;
    std::vector<Mask> outE;
    for (auto& o : outlines) outE.push_back(o.empty() ? Mask{} : edges(o.img));
    auto overlapsMask = [&](const Mask& e) {
        std::vector<std::pair<int, Match>> h;
        for (int i = 0; i < (int)outlines.size(); ++i) {
            if (outlines[i].empty()) continue;
            Match m = match(outE[i], e);
            if (m.score >= kMatchScore) h.push_back({i, m});
        }
        return h;
    };
    auto overlaps = [&](const Image& im) { return overlapsMask(edges(im)); };
    std::vector<std::pair<int, Match>> hits = overlaps(shot);
    // The map overlay zoomed in or out since the outline was made: the screenshot is tried at other sizes (1.5x
    // smaller to 2x larger), roughly at half resolution first, and joined at the outline's size.
    if (hits.empty() && std::any_of(outlines.begin(), outlines.end(), [](const Outline& o) { return !o.empty(); })) {
        Mask se = edges(shotIn);
        std::vector<Mask> half;
        for (auto& m : outE) half.push_back(m.m.empty() ? Mask{} : scaledMask(m, 0.5));
        auto bestAt = [&](double sc, bool fine) {
            // made larger, a screenshot is blurred: its outline is made larger instead
            Mask e = !fine ? scaledMask(se, sc * 0.5) : sc > 1 ? scaledMask(se, sc) : edges(scaled(shotIn, sc));
            double b = 0;
            for (size_t i = 0; i < outlines.size(); ++i)
                if (!outlines[i].empty()) b = std::max(b, match(fine ? outE[i] : half[i], e).score);
            return b;
        };
        double bestS = 1, bestScore = 0;
        for (double sc = 0.5; sc <= 1.5; sc *= 1.03) {
            if (std::abs(sc - 1) < 0.02) continue;
            double v = bestAt(sc, false);
            if (v > bestScore) bestScore = v, bestS = sc;
        }
        if (bestScore >= 0.3) {  // then exactly, around the best size
            double best = 0, s0 = bestS;
            for (double f = 0.97; f <= 1.031; f += 0.005) {
                double v = bestAt(s0 * f, true);
                if (v > best) best = v, bestS = s0 * f;
            }
            if (best >= kMatchScore) {
                shot = scaled(shotIn, bestS);
                hits = bestS > 1 ? overlapsMask(scaledMask(se, bestS)) : overlaps(shot);
            }
        }
    }
    if (hits.empty()) {
        outlines.push_back(render(shot));
        r.into = (int)outlines.size() - 1;
        return r;
    }
    r.into = hits[0].first;
    r.overlapped = (int)hits.size();
    Outline& o = outlines[r.into];
    const Match& m0 = hits[0].second;
    int bx, by, ax, ay;
    std::vector<Label> found;
    o.img = merge(o.img, shot, true, m0.dx, m0.dy, bx, by, ax, ay, &found);
    shiftLabels(o.labels, bx, by);
    addLabels(o.labels, found);
    int sx = ax, sy = ay;  // the screenshot's place in the joined picture
    for (size_t k = 1; k < hits.size(); ++k) {  // the screenshot links them: they become one picture
        const Match& m = hits[k].second;
        Outline& other = outlines[hits[k].first];
        o.img = merge(o.img, other.img, false, sx - m.dx, sy - m.dy, bx, by, ax, ay);
        shiftLabels(o.labels, bx, by);
        shiftLabels(other.labels, ax, ay);
        addLabels(o.labels, other.labels);
        sx += bx, sy += by;
        other = Outline{};
    }
    return r;
}

}  // namespace outline
