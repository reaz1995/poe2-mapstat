// Points graph of a zone (see outline_graph.h).
#include "outline_graph.h"

#include <climits>
#include <cmath>
#include <functional>
#include <map>

#include "outline_util.h"

namespace outline {

using namespace detail;

namespace {

// A label of a run: where its point is and its text (bright pixels in the label box, 1 = text).
struct Mark {
    double x = 0, y = 0;  // the icon under it, else just below the text
    const Label* lab = nullptr;
    std::vector<uint8_t> text;
    int n = 0;  // text pixels
    int cls = -1;  // same text = same class, in every run
    double edge = 0;  // how far it is from the outside of the zone (px)
};

// Distance (px, in 4 px cells) of every cell from the outside of the zone: the ground beyond the outermost map
// lines, reached from the picture's border without crossing a line.
struct Outside {
    int cw = 0, ch = 0;
    std::vector<int> d;
    double at(double x, double y) const {
        int cx = std::clamp((int)(x / 4), 0, cw - 1), cy = std::clamp((int)(y / 4), 0, ch - 1);
        return d[cy * cw + cx] * 4.0;
    }
};

Outside outside(const Image& im) {
    const int S = 4;
    Outside o;
    o.cw = (im.w + S - 1) / S, o.ch = (im.h + S - 1) / S;
    std::vector<uint8_t> line((size_t)o.cw * o.ch, 0);
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            const uint8_t* p = &im.px[((size_t)y * im.w + x) * 3];
            if (std::min({p[0], p[1], p[2]}) >= 150) line[(y / S) * o.cw + x / S] = 1;
        }
    std::vector<uint8_t> wall = line;  // lines thickened, so small gaps in them do not let the outside in
    for (int y = 0; y < o.ch; ++y)
        for (int x = 0; x < o.cw; ++x)
            if (line[y * o.cw + x])
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx) {
                        int X = x + dx, Y = y + dy;
                        if (X >= 0 && Y >= 0 && X < o.cw && Y < o.ch) wall[Y * o.cw + X] = 1;
                    }
    o.d.assign(wall.size(), -1);
    std::vector<int> q;
    for (int y = 0; y < o.ch; ++y)
        for (int x = 0; x < o.cw; ++x)
            if ((x == 0 || y == 0 || x == o.cw - 1 || y == o.ch - 1) && !wall[y * o.cw + x])
                o.d[y * o.cw + x] = 0, q.push_back(y * o.cw + x);
    for (size_t i = 0; i < q.size(); ++i) {  // the outside
        int x = q[i] % o.cw, y = q[i] / o.cw;
        for (auto [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
            int X = x + dx, Y = y + dy;
            if (X < 0 || Y < 0 || X >= o.cw || Y >= o.ch || wall[Y * o.cw + X] || o.d[Y * o.cw + X] >= 0) continue;
            o.d[Y * o.cw + X] = 0, q.push_back(Y * o.cw + X);
        }
    }
    for (size_t i = 0; i < q.size(); ++i) {  // then how far from it, through everything
        int x = q[i] % o.cw, y = q[i] / o.cw;
        for (auto [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
            int X = x + dx, Y = y + dy;
            if (X < 0 || Y < 0 || X >= o.cw || Y >= o.ch || o.d[Y * o.cw + X] >= 0) continue;
            o.d[Y * o.cw + X] = o.d[q[i]] + 1, q.push_back(Y * o.cw + X);
        }
    }
    for (int& v : o.d) v = std::max(v, 0);
    return o;
}

bool isText(const uint8_t* p) {
    int mx = std::max({p[0], p[1], p[2]}), mn = std::min({p[0], p[1], p[2]});
    return mn >= 150 && mx - mn <= 60 && !isRing(p);
}

Mark mark(const Outline& o, const Label& l) {
    Mark m;
    m.lab = &l;
    m.x = l.cx >= 0 ? l.cx : l.x + l.w / 2.0;
    m.y = l.cy >= 0 ? l.cy : l.y + l.h + 8;
    if (l.text.size() == (size_t)l.w * l.h) {
        m.text = l.text;
    } else {  // older outlines: what is left of the letters in the outline picture
        m.text.assign((size_t)l.w * l.h, 0);
        for (int y = 0; y < l.h; ++y)
            for (int x = 0; x < l.w; ++x) {
                int X = l.x + x, Y = l.y + y;
                if (X >= 0 && Y >= 0 && X < o.img.w && Y < o.img.h && isText(&o.img.px[((size_t)Y * o.img.w + X) * 3]))
                    m.text[y * l.w + x] = 255;
            }
    }
    for (uint8_t v : m.text) m.n += v >= 128;
    return m;
}

// How alike two labels' text is (-1..1: correlation of the letters' brightness), over the part where they overlap,
// best over sideways / small up-down shifts. A label partly hidden in one run still matches its whole self.
double labelLike(const Mark& a, const Mark& b) {
    int aw = a.lab->w, ah = a.lab->h, bw = b.lab->w, bh = b.lab->h;
    if (std::abs(ah - bh) > 5 || a.n < 20 || b.n < 20) return 0;
    if (std::min(aw, bw) < 0.55 * std::max(aw, bw)) return 0;  // much shorter: another word
    int span = std::abs(aw - bw) + 3;
    double best = 0;
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -span; dx <= span; ++dx) {  // b's (x, y) lies at a's (x + dx, y + dy)
            int x0 = std::max(0, dx), x1 = std::min(aw, bw + dx);
            if (x1 - x0 < 25 || x1 - x0 < 0.75 * std::min(aw, bw)) continue;
            double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
            int cnt = 0;
            for (int y = 0; y < ah; ++y) {
                int by = y - dy;
                for (int x = x0; x < x1; ++x) {
                    double pa = a.text[y * aw + x];
                    double pb = by >= 0 && by < bh ? b.text[by * bw + x - dx] : 0;
                    sa += pa, sb += pb, saa += pa * pa, sbb += pb * pb, sab += pa * pb, ++cnt;
                }
            }
            double va = saa - sa * sa / cnt, vb = sbb - sb * sb / cnt;
            if (va <= 0 || vb <= 0) continue;
            best = std::max(best, (sab - sa * sb / cnt) / std::sqrt(va * vb));
        }
    return best;
}

// The 8 ways a map can be turned (by quarter turns) and mirrored, as 2 x 2 matrices.
const int kOrient[8][4] = {{1, 0, 0, 1},  {0, -1, 1, 0}, {-1, 0, 0, -1}, {0, 1, -1, 0},
                           {-1, 0, 0, 1}, {0, 1, 1, 0},  {1, 0, 0, -1},  {0, -1, -1, 0}};
const int* orientOf(int o) { return kOrient[o]; }

}  // namespace

Graph pointsGraph(const std::vector<Outline>& outlines, double minFit) {
    Graph g;
    g.runs = (int)outlines.size();
    g.fits.assign(outlines.size(), -1);
    std::vector<std::vector<Mark>> marks(outlines.size());
    for (size_t r = 0; r < outlines.size(); ++r) {
        Outside out = outside(outlines[r].img);
        for (const Label& l : outlines[r].labels) {
            Mark m = mark(outlines[r], l);
            m.edge = out.at(m.x, m.y);
            // words: a row across the letters crosses many strokes (a bit of map line or a curve, a few)
            int best = 0;
            for (int y = l.h / 4; y < l.h - l.h / 4; ++y) {
                int strokes = 0;
                bool was = false;
                for (int x = 0; x < l.w; ++x) {
                    bool on = m.text[y * l.w + x] >= 128;
                    strokes += on && !was, was = on;
                }
                best = std::max(best, strokes);
            }
            if (m.n >= 20 && best >= 6) marks[r].push_back(std::move(m));
        }
    }

    // 1. the same text in different runs = the same point (groups joined through any similar pair)
    std::vector<std::pair<int, int>> ids;  // (run, mark)
    for (int r = 0; r < (int)marks.size(); ++r)
        for (int k = 0; k < (int)marks[r].size(); ++k) ids.push_back({r, k});
    std::vector<int> parent(ids.size());
    for (size_t i = 0; i < ids.size(); ++i) parent[i] = (int)i;
    std::function<int(int)> root = [&](int i) { return parent[i] == i ? i : parent[i] = root(parent[i]); };
    for (size_t i = 0; i < ids.size(); ++i)
        for (size_t j = i + 1; j < ids.size(); ++j)
            if (labelLike(marks[ids[i].first][ids[i].second], marks[ids[j].first][ids[j].second]) >= 0.8)
                parent[root((int)i)] = root((int)j);
    std::map<int, int> clsOf;
    for (size_t i = 0; i < ids.size(); ++i) {
        int c = root((int)i);
        if (!clsOf.count(c)) clsOf[c] = (int)clsOf.size();
        marks[ids[i].first][ids[i].second].cls = clsOf[c];
    }
    int nCls = (int)clsOf.size();
    for (auto& run : marks)  // the same label found twice in one run (pieces joined a few pixels apart): once
        for (size_t i = 0; i < run.size(); ++i)
            for (size_t j = run.size(); j-- > i + 1;)
                if (run[j].cls == run[i].cls && std::hypot(run[j].x - run[i].x, run[j].y - run[i].y) < 40) {
                    if (run[j].lab->w > run[i].lab->w) std::swap(run[i], run[j]);
                    run.erase(run.begin() + j);
                }
    std::vector<std::map<int, int>> perRun(nCls);  // class -> run -> how many in that run
    for (int r = 0; r < (int)marks.size(); ++r)
        for (Mark& m : marks[r]) ++perRun[m.cls][r];
    // a named place is there once per run; "Checkpoint" (and other labels several runs repeat) are not places
    std::vector<bool> repeated(nCls, false);  // several in a run: checkpoints
    for (int c = 0; c < nCls; ++c)
        for (auto& kv : perRun[c]) repeated[c] = repeated[c] || kv.second > 1;
    // Checkpoints: the label most often repeated in a run. Other repeated labels (several Obelisks of Rust in a
    // zone, say) are places that come several times.
    int checkCls = -1, checkN = 0;
    for (int c = 0; c < nCls; ++c) {
        int n = 0;
        for (auto& kv : perRun[c]) n += kv.second;
        if (repeated[c] && n > checkN) checkN = n, checkCls = c;
    }
    std::vector<bool> isCheck(nCls, false);  // also a checkpoint label with a bit of map line in its box, say
    if (checkCls >= 0) isCheck[checkCls] = true;
    for (auto& ra : marks)
        for (const Mark& a : ra) {
            if (isCheck[a.cls] || checkCls < 0) continue;
            for (auto& rb : marks)
                for (const Mark& b : rb)
                    if (b.cls == checkCls && labelLike(a, b) >= 0.65) isCheck[a.cls] = true;
        }
    auto once = [&](int c) { return !repeated[c] && !isCheck[c]; };
    auto find = [&](int r, int c) -> const Mark* {
        for (const Mark& m : marks[r])
            if (m.cls == c) return &m;
        return nullptr;
    };
    std::vector<int> places;  // named places seen in 2+ runs, most often seen first
    for (int c = 0; c < nCls; ++c)
        if (once(c) && perRun[c].size() >= 2) places.push_back(c);
    std::sort(places.begin(), places.end(), [&](int a, int b) { return perRun[a].size() > perRun[b].size(); });
    if (places.empty()) return g;
    // Phase 1: entrances (the ways in and out of the zone) lie on its outer edge and stay put; the places inside
    // swap between a few spots from run to run.
    std::vector<bool> entrance(nCls, false);
    std::vector<int> entrances;
    for (int c : places) {
        std::vector<double> e;
        for (auto& run : marks)
            for (const Mark& m : run)
                if (m.cls == c) e.push_back(m.edge);
        std::sort(e.begin(), e.end());
        if (e[e.size() / 2] <= 60) entrance[c] = true, entrances.push_back(c);
    }

    struct Place {
        int run;
        double sr, si;  // squash undone, then x' = (sr + i si) * (x - ax) + refA, squash again
        double tx = 0, ty = 0;  // then moved by this much (fitting the run's map onto the reference's)
        int m[4] = {1, 0, 0, 1};  // turned / mirrored first (the game turns and mirrors some layouts)
        double fit = 1;  // share of the run's map walls on the reference's
    };
    const double k = 0.82;  // the overlay map is seen at an angle: drawn ~0.82 as high as wide
    struct Align {
        int A = -1, B = -1, ref = -1;
        std::vector<Place> placed;
    };
    auto mapWith = [&](const Align& al, const Place& p, double x, double y, double& ox2, double& oy2) {
        const Mark* a = find(p.run, al.A);
        const Mark* ra = find(al.ref, al.A);
        double ux = x - a->x, uy = (y - a->y) / k;
        double dx = p.m[0] * ux + p.m[1] * uy, dy = p.m[2] * ux + p.m[3] * uy;
        ox2 = ra->x + p.sr * dx - p.si * dy + p.tx;
        oy2 = ra->y + (p.si * dx + p.sr * dy) * k + p.ty;
    };
    // Where each run's map was seen (16 px cells): lined up right, the runs' maps cover about the same ground (a
    // zone keeps its overall shape between instances, while some places in it move).
    const int C = 16;
    std::vector<std::vector<uint8_t>> seen(outlines.size());
    for (size_t r = 0; r < outlines.size(); ++r) {
        const Image& im = outlines[r].img;
        int cw = (im.w + C - 1) / C, ch = (im.h + C - 1) / C;
        seen[r].assign((size_t)cw * ch, 0);
        for (int y = 0; y < im.h; y += 2)
            for (int x = 0; x < im.w; x += 2)
                if (!isNone(&im.px[((size_t)y * im.w + x) * 3])) seen[r][(y / C) * cw + x / C] = 1;
    }
    auto seenAt = [&](int r, double x, double y) {
        const Image& im = outlines[r].img;
        int cw = (im.w + C - 1) / C, ch = (im.h + C - 1) / C, cx = (int)std::floor(x / C), cy = (int)std::floor(y / C);
        return cx >= 0 && cy >= 0 && cx < cw && cy < ch && seen[r][cy * cw + cx];
    };
    auto alignOn = [&](int A, int B, int forceRef = -1) {
        Align al;
        al.A = A, al.B = B, al.ref = forceRef;
        long refScore = forceRef >= 0 ? LONG_MAX : -1;  // the run with A (and B) and the most map seen: drawn
        for (auto& kv : perRun[A]) {
            int r = kv.first;
            long score = (B >= 0 && find(r, B) ? 1L << 30 : 0) + std::count(seen[r].begin(), seen[r].end(), 1);
            if (score > refScore) refScore = score, al.ref = r;
        }
        const Mark* refA = find(al.ref, A);
        const Mark* refB = B >= 0 ? find(al.ref, B) : nullptr;
        for (int r = 0; r < (int)marks.size(); ++r) {
            const Mark* a = find(r, A);
            if (!a) continue;
            Place p = {r, 1, 0, 0, 0, {1, 0, 0, 1}, 1};
            const Mark* b = B >= 0 ? find(r, B) : nullptr;
            if (b && refB && r != al.ref) {
                double vx = b->x - a->x, vy = (b->y - a->y) / k, wx = refB->x - refA->x, wy = (refB->y - refA->y) / k;
                double d = vx * vx + vy * vy;
                double sr = (wx * vx + wy * vy) / d, si = (wy * vx - wx * vy) / d;  // complex w / v
                double scale = std::hypot(sr, si), turn = std::atan2(si, sr) * 180 / 3.14159265;
                if (scale > 0.7 && scale < 1.4 && std::abs(turn) < 25) p.sr = sr, p.si = si;
            }
            al.placed.push_back(p);
        }
        return al;
    };
    // How well an alignment works: runs lined up, how much their maps cover the reference's, then how many pairs of
    // the other places land close together.
    auto agree = [&](const Align& al) {
        double cover = 0;
        for (auto& p : al.placed) {
            if (p.run == al.ref) continue;
            const Image& im = outlines[p.run].img;
            int cw = (im.w + C - 1) / C, ch = (im.h + C - 1) / C, both = 0, all = 0;
            for (int cy = 0; cy < ch; ++cy)
                for (int cx = 0; cx < cw; ++cx) {
                    if (!seen[p.run][cy * cw + cx]) continue;
                    double x, y;
                    mapWith(al, p, cx * C + C / 2.0, cy * C + C / 2.0, x, y);
                    ++all, both += seenAt(al.ref, x, y);
                }
            if (all) cover += (double)both / all;
        }
        int pairs = 0;
        for (int c : places) {
            if (c == al.A || c == al.B) continue;
            std::vector<std::pair<double, double>> at;
            for (auto& p : al.placed)
                if (const Mark* m = find(p.run, c)) {
                    double x, y;
                    mapWith(al, p, m->x, m->y, x, y);
                    at.push_back({x, y});
                }
            for (size_t i = 0; i < at.size(); ++i)
                for (size_t j = i + 1; j < at.size(); ++j)
                    pairs += std::hypot(at[i].first - at[j].first, at[i].second - at[j].second) <= 120;
        }
        return al.placed.size() * 1000.0 + cover * 100 + pairs;
    };
    // 2. runs are lined up on an entrance (A) and, where a run has it too, on another one far from it (B): with
    // both, the run is also scaled / tilted a little so A and B land on the reference's; with A only, it is
    // shifted. The pair that makes the runs' maps cover the same ground best is taken. (No entrance found: any
    // named place will do.)
    Align best;
    double bestScore = -1;
    const std::vector<int>& anchors = entrances.empty() ? places : entrances;  // lined up on the entrances
    for (int A : anchors)
        for (int B = -1; B < nCls; ++B) {
            if (B == A || (B >= 0 && std::find(anchors.begin(), anchors.end(), B) == anchors.end())) continue;
            if (B >= 0) {
                bool far = false;
                for (auto& kv : perRun[B]) {
                    const Mark *a = find(kv.first, A), *b = find(kv.first, B);
                    far |= a && b && std::hypot(a->x - b->x, a->y - b->y) > 600;
                }
                if (!far) continue;
            }
            Align al = alignOn(A, B);
            double sc = agree(al) + (B >= 0) * 0.5;
            if (sc > bestScore) bestScore = sc, best = std::move(al);
        }
    // Lined up on an entrance, a run can still be turned or mirrored (the game does that to some layouts), or sit a
    // little off: each run is tried in the 8 ways a map can be turned / mirrored, sized +-8 % and moved up to 70 px
    // (the entrance stays put), and kept the way its map walls lie best on the reference's, when clearly better.
    auto refine = [&](Align& al) {  // returns how many runs were turned each way
        std::vector<int> turned(8, 0);
        const int Cs = 8;
        const Image& rimg = outlines[al.ref].img;
        int cw = (rimg.w + Cs - 1) / Cs, ch = (rimg.h + Cs - 1) / Cs;
        std::vector<uint8_t> wall((size_t)cw * ch, 0);
        Mask re = edges(rimg);
        for (int y = 0; y < rimg.h; ++y)
            for (int x = 0; x < rimg.w; ++x)
                if (re.m[y * rimg.w + x])
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            int X = x / Cs + dx, Y = y / Cs + dy;
                            if (X >= 0 && Y >= 0 && X < cw && Y < ch) wall[Y * cw + X] = 1;
                        }
        auto onWall = [&](double x, double y) {
            int X = (int)std::floor(x / Cs), Y = (int)std::floor(y / Cs);
            return X >= 0 && Y >= 0 && X < cw && Y < ch && wall[Y * cw + X];
        };
        const Mark* ra = find(al.ref, al.A);
        for (Place& p : al.placed) {
            if (p.run == al.ref) continue;
            const Image& im = outlines[p.run].img;
            const Mark* a = find(p.run, al.A);
            Mask e = edges(im);
            std::vector<std::pair<double, double>> pts;  // the run's walls, one per 8 px cell, unsquashed, from A
            int icw = (im.w + Cs - 1) / Cs;
            std::vector<uint8_t> seenCell((size_t)icw * ((im.h + Cs - 1) / Cs), 0);
            for (int y = 0; y < im.h; ++y)
                for (int x = 0; x < im.w; ++x)
                    if (e.m[y * im.w + x] && !seenCell[(y / Cs) * icw + x / Cs]) {
                        seenCell[(y / Cs) * icw + x / Cs] = 1;
                        pts.push_back({x - a->x, (y - a->y) / k});
                    }
            if (pts.size() < 50) continue;
            struct Try {
                int o = 0;
                double s = 1, tx = 0, ty = 0;
                int n = -1;
            };
            // the other named places both runs have (an exit next to the entrance, say) must land on the
            // reference's too: each that does counts as much as a tenth of the walls
            std::vector<std::pair<std::pair<double, double>, std::pair<double, double>>> pairs;  // (run, ref)
            for (int c : places) {
                const Mark *x = find(p.run, c), *y = find(al.ref, c);
                if (c != al.A && x && y) pairs.push_back({{x->x - a->x, (x->y - a->y) / k}, {y->x, y->y}});
            }
            const int bonus = (int)pts.size() / 10;
            auto score = [&](const Try& t) {
                const int* m = kOrient[t.o];
                double sr = p.sr * t.s, si = p.si * t.s;
                auto at = [&](double ux, double uy, double& X, double& Y) {
                    double dx = m[0] * ux + m[1] * uy, dy = m[2] * ux + m[3] * uy;
                    X = ra->x + sr * dx - si * dy + t.tx, Y = ra->y + (si * dx + sr * dy) * k + t.ty;
                };
                int n = 0;
                double X, Y;
                for (auto& q : pts) at(q.first, q.second, X, Y), n += onWall(X, Y);
                for (auto& pr : pairs) {
                    at(pr.first.first, pr.first.second, X, Y);
                    if (std::hypot(X - pr.second.first, Y - pr.second.second) < 90) n += bonus;
                }
                return n;
            };
            Try base{0, 1, 0, 0, 0};
            base.n = score(base);
            Try bestT = base, bestId = base;  // best of all, best not turned
            for (int o = 0; o < 8; ++o)
                for (double sc = 0.92; sc <= 1.0801; sc += 0.04)
                    for (int ty = -64; ty <= 64; ty += 8)
                        for (int tx = -64; tx <= 64; tx += 8) {
                            Try t{o, sc, (double)tx, (double)ty, 0};
                            t.n = score(t);
                            if (t.n > bestT.n) bestT = t;
                            if (o == 0 && t.n > bestId.n) bestId = t;
                        }
            for (Try* t : {&bestT, &bestId}) {  // finer, around the best
                Try c = *t;
                for (double sc = c.s - 0.02; sc <= c.s + 0.0201; sc += 0.01)
                    for (double ty = c.ty - 6; ty <= c.ty + 6; ty += 2)
                        for (double tx = c.tx - 6; tx <= c.tx + 6; tx += 2) {
                            Try u{c.o, sc, tx, ty, 0};
                            u.n = score(u);
                            if (u.n > t->n) *t = u;
                        }
            }
            // turning a map needs clear evidence; moving it a little less so
            Try use = bestT.o != 0 && bestT.n >= bestId.n * 1.25 + 20 ? bestT : bestId;
            p.fit = (double)std::max(use.n, base.n) / (pts.size() + bonus * pairs.size());
            if (use.o == 0 && use.n < base.n * 1.15 + 10) continue;
            std::copy_n(kOrient[use.o], 4, p.m);
            p.sr *= use.s, p.si *= use.s;
            p.tx = use.tx, p.ty = use.ty;
            ++turned[use.o];
        }
        return turned;
    };
    {
        std::vector<int> turned = refine(best);
        // most runs turned the same way: the reference is the odd one out, take one of those instead
        int o = (int)(std::max_element(turned.begin() + 1, turned.end()) - turned.begin());
        if (turned[o] * 2 > (int)best.placed.size() - 1) {
            int newRef = -1;
            for (auto& p : best.placed)
                if (p.run != best.ref && std::equal(p.m, p.m + 4, orientOf(o))) newRef = p.run;
            if (newRef >= 0) {
                best = alignOn(best.A, best.B, newRef);
                refine(best);
            }
        }
    }
    for (const Place& p : best.placed) g.fits[p.run] = p.fit;
    // a run whose map hardly lies on the reference's in any way is another layout: left out
    best.placed.erase(std::remove_if(best.placed.begin(), best.placed.end(),
                                     [&](const Place& p) { return p.run != best.ref && p.fit < minFit; }),
                      best.placed.end());
    for (const Place& p : best.placed) g.usedRuns.push_back(p.run);
    const int ref = best.ref;
    std::vector<Place>& placed = best.placed;
    g.used = (int)placed.size();
    auto map = [&](const Place& p, double x, double y, double& ox2, double& oy2) { mapWith(best, p, x, y, ox2, oy2); };

    // 3. canvas: the reference outline, dimmed, with room for points of other runs that land outside it
    const Image& base = outlines[ref].img;
    double minX = 0, minY = 0, maxX = base.w, maxY = base.h;
    for (auto& p : placed)
        for (const Mark& m : marks[p.run]) {
            double x, y;
            map(p, m.x, m.y, x, y);
            minX = std::min(minX, x - 150), maxX = std::max(maxX, x + 150);
            minY = std::min(minY, y - 70), maxY = std::max(maxY, y + 40);
        }
    int ox = (int)std::ceil(-minX), oy = (int)std::ceil(-minY);
    int W = (int)std::ceil(maxX - minX), H = (int)std::ceil(maxY - minY);
    g.img.w = W, g.img.h = H;
    g.img.px.assign((size_t)W * H * 3, 0);
    Mask e = edges(base);
    // only the map's walls: lit twigs and grass that made it into an outline are short bits
    components(e.m, base.w, base.h, [&](const std::vector<int>& comp, const Box& b) {
        if (comp.size() < 250 && std::max(b.w, b.h) < 120)
            for (int k : comp) e.m[k] = 0;
    });
    for (int y = 0; y < base.h; ++y)
        for (int x = 0; x < base.w; ++x)
            if (e.m[y * base.w + x]) {
                uint8_t* q = &g.img.px[((size_t)(y + oy) * W + x + ox) * 3];
                q[0] = q[1] = 95, q[2] = 115;
            }
    auto put = [&](int x, int y, const uint8_t* c, double a = 1) {
        if (x < 0 || y < 0 || x >= W || y >= H) return;
        uint8_t* q = &g.img.px[((size_t)y * W + x) * 3];
        for (int i = 0; i < 3; ++i) q[i] = (uint8_t)std::lround(q[i] * (1 - a) + c[i] * a);
    };
    auto disc = [&](double fx, double fy, double r0, double r1, const uint8_t* c) {
        for (int y = (int)(fy - r1) - 1; y <= (int)(fy + r1) + 1; ++y)
            for (int x = (int)(fx - r1) - 1; x <= (int)(fx + r1) + 1; ++x) {
                double d = std::hypot(x - fx, y - fy);
                if (d >= r0 && d <= r1) put(x, y, c);
            }
    };
    // a label, enlarged 1.5x from its run's picture (smooth), in the point's colour, on a dark backing
    const double S = 1.5;
    std::vector<Box> taken;  // labels already on the map: a new one that would cover one goes up a line
    auto label = [&](const Mark& m, int cx, int bottom, const uint8_t* col, bool onMap = true) {
        const Label& l = *m.lab;
        int lw = (int)(l.w * S), lh = (int)(l.h * S), x0 = cx - lw / 2, y0 = bottom - lh;
        for (int tries = 0; onMap && tries < 4; ++tries) {
            bool hit = false;
            for (const Box& t : taken)
                hit |= x0 - 4 < t.x + t.w && t.x < x0 + lw + 4 && y0 - 3 < t.y + t.h && t.y < y0 + lh + 3;
            if (!hit) break;
            y0 -= lh + 6;
        }
        if (onMap) taken.push_back({x0 - 4, y0 - 3, lw + 8, lh + 6});
        static const uint8_t black[3] = {0, 0, 0};
        for (int y = -3; y < lh + 3; ++y)
            for (int x = -4; x < lw + 4; ++x) put(x0 + x, y0 + y, black, 0.75);
        for (int y = 0; y < lh; ++y)
            for (int x = 0; x < lw; ++x) {
                double fx = x / S, fy = y / S;  // bilinear sample of the text brightness
                int ix = (int)fx, iy = (int)fy;
                double tx = fx - ix, ty = fy - iy, v = 0;
                for (int j = 0; j < 2; ++j)
                    for (int i = 0; i < 2; ++i) {
                        int X = std::min(l.w - 1, ix + i), Y = std::min(l.h - 1, iy + j);
                        v += m.text[Y * l.w + X] / 255.0 * (i ? tx : 1 - tx) * (j ? ty : 1 - ty);
                    }
                if (v > 0.05) put(x0 + x, y0 + y, col, std::min(1.0, v * 1.2));
            }
        return lw;
    };

    // 4. the points. Checkpoints (repeated labels): small dots. Entrances: a dot and label where each run had it, a
    // ring when always in the same place. Places inside: grouped into the spots they swap between (numbered rings);
    // a dot in the place's colour where each run had it, the names in the legend.
    static const uint8_t cols[][3] = {{255, 205, 40}, {80, 200, 255}, {255, 110, 190}, {120, 230, 110},
                                      {255, 140, 60}, {200, 150, 255}, {90, 255, 220}, {255, 255, 140}};
    static const uint8_t check[3] = {40, 140, 190};
    static const uint8_t slotCol[3] = {170, 170, 185};
    struct Legend {
        const Mark* rep;
        const uint8_t* col;
        std::string text;
    };
    std::vector<Legend> legend;
    struct Pts {
        int cls;
        std::vector<std::pair<double, double>> at;
        const Mark* rep = nullptr;
    };
    std::vector<Pts> named;
    std::vector<std::pair<double, double>> checks;  // checkpoints of all runs
    const Mark* checkRep = nullptr;
    for (int c = 0; c < nCls; ++c) {
        Pts p{c, {}, nullptr};
        for (auto& pl : placed)
            for (const Mark& m : marks[pl.run]) {
                if (m.cls != c) continue;
                double x, y;
                map(pl, m.x, m.y, x, y);
                p.at.push_back({x + ox, y + oy});
                if (!p.rep || m.lab->w > p.rep->lab->w) p.rep = &m;  // the most complete label
            }
        if (p.at.size() < 2) continue;  // seen once: nothing to compare (and often not a real label)
        if (isCheck[c]) {
            for (auto& q : p.at) checks.push_back(q);
            if (!checkRep || p.rep->lab->w > checkRep->lab->w) checkRep = p.rep;
            continue;
        }
        named.push_back(std::move(p));
    }
    // checkpoints: a small dot each; where 2 or more runs had one (within 80 px), a small ring
    {
        std::vector<int> par(checks.size());
        for (size_t i = 0; i < checks.size(); ++i) par[i] = (int)i;
        std::function<int(int)> rt = [&](int i) { return par[i] == i ? i : par[i] = rt(par[i]); };
        for (size_t i = 0; i < checks.size(); ++i)
            for (size_t j = i + 1; j < checks.size(); ++j)
                if (std::hypot(checks[i].first - checks[j].first, checks[i].second - checks[j].second) < 80)
                    par[rt((int)i)] = rt((int)j);
        std::map<int, std::vector<int>> groups;
        for (size_t i = 0; i < checks.size(); ++i) groups[rt((int)i)].push_back((int)i);
        for (auto& kv : groups) {
            double mx = 0, my = 0;
            for (int i : kv.second) mx += checks[i].first, my += checks[i].second;
            mx /= kv.second.size(), my /= kv.second.size();
            if (kv.second.size() >= 2) disc(mx, my, 12, 14, check);
            for (int i : kv.second) disc(checks[i].first, checks[i].second, 0, 5, check);
        }
    }
    std::stable_sort(named.begin(), named.end(), [&](const Pts& a, const Pts& b) {
        if (entrance[a.cls] != entrance[b.cls]) return (bool)entrance[a.cls];  // entrances first
        return a.at.size() > b.at.size();
    });
    // spots: points of the inner places closer than 130 px are one spot
    struct Spot {
        double x = 0, y = 0, r = 0;
        std::vector<std::pair<double, double>> pts;
    };
    std::vector<Spot> spots;
    std::vector<std::vector<int>> spotOf(named.size());
    {
        std::vector<std::pair<int, int>> who;  // (named index, point index)
        std::vector<std::pair<double, double>> pts;
        for (size_t i = 0; i < named.size(); ++i)
            if (!entrance[named[i].cls])
                for (size_t j = 0; j < named[i].at.size(); ++j) who.push_back({(int)i, (int)j}), pts.push_back(named[i].at[j]);
        std::vector<int> par(pts.size());
        for (size_t i = 0; i < pts.size(); ++i) par[i] = (int)i;
        std::function<int(int)> rt = [&](int i) { return par[i] == i ? i : par[i] = rt(par[i]); };
        for (size_t i = 0; i < pts.size(); ++i)
            for (size_t j = i + 1; j < pts.size(); ++j)
                if (std::hypot(pts[i].first - pts[j].first, pts[i].second - pts[j].second) < 130) par[rt((int)i)] = rt((int)j);
        std::map<int, int> idx;
        std::vector<int> spotOfPt(pts.size());
        for (size_t i = 0; i < pts.size(); ++i) {
            int r = rt((int)i);
            if (!idx.count(r)) idx[r] = (int)spots.size(), spots.emplace_back();
            spots[idx[r]].pts.push_back(pts[i]);
            spotOfPt[i] = idx[r];
        }
        for (Spot& sp : spots) {
            for (auto& q : sp.pts) sp.x += q.first, sp.y += q.second;
            sp.x /= sp.pts.size(), sp.y /= sp.pts.size();
            for (auto& q : sp.pts) sp.r = std::max(sp.r, std::hypot(q.first - sp.x, q.second - sp.y));
            sp.r += 22;
        }
        std::vector<int> order(spots.size());  // numbered left to right
        for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
        std::sort(order.begin(), order.end(), [&](int a, int b) { return spots[a].x < spots[b].x; });
        std::vector<int> num(spots.size());
        for (size_t i = 0; i < order.size(); ++i) num[order[i]] = (int)i + 1;
        for (size_t i = 0; i < pts.size(); ++i) {
            auto& v = spotOf[who[i].first];
            if (std::find(v.begin(), v.end(), num[spotOfPt[i]]) == v.end()) v.push_back(num[spotOfPt[i]]);
        }
        for (auto& v : spotOf) std::sort(v.begin(), v.end());
        std::vector<Spot> sorted;
        for (int i : order) sorted.push_back(spots[i]);
        spots = std::move(sorted);
    }
    for (const Spot& sp : spots) {
        disc(sp.x, sp.y, sp.r, sp.r + 2, slotCol);
    }
    int nCol = 0;
    for (size_t i = 0; i < named.size(); ++i) {
        const Pts& p = named[i];
        const uint8_t* col = cols[nCol++ % 8];
        int most = 0, with = 0;  // runs lined up that had it
        for (auto& pl : placed)
            if (perRun[p.cls].count(pl.run)) ++with, most = std::max(most, perRun[p.cls][pl.run]);
        std::string runs = std::to_string(with) + "/" + std::to_string(g.used) + " runs" +
                           (most > 1 ? "  \u00b7  up to " + std::to_string(most) + " per run" : "");
        if (entrance[p.cls]) {
            double mx = 0, my = 0, spread = 0;
            for (auto& q : p.at) mx += q.first, my += q.second;
            mx /= p.at.size(), my /= p.at.size();
            for (auto& q : p.at) spread = std::max(spread, std::hypot(q.first - mx, q.second - my));
            bool same = spread <= 120;
            for (auto& q : p.at) disc(q.first, q.second, 0, 8, col);
            if (!same) label(*p.rep, (int)p.at[0].first, (int)p.at[0].second - 14, col);  // once: the legend says the rest
            if (same) {
                disc(mx, my, 20, 23, col);
                label(*p.rep, (int)mx, (int)my - 28, col);
            }
            legend.push_back({p.rep, col, "entrance  \u00b7  " + runs + "  \u00b7  " +
                                              (same ? std::string("same place") : std::string("moves"))});
            continue;
        }
        for (auto& q : p.at) disc(q.first, q.second, 0, 8, col);
        std::string where;
        for (int n : spotOf[i]) where += (where.empty() ? "" : ", ") + std::to_string(n);
        legend.push_back({p.rep, col, runs + "  \u00b7  " + (spotOf[i].size() == 1 ? "spot " : "spots ") + where});
    }

    if (checkRep) {
        static const uint8_t checkText[3] = {80, 180, 230};
        legend.push_back({checkRep, checkText, "small dots  \u00b7  ring: 2+ runs had one there"});
    }

    // 5. crop to the map and its points, with the legend above it: a caption line (written by the app), then one
    // row per place: its label, then the text
    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const uint8_t* q = &g.img.px[((size_t)y * W + x) * 3];
            if (q[0] || q[1] || q[2]) x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
        }
    if (x1 < 0) return g;
    const int top = 44 + (int)legend.size() * 28 + 16;  // room for caption and legend
    x0 = std::max(0, x0 - 40), x1 = std::min(W - 1, x1 + 40), y1 = std::min(H - 1, y1 + 30);
    int sy0 = std::max(0, y0 - 20);
    Image c;
    c.w = std::max(x1 - x0 + 1, 760), c.h = (y1 - sy0 + 1) + top;
    c.px.assign((size_t)c.w * c.h * 3, 0);
    for (int y = sy0; y <= y1; ++y)
        std::copy_n(&g.img.px[((size_t)y * W + x0) * 3], (size_t)(x1 - x0 + 1) * 3,
                    &c.px[((size_t)(y - sy0 + top) * c.w) * 3]);
    g.img = std::move(c);
    W = g.img.w, H = g.img.h;
    for (size_t i = 0; i < spots.size(); ++i)  // spot numbers, at the top right of their rings
        g.texts.push_back({(int)(spots[i].x + spots[i].r * 0.7) - x0 + 2, (int)(spots[i].y - spots[i].r * 0.7) - sy0 + top - 22,
                           std::to_string(i + 1)});
    for (size_t i = 0; i < legend.size(); ++i) {
        int ly = 70 + (int)i * 28;  // label bottom
        int lw = label(*legend[i].rep, 16 + (int)(legend[i].rep->lab->w * S) / 2, ly, legend[i].col, false);
        g.texts.push_back({16 + lw + 14, ly - 20, legend[i].text});
    }
    return g;
}

}  // namespace outline

namespace outline {

namespace {

// The ground inside a map's outer walls, on a square grid of 16 px cells (the overlay's squash undone), centred on
// its middle. Small bits (text, lit twigs) are left out; gaps in the walls up to ~16 px are closed first.
const int kShapeN = 200;
using Shape = std::vector<uint8_t>;

Shape mapShape(const Image& im) {
    const int C = 8, G = 16, N = kShapeN;
    const double k = 0.82;
    int cw = (im.w + C - 1) / C, ch = (im.h + C - 1) / C;
    std::vector<uint8_t> cell((size_t)cw * ch, 0);
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            const uint8_t* p = &im.px[((size_t)y * im.w + x) * 3];
            int mx = std::max({p[0], p[1], p[2]}), mn = std::min({p[0], p[1], p[2]});
            if (mn > 150 && mx - mn < 60) cell[(y / C) * cw + x / C] = 1;
        }
    size_t largest = 0;
    std::vector<std::pair<std::vector<int>, size_t>> comps;
    components(cell, cw, ch, [&](const std::vector<int>& comp, const Box&) {
        largest = std::max(largest, comp.size());
        comps.push_back({comp, comp.size()});
    });
    std::fill(cell.begin(), cell.end(), 0);
    for (auto& c : comps)
        if (c.second >= std::max<size_t>(30, largest / 20))
            for (int i : c.first) cell[i] = 1;
    // walls made 2 cells thicker, then the outside: what is reached from the border
    int W = cw + 2, H = ch + 2;
    std::vector<uint8_t> wall((size_t)W * H, 0), out((size_t)W * H, 0);
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x)
            if (cell[y * cw + x])
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx) wall[(size_t)(y + 1 + dy) * W + x + 1 + dx] = 1;
    std::vector<int> q = {0};
    out[0] = 1;
    for (size_t i = 0; i < q.size(); ++i) {
        int x = q[i] % W, y = q[i] / W;
        for (auto [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
            int X = x + dx, Y = y + dy;
            if (X < 0 || Y < 0 || X >= W || Y >= H || out[(size_t)Y * W + X] || wall[(size_t)Y * W + X]) continue;
            out[(size_t)Y * W + X] = 1, q.push_back(Y * W + X);
        }
    }
    double sx = 0, sy = 0;
    long n = 0;
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x)
            if (!out[(size_t)(y + 1) * W + x + 1]) sx += x * C, sy += y * C / k, ++n;
    Shape s((size_t)N * N, 0);
    if (!n) return s;
    sx /= n, sy /= n;
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x)
            if (!out[(size_t)(y + 1) * W + x + 1]) {
                int gx = (int)((x * C - sx) / G + N / 2), gy = (int)((y * C / k - sy) / G + N / 2);
                if (gx >= 0 && gy >= 0 && gx < N && gy < N) s[(size_t)gy * N + gx] = 1;
            }
    return s;
}

// The 8 ways to turn / mirror a shape, each centred again.
std::vector<Shape> turns(const Shape& s) {
    const int N = kShapeN;
    std::vector<Shape> r;
    for (int o = 0; o < 8; ++o) {
        Shape t((size_t)N * N, 0);
        double mx = 0, my = 0;
        long n = 0;
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                if (!s[(size_t)y * N + x]) continue;
                int X = x, Y = y;
                if (o & 4) X = N - 1 - X;  // mirrored
                for (int k = 0; k < (o & 3); ++k) std::tie(X, Y) = std::pair{N - 1 - Y, X};  // turned 90
                t[(size_t)Y * N + X] = 1, mx += X, my += Y, ++n;
            }
        if (!n) {
            r.push_back(t);
            continue;
        }
        int ox = (int)std::lround(N / 2 - mx / n), oy = (int)std::lround(N / 2 - my / n);
        Shape c((size_t)N * N, 0);
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x)
                if (t[(size_t)y * N + x] && x + ox >= 0 && y + oy >= 0 && x + ox < N && y + oy < N)
                    c[(size_t)(y + oy) * N + x + ox] = 1;
        r.push_back(std::move(c));
    }
    return r;
}

// How much two shapes overlap (shared / either), best over the turns of b and small shifts.
double sameShape(const Shape& a, const std::vector<Shape>& bTurns) {
    const int N = kShapeN;
    double best = 0;
    for (const Shape& b : bTurns)
        for (int dy = -4; dy <= 4; dy += 2)
            for (int dx = -4; dx <= 4; dx += 2) {
                long both = 0, either = 0;
                for (int y = 0; y < N; ++y) {
                    int Y = y - dy;
                    for (int x = 0; x < N; ++x) {
                        int X = x - dx;
                        bool bb = X >= 0 && Y >= 0 && X < N && Y < N && b[(size_t)Y * N + X];
                        bool aa = a[(size_t)y * N + x];
                        both += aa && bb, either += aa || bb;
                    }
                }
                if (either) best = std::max(best, (double)both / either);
            }
    return best;
}

}  // namespace

std::vector<std::vector<int>> layoutFamilies(const std::vector<Outline>& outlines, double minSame) {
    int n = (int)outlines.size();
    std::vector<Shape> shapes;
    for (auto& o : outlines) shapes.push_back(mapShape(o.img));
    std::vector<std::vector<double>> same(n, std::vector<double>(n, 1));
    for (int j = 0; j < n; ++j) {
        auto t = turns(shapes[j]);
        for (int i = 0; i < j; ++i) same[i][j] = same[j][i] = sameShape(shapes[i], t);
    }
    // joined while the two groups are alike on average (average linkage): a layout in between does not chain
    // two different ones together
    std::vector<std::vector<int>> groups;
    for (int i = 0; i < n; ++i) groups.push_back({i});
    for (;;) {
        double best = -1;
        int ba = -1, bb = -1;
        for (size_t a = 0; a < groups.size(); ++a)
            for (size_t b = a + 1; b < groups.size(); ++b) {
                double s = 0;
                for (int i : groups[a])
                    for (int j : groups[b]) s += same[i][j];
                s /= (double)(groups[a].size() * groups[b].size());
                if (s > best) best = s, ba = (int)a, bb = (int)b;
            }
        if (ba < 0 || best < minSame) break;
        groups[ba].insert(groups[ba].end(), groups[bb].begin(), groups[bb].end());
        groups.erase(groups.begin() + bb);
    }
    for (auto& g : groups) std::sort(g.begin(), g.end());
    std::stable_sort(groups.begin(), groups.end(), [](auto& a, auto& b) { return a.size() > b.size(); });
    return groups;
}

}  // namespace outline
