// MapTimerOverlay: current area + timer, recent records, campaign footer, resizable.
#include "app.h"
#include "experience.h"

struct OvLine {
    std::wstring lvl, name, time;
    std::wstring range = L"";  // "54-64" for zones that follow the character level, shown small after the level
    std::wstring cmp = L"";    // "(+5s)": against the best run (campaign zones, Options), left of the time
};

bool gOvCompare = false;
HWND hCmpBestChk = nullptr;

static int ovHeight(double sc, int n);
static void overlayData(OvLine& head, std::vector<OvLine>& last, COLORREF& timeCol);
static void overlayDataNow(OvLine& head, std::vector<OvLine>& last, COLORREF& timeCol);
static void paintOverlay(HDC dc, RECT rc);
static void overlayResized(int w, int h);
static void ovFitHeight();
static LRESULT CALLBACK OverlayProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);

double gOvScale = 1.0;

// Always-on-top window: current area with its timer, plus the last N areas (0-10) in grey.
// Shows the campaign zone while in the campaign, otherwise the current or last map.
// Everything scales with the window size.
static const int kOvW = 300, kOvPad = 10, kOvHead = 24, kOvLine = 19, kOvEdge = 7;

int gOvRecords = 4;
int gOvTransparency = 10;

// Settings of this overlay live in [MapTimerOverlay]; older versions used [overlay] (read as fallback).
static const wchar_t* kOvSec = L"MapTimerOverlay";

int ovIniInt(const wchar_t* key, int def) {
    int old = GetPrivateProfileIntW(L"overlay", key, def, iniPath().c_str());
    return GetPrivateProfileIntW(kOvSec, key, old, iniPath().c_str());
}

void ovIniSet(const wchar_t* key, int v) {
    WritePrivateProfileStringW(kOvSec, key, std::to_wstring(v).c_str(), iniPath().c_str());
}

// Campaign footer ("Act 1: 01:08" | "Campaign: 07:43") shown while in a campaign zone.
static const int kOvFoot = 9 + 19;  // separator + one line

static bool gOvFoot = false;
static bool gOvFootPrev = false;  // with Compare zone times: the previous act's line above the current one

// Two experience lines under the header (Experience tab): the current area level (it moves here from the header) with
// the character level, and the best area level to level up in. Both with the experience compared to a monster of the
// character's own level.
static const int kOvExp = 18;
static bool gOvExp = false;

static int ovHeight(double sc, int n) {
    return (
        int)((kOvPad * 2 + kOvHead + (gOvExp ? (gOvFoot ? 4 : 3) * kOvExp : 0) + (n ? 9 + n * kOvLine : 0) + (gOvFoot ? kOvFoot : 0) + (gOvFootPrev ? kOvLine : 0)) *
                 sc +
             0.5);
}

static void overlayData(OvLine& head, std::vector<OvLine>& last, COLORREF& timeCol) {
    static OvLine cHead;
    static std::vector<OvLine> cLast;
    static COLORREF cCol = C::Comment;
    bool waiting = (g.curZone >= 0 && unnamed(g.zones[g.curZone].name, g.zones[g.curZone].areaId)) ||
                   (g.curZone < 0 && g.hasCurrent && unnamed(g.cur.name, g.cur.areaId));
    if (!waiting) {
        overlayDataNow(cHead, cLast, cCol);
    }
    head = cHead;
    last = cLast;
    timeCol = cCol;
}

static void overlayDataNow(OvLine& head, std::vector<OvLine>& last, COLORREF& timeCol) {
    uint32_t now = nowTick();
    last.clear();
    timeCol = C::Comment;
    size_t want = (size_t)gOvRecords;
    bool stoppedHere = g.curZone < 0 && g.cmpClock == Tracker::Clock::Stopped && Tracker::isCampaign(g.location);
    if (g.curZone >= 0 || stoppedHere) {  // campaign
        if (stoppedHere) {  // run finished: the header shows the final campaign time
            uint64_t total = 0;
            for (auto& z : g.zones) total += z.activeMs;
            head = {std::to_wstring(g.locationLevel), W(g.locationDisplay()),
                    W(fmtDuration(total))};
            timeCol = C::Red;
        } else {
            const Zone& z = g.zones[g.curZone];
            head = {std::to_wstring(z.level), areaName(z.name, z.areaId), W(fmtDuration(z.activeAt(now))),
                    W(g.levelRange(z.areaId))};
            if (gOvCompare && !z.town) head.cmp = zoneVsBest(z.areaId, now);
            timeCol = g.cmpClock == Tracker::Clock::Paused ? C::Yellow : z.inSegment ? C::Purple : C::Cyan;
        }
        std::vector<int> idx;
        for (size_t i = 0; i < g.zones.size(); ++i)
            if ((int)i != g.curZone && g.zones[i].entries > 0) idx.push_back((int)i);
        std::sort(idx.begin(), idx.end(), [](int a, int b) { return g.zones[a].lastEnter > g.zones[b].lastEnter; });
        for (size_t i = 0; i < idx.size() && i < want; ++i) {
            const Zone& p = g.zones[idx[i]];
            last.push_back({std::to_wstring(p.level), areaName(p.name, p.areaId), W(fmtDuration(p.activeAt(now)))});
        }
        return;
    }
    if (g.inHideoutNow) {
        // In a hideout: the hideout is the header (time of this stay), the paused map becomes the first record.
        head = {std::to_wstring(g.locationLevel), W(g.locationIsLoaded() && !g.locationName.empty() ? g.locationName : std::string("Hideout")),
                W(fmtDuration((uint32_t)(now - g.hideoutStayTick)))};
        timeCol = C::Orange;
        if (g.hasCurrent && last.size() < want)
            last.push_back({std::to_wstring(g.cur.level), areaName(g.cur.name, g.cur.areaId),
                            W(fmtDuration(g.cur.activeAt(now)))});
    } else if (g.hasCurrent) {
        head = {std::to_wstring(g.cur.level), areaName(g.cur.name, g.cur.areaId), W(fmtDuration(g.cur.activeAt(now)))};
        // In a sub-area: its own name, level and timer. Back in the map the header shows the map time again,
        // which includes the sub-areas.
        if (g.inSubArea() && !unnamed(g.locationName, g.location)) {
            head.lvl = std::to_wstring(g.locationLevel);
            head.name = areaName(g.locationName, g.location);
            head.time = W(fmtDuration(g.subAreaAt(now)));
        }
        timeCol = g.cur.state == RunState::Paused ? C::Orange : g.cur.state == RunState::Loading ? C::Cyan : C::Purple;
    } else {
        head = {L"", W(g.location.empty() ? std::string("No active map") : g.locationDisplay()), L"00:00"};
    }
    for (size_t i = g.history.size(); i > g.generalStart && last.size() < want; --i) {
        const Run& p = g.history[i - 1];
        last.push_back({std::to_wstring(p.level), areaName(p.name, p.areaId), W(fmtDuration(p.activeMs))});
    }
}

void ovFonts(double sc) {
    if (fOvHead) DeleteObject(fOvHead);
    if (fOvSmall) DeleteObject(fOvSmall);
    if (fOvTiny) DeleteObject(fOvTiny);
    fOvTiny = font((int)(11 * sc + 0.5), FW_NORMAL, gFontUi.c_str());  // level range after the header level
    fOvHead = font((int)(18 * sc + 0.5), FW_BOLD, gFontUi.c_str());
    fOvSmall = font((int)(14 * sc + 0.5), FW_NORMAL, gFontUi.c_str());
}

static void paintOverlay(HDC dc, RECT rc) {
    const double S = gOvScale;
    const int P = (int)(kOvPad * S), HH = (int)(kOvHead * S), LH = (int)(kOvLine * S);
    FillRect(dc, &rc, brDarkest);
    HPEN pen = CreatePen(PS_SOLID, 1, C::Line);
    HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, 14, 14);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(pen);
    SetBkMode(dc, TRANSPARENT);

    OvLine head;
    std::vector<OvLine> last;
    COLORREF timeCol;
    overlayData(head, last, timeCol);

    auto line = [&](const OvLine& l, int y, int h, HFONT f, COLORREF lvlCol, COLORREF nameCol, COLORREF tCol) {
        SelectObject(dc, f);
        RECT t = {P, y, rc.right - P, y + h};
        SetTextColor(dc, tCol);
        DrawTextW(dc, l.time.c_str(), -1, &t, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        SIZE ts, ls;
        GetTextExtentPoint32W(dc, l.time.c_str(), (int)l.time.size(), &ts);
        if (!l.cmp.empty()) {  // "(+5s)" left of the time: green under the best, red over it
            SelectObject(dc, fOvSmall);
            SIZE cs;
            GetTextExtentPoint32W(dc, l.cmp.c_str(), (int)l.cmp.size(), &cs);
            RECT c = {P, y, rc.right - P - ts.cx - (int)(6 * S), y + h};
            SetTextColor(dc, l.cmp[1] == L'-' ? C::Green : C::Red);
            DrawTextW(dc, l.cmp.c_str(), -1, &c, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            ts.cx += cs.cx + (int)(6 * S);  // the name ends before both
            SelectObject(dc, f);
        }
        GetTextExtentPoint32W(dc, L"00", 2, &ls);
        RECT lv = {P, y, P + ls.cx + 2, y + h};
        SetTextColor(dc, lvlCol);
        DrawTextW(dc, l.lvl.c_str(), -1, &lv, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        int nameX = P + (l.lvl.empty() ? 0 : ls.cx + (int)(8 * S));
        if (!l.range.empty()) {  // "(54-64)" in the tiny font right after the level
            std::wstring r = L"(" + l.range + L")";
            SelectObject(dc, fOvTiny);
            SIZE rs;
            GetTextExtentPoint32W(dc, r.c_str(), (int)r.size(), &rs);
            RECT rr = {nameX - (int)(5 * S), y, nameX - (int)(5 * S) + rs.cx, y + h};
            SetTextColor(dc, C::Comment);
            DrawTextW(dc, r.c_str(), -1, &rr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            nameX = rr.right + (int)(8 * S);
            SelectObject(dc, f);
        }
        RECT n = {nameX, y, rc.right - P - ts.cx - (int)(10 * S), y + h};
        SetTextColor(dc, nameCol);
        DrawTextW(dc, l.name.c_str(), -1, &n, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    };
    {
        OvLine h = head;
        if (gOvExp) h.lvl.clear(), h.range.clear();  // the area level moves to the experience line below
        line(h, P, HH, fOvHead, C::Pink, C::Fg, timeCol);
    }
    const int XL = (int)(kOvExp * S), XH = gOvExp ? (gOvFoot ? 4 : 3) * XL : 0;  // 4th line in the campaign
    if (gOvExp) {
        // Line 1: "Curr. area: 64 (56-64)  Your lvl: 88" and the exp bonus of the current area. The area level is
        // red when it is outside the safe range (3 + level / 16 levels around the character), the range in small
        // print is shown for zones that follow the character level.
        // Line 2: "Best area: 55", the area level with the most experience per monster, and its exp bonus.
        int area = expArea(), range = xp::safeRange(gExpLevel);
        bool below = area > 0 && area < gExpLevel;
        int bound = below ? std::max(1, gExpLevel - range) : gExpLevel + range;
        int margin = below ? area - bound : bound - area;
        const double bonus = expEffMult() * expGainMult();  // tablets and map mods
        int best = xp::bestArea(gExpLevel, bonus);
        double mArea = area > 0 ? xp::expectedMultiplier(gExpLevel, area, bonus) : bonus,
               mBest = xp::expectedMultiplier(gExpLevel, best, bonus);
        std::wstring vArea = expPct(mArea), vBest = expPct(mBest);
        SelectObject(dc, fOvSmall);
        SIZE w1, w2;  // both "Exp bonus:" labels start at the same x: right of them room for the wider value
        GetTextExtentPoint32W(dc, vArea.c_str(), (int)vArea.size(), &w1);
        GetTextExtentPoint32W(dc, vBest.c_str(), (int)vBest.size(), &w2);
        const int valueW = std::max(w1.cx, w2.cx);
        // Draws "caption value" pairs from the left (caption grey), then "Exp bonus: x%" aligned right.
        struct Part {
            std::wstring caption, shortCaption, value;
            COLORREF col;
            std::wstring small = L"";  // drawn in the tiny font after the value, e.g. "(56-64)"
        };
        std::wstring lvlRange = g.curZone >= 0 ? W(g.levelRange(g.zones[g.curZone].areaId)) : L"";
        std::vector<Part> line1 = {{L"Curr. area:", L"Area:", area > 0 ? std::to_wstring(area) : std::wstring(L"-"),
                                    area > 0 && margin < 0 ? C::Red : C::Pink, lvlRange},
                                   {L"Your lvl:", L"Lvl:", std::to_wstring(gExpLevel), C::Green}};
        std::vector<Part> line2 = {{L"Best area:", L"Best:", W(xp::bestAreaText(gExpLevel, bonus)), C::Green}};
        const int gap = (int)(10 * S);
        auto textW = [&](const std::wstring& t, HFONT f) {
            SelectObject(dc, f);
            SIZE sz;
            GetTextExtentPoint32W(dc, t.c_str(), (int)t.size(), &sz);
            SelectObject(dc, fOvSmall);
            return (int)sz.cx;
        };
        // Width of the left part of a line; `shortCaps` uses the short captions ("Area:", "Lvl:").
        auto partsW = [&](const std::vector<Part>& parts, bool shortCaps) {
            int w = 0;
            for (const Part& p : parts) {
                w += textW((shortCaps ? p.shortCaption : p.caption) + L" ", fOvSmall) + textW(p.value, fOvSmall) + gap;
                if (!p.small.empty()) w += textW(L" (" + p.small + L")", fOvTiny);
            }
            return w;
        };
        // Short captions when the full ones do not fit next to the exp bonus (narrow overlay).
        const int avail = rc.right - 2 * P;
        bool shortCaps = partsW(line1, false) + textW(L"Exp bonus: ", fOvSmall) + valueW > avail;
        const std::wstring bonusCap = shortCaps ? L"Exp:" : L"Exp bonus:";
        auto expLine = [&](int y, const std::vector<Part>& parts, double m, const std::wstring& val) {
            RECT r = {P, y, rc.right - P, y + XL};
            int x = P;
            for (const Part& p : parts) {
                std::wstring cap = (shortCaps ? p.shortCaption : p.caption) + L" ";
                RECT rp = {x, y, rc.right - P, y + XL};
                SetTextColor(dc, C::Comment);
                DrawTextW(dc, cap.c_str(), -1, &rp, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                rp.left += textW(cap, fOvSmall);
                SetTextColor(dc, p.col);
                DrawTextW(dc, p.value.c_str(), -1, &rp, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                rp.left += textW(p.value, fOvSmall);
                if (!p.small.empty()) {
                    std::wstring sm = L" (" + p.small + L")";
                    SelectObject(dc, fOvTiny);
                    SetTextColor(dc, C::Comment);
                    DrawTextW(dc, sm.c_str(), -1, &rp, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(dc, fOvSmall);
                    rp.left += textW(sm, fOvTiny);
                }
                x = rp.left + gap;
            }
            SetTextColor(dc, m > 1.0005 ? C::Green : m < 0.9995 ? C::Red : C::Fg);
            DrawTextW(dc, val.c_str(), -1, &r, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            RECT rl = r;
            rl.right -= valueW + (int)(5 * S);
            SetTextColor(dc, C::Comment);
            DrawTextW(dc, bonusCap.c_str(), -1, &rl, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        };
        expLine(P + HH, line1, mArea, vArea);
        expLine(P + HH + XL, line2, mBest, vBest);
        // Line 3: experience to the next level, and exp/h of the last level gained in one zone ("In Between"); line 4
        // in the campaign: exp/h of the whole run ("Total")
        auto rightValue = [&](int y, const wchar_t* cap, const std::wstring& val) {
            RECT rr = {P, y, rc.right - P, y + XL};
            std::wstring v = val.empty() ? L"-" : val;
            SelectObject(dc, fOvSmall);
            SetTextColor(dc, C::Cyan);
            DrawTextW(dc, v.c_str(), -1, &rr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            rr.right -= textW(v, fOvSmall) + (int)(5 * S);
            SetTextColor(dc, C::Comment);
            DrawTextW(dc, cap, -1, &rr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        };
        {
            std::wstring req = expRequiredText(), eph = expPerHourText();
            int y = P + HH + 2 * XL;
            RECT r = {P, y, rc.right - P, y + XL};
            SelectObject(dc, fOvSmall);
            std::wstring cap = shortCaps ? L"Req:" : L"Required exp:";
            SetTextColor(dc, C::Comment);
            DrawTextW(dc, (cap + L" ").c_str(), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            r.left += textW(cap + L" ", fOvSmall);
            SetTextColor(dc, C::Cyan);
            DrawTextW(dc, (req.empty() ? L"-" : req).c_str(), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            rightValue(y, L"In Between:", eph);
            if (gOvFoot) {
                uint64_t total = 0;
                for (auto& z : g.zones) total += z.activeAt(nowTick());
                rightValue(y + XL, L"Total:", expCampaignPerHourText(total));
            }
        }
    }
    HBRUSH b;
    if (gOvRecords > 0) {
        int y = P + HH + XH + (int)(4 * S);
        RECT sep = {P, y, rc.right - P, y + 1};
        b = CreateSolidBrush(C::Line);
        FillRect(dc, &sep, b);
        DeleteObject(b);
        y += (int)(5 * S);
        for (auto& l : last) {
            line(l, y, LH, fOvSmall, C::Comment, C::Comment, C::Comment);
            y += LH;
        }
    }
    if (!gOvFoot) return;
    // Footer: time of the current act and of the whole campaign run
    uint32_t now = nowTick();
    std::string act = Tracker::actOf(g.curZone >= 0 ? g.zones[g.curZone].areaId : g.location);
    COLORREF clockCol = g.cmpClock == Tracker::Clock::Stopped            ? C::Red
                        : g.cmpClock == Tracker::Clock::Paused           ? C::Yellow
                        : g.curZone >= 0 && g.zones[g.curZone].inSegment ? C::Purple
                                                                         : C::Cyan;
    uint64_t actMs = 0, totalMs = 0;
    for (auto& z : g.zones) {
        uint64_t ms = z.activeAt(now);
        totalMs += ms;
        if (Tracker::actOf(z.areaId) == act) actMs += ms;
    }
    int y =
        P + HH + XH + (gOvRecords > 0 ? (int)(9 * S) + gOvRecords * LH : 0) + (int)(4 * S);  // below the record lines
    RECT sep2 = {P, y, rc.right - P, y + 1};
    b = CreateSolidBrush(C::Line);
    FillRect(dc, &sep2, b);
    DeleteObject(b);
    y += (int)(5 * S);
    SelectObject(dc, fOvSmall);
    std::wstring pName, pTime, pCmp;
    if (gOvFootPrev && previousAct(now, pName, pTime, pCmp)) {  // "Act 1: 15:12 (+3m15s)": the act just finished
        std::wstring lp = pName + L": ";
        SIZE a, t;
        GetTextExtentPoint32W(dc, lp.c_str(), (int)lp.size(), &a);
        GetTextExtentPoint32W(dc, pTime.c_str(), (int)pTime.size(), &t);
        SetTextColor(dc, C::Comment);
        TextOutW(dc, P, y + (LH - a.cy) / 2, lp.c_str(), (int)lp.size());
        SetTextColor(dc, C::Fg);
        TextOutW(dc, P + a.cx, y + (LH - a.cy) / 2, pTime.c_str(), (int)pTime.size());
        if (!pCmp.empty()) {
            SetTextColor(dc, pCmp[1] == L'-' ? C::Green : C::Red);
            TextOutW(dc, P + a.cx + t.cx + (int)(6 * S), y + (LH - a.cy) / 2, pCmp.c_str(), (int)pCmp.size());
        }
        y += LH;
    }
    // One line: "Act 1: 01:08" on the left, "Campaign: 07:43" on the right
    std::wstring lA = (act.empty() ? std::wstring(L"Act") : W(act)) + L":", lB = L"Campaign:";
    std::wstring tA = W(fmtDuration(actMs)), tB = W(fmtDuration(totalMs));
    SIZE sp, lb, tb;
    GetTextExtentPoint32W(dc, L" ", 1, &sp);
    GetTextExtentPoint32W(dc, lA.c_str(), (int)lA.size(), &lb);
    SetTextColor(dc, C::Comment);
    TextOutW(dc, P, y + (LH - lb.cy) / 2, lA.c_str(), (int)lA.size());
    // with Compare zone times: over the act's time to beat, the act time turns red with "(+1m05s)" after it
    int64_t actBest = gOvCompare && !act.empty() ? bestActMs(act) : -1;
    bool over = actBest >= 0 && (int64_t)actMs > actBest;
    SetTextColor(dc, over ? C::Red : clockCol);
    TextOutW(dc, P + lb.cx + sp.cx, y + (LH - lb.cy) / 2, tA.c_str(), (int)tA.size());
    if (over) {
        SIZE ta;
        GetTextExtentPoint32W(dc, tA.c_str(), (int)tA.size(), &ta);
        std::wstring d = diffInParens((int64_t)actMs - actBest);
        TextOutW(dc, P + lb.cx + sp.cx + ta.cx + (int)(6 * S), y + (LH - lb.cy) / 2, d.c_str(), (int)d.size());
    }
    GetTextExtentPoint32W(dc, tB.c_str(), (int)tB.size(), &tb);
    GetTextExtentPoint32W(dc, lB.c_str(), (int)lB.size(), &lb);
    int xr = rc.right - P - tb.cx;
    SetTextColor(dc, g.cmpClock == Tracker::Clock::Running ? C::Cyan : clockCol);
    TextOutW(dc, xr, y + (LH - tb.cy) / 2, tB.c_str(), (int)tB.size());
    SetTextColor(dc, C::Comment);
    TextOutW(dc, xr - sp.cx - lb.cx, y + (LH - lb.cy) / 2, lB.c_str(), (int)lB.size());
}

void saveOverlayPos() {
    RECT wr;
    GetWindowRect(hOverlay, &wr);
    ovIniSet(L"X", wr.left);
    ovIniSet(L"Y", wr.top);
    ovIniSet(L"W", wr.right - wr.left);
    ovIniSet(L"H", wr.bottom - wr.top);
    anchorSave(hOverlay, kOvSec);
}

// size changed: scale = what fits both width and height, rounded corners follow the size
static void overlayResized(int w, int h) {
    double sc = std::min((double)w / kOvW, (double)h / ovHeight(1.0, gOvRecords));
    sc = std::clamp(sc, 0.6, 3.0);
    if (std::abs(sc - gOvScale) > 0.01 || !fOvHead) {
        gOvScale = sc;
        ovFonts(sc);
    }
    SetWindowRgn(hOverlay, CreateRoundRectRgn(0, 0, w + 1, h + 1, 14, 14), TRUE);
    InvalidateRect(hOverlay, nullptr, FALSE);
}

// keep the width, fit the height to the lines shown
static void ovFitHeight() {
    if (hOverlay) {
        RECT wr;
        GetWindowRect(hOverlay, &wr);
        double sc = std::clamp((double)(wr.right - wr.left) / kOvW, 0.6, 3.0);
        SetWindowPos(hOverlay, nullptr, 0, 0, wr.right - wr.left, ovHeight(sc, gOvRecords),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        saveOverlayPos();
    }
}

void setOverlayRecords(int n) {
    gOvRecords = std::clamp(n, 0, 10);
    ovIniSet(L"Records", gOvRecords);
    ovFitHeight();
}

BYTE overlayAlpha() { return (BYTE)std::lround(255 * (100 - gOvTransparency) / 100.0); }

// Applies to all overlays.
void setOverlayTransparency(int pct) {
    gOvTransparency = std::clamp(pct, 0, 100);
    ovIniSet(L"Transparency", gOvTransparency);
    SetWindowTextW(hAlphaVal, (std::to_wstring(gOvTransparency) + L"%").c_str());
    if (hAlphaSlider && sliderValue(hAlphaSlider) != gOvTransparency) setSliderValue(hAlphaSlider, gOvTransparency);
    if (hOverlay) SetLayeredWindowAttributes(hOverlay, 0, overlayAlpha(), LWA_ALPHA);
    if (hCmdBar) SetLayeredWindowAttributes(hCmdBar, 0, overlayAlpha(), LWA_ALPHA);
    if (hGraphOv) SetLayeredWindowAttributes(hGraphOv, 0, overlayAlpha(), LWA_ALPHA);
    if (hStepsOv) SetLayeredWindowAttributes(hStepsOv, 0, overlayAlpha(), LWA_ALPHA);
}

// Campaign footer: shown whenever the player is in a campaign zone. Experience lines: when turned on.
bool ovCampaignShown() { return gOvFoot; }

void ovSyncFooter() {
    bool want = g.curZone >= 0 || (g.cmpClock == Tracker::Clock::Stopped && Tracker::isCampaign(g.location));
    bool exp = gExpOnOverlay && gExpLevel > 0 && !expNoMonsters();
    std::wstring n, t, c;
    bool prev = want && gOvCompare && previousAct(nowTick(), n, t, c);
    if (want == gOvFoot && exp == gOvExp && prev == gOvFootPrev) return;
    gOvFootPrev = prev;
    gOvFoot = want;
    gOvExp = exp;
    ovFitHeight();
}

static LRESULT CALLBACK OverlayProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCCALCSIZE:  // no visible frame: the whole window is client area (frame only used for resizing)
            if (wp) return 0;
            break;
        case WM_NCACTIVATE:  // never paint the resize frame on activation, it would flash a wide border
            return DefWindowProcW(h, msg, wp, -1);
        case WM_NCHITTEST: {  // edges/corners resize, anywhere else drags
            POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
            RECT wr;
            GetWindowRect(h, &wr);
            bool L = pt.x < wr.left + kOvEdge, R = pt.x >= wr.right - kOvEdge;
            bool T = pt.y < wr.top + kOvEdge, B = pt.y >= wr.bottom - kOvEdge;
            if (T && L) return HTTOPLEFT;
            if (T && R) return HTTOPRIGHT;
            if (B && L) return HTBOTTOMLEFT;
            if (B && R) return HTBOTTOMRIGHT;
            if (L) return HTLEFT;
            if (R) return HTRIGHT;
            if (T) return HTTOP;
            if (B) return HTBOTTOM;
            return HTCAPTION;
        }
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mm = (MINMAXINFO*)lp;
            mm->ptMinTrackSize = {(LONG)(kOvW * 0.6), (LONG)ovHeight(0.6, gOvRecords)};
            mm->ptMaxTrackSize = {kOvW * 3, ovHeight(3.0, gOvRecords) + 400};
            return 0;
        }
        case WM_SIZE:
            overlayResized(LOWORD(lp), HIWORD(lp));
            return 0;
        case WM_NCLBUTTONDBLCLK:  // double-click: bring up the main window
        case WM_LBUTTONDBLCLK:
            showMain();
            return 0;
        case WM_NCRBUTTONDOWN:  // no system menu on the drag area
            return 0;
        case WM_NCRBUTTONUP:
        case WM_CONTEXTMENU:
            overlayMenu(h);
            return 0;
        case WM_MEASUREITEM:
            measureMenu((MEASUREITEMSTRUCT*)lp);
            return TRUE;
        case WM_DRAWITEM:
            drawMenu((DRAWITEMSTRUCT*)lp);
            return TRUE;
        case WM_ENTERSIZEMOVE:
            dragStart(h);
            return 0;
        case WM_EXITSIZEMOVE:
            saveOverlayPos();
            return 0;
        case WM_MOVING:
            snapOverlay(h, (RECT*)lp);
            return TRUE;
        case WM_ERASEBKGND:
        case WM_NCPAINT:
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            HDC mem = CreateCompatibleDC(dc);  // double buffer: no flicker every 0.5 s
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ ob = SelectObject(mem, bmp);
            paintOverlay(mem, rc);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, ob);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(h, &ps);
            return 0;
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void showOverlay(bool on) {
    gOverlayOn = on;
    if (on && !hOverlay) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = OverlayProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_SIZEALL);
        wc.lpszClassName = L"Poe2MapStatOverlay";
        wc.style = CS_DBLCLKS;
        RegisterClassExW(&wc);
        int x = ovIniInt(L"X", 40);
        int y = ovIniInt(L"Y", 40);
        int w = ovIniInt(L"W", kOvW);
        int h = ovIniInt(L"H", ovHeight((double)w / kOvW, gOvRecords));
        hOverlay = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
                                   wc.lpszClassName, L"MapTimerOverlay", WS_POPUP | WS_THICKFRAME, x, y, w, h, nullptr,
                                   nullptr, wc.hInstance, nullptr);
        SetLayeredWindowAttributes(hOverlay, 0, overlayAlpha(), LWA_ALPHA);
        hideFromCapture(hOverlay);
        DWMNCRENDERINGPOLICY ncr = DWMNCRP_DISABLED;  // no DWM frame/shadow around the overlay
        DwmSetWindowAttribute(hOverlay, DWMWA_NCRENDERING_POLICY, &ncr, sizeof ncr);
        overlayResized(w, h);
    }
    updateOverlayVisibility();
    if (hOverlayChk) InvalidateRect(hOverlayChk, nullptr, FALSE);
}
