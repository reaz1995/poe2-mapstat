// Summary tab: one row per map type, plus the Area Lvl hover breakdown.
#include "app.h"

struct Agg {
    std::string name;
    int count = 0, minLvl = 999, maxLvl = 0;
    uint64_t totalMs = 0, bestMs = UINT64_MAX;
    int runCount = 0;        // part of count that are runs of their own (sub-area visits are inside a map's time)
    uint64_t runMs = 0;
};

// Small themed popup (not a system tooltip, so it follows the colours of the loaded theme).
struct TipRow { int lvl; int count; uint64_t ms; };

static std::vector<TipRow> levelBreakdown(const std::string& areaId);
static void paintTip(HDC dc, RECT rc);
static LRESULT CALLBACK TipProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
static void showTip(const std::wstring& key, const std::wstring& title, const std::string& areaId);

void refreshSummary() {
    std::map<std::string, Agg> m;
    auto one = [&](const std::string& id, const std::string& name, int level, uint64_t ms, bool run) {
        Agg& a = m[id];
        if (a.name.empty()) a.name = name;
        a.count++;
        a.totalMs += ms;
        if (run) a.runCount++, a.runMs += ms;
        if (ms < a.bestMs) a.bestMs = ms;
        a.minLvl = std::min(a.minLvl, level);
        a.maxLvl = std::max(a.maxLvl, level);
    };
    auto add = [&](const Run& r, uint64_t ms) {
        if (r.entries == 0 || ms == 0) return;  // generated but never entered (no loading screen): not a play
        one(r.areaId, r.name, r.level, ms, true);
        for (auto& s : r.subs)  // Abyssal Depths, Lightless Void, ... also get their own row (time is in the map too)
            if (s.ms > 0) one(s.areaId, g.nameOf(s.areaId), s.level, s.ms, false);
    };
    bool withCur = false;
    for (auto& [r, ms] : g.summaryRuns(nowTick())) {
        add(*r, ms);
        if (r == &g.cur) withCur = true;
    }

    std::vector<std::pair<std::string, Agg>> rows(m.begin(), m.end());
    std::sort(rows.begin(), rows.end(), [](auto& a, auto& b) {
        return a.second.count != b.second.count ? a.second.count > b.second.count : a.second.totalMs > b.second.totalMs;
    });

    int maps = 0, kinds = 0, subVisits = 0;
    uint64_t total = 0;
    std::vector<LvRow> lv;
    for (auto& [id, a] : rows) {
        if (!searchMatch(hSumList, areaName(a.name, id), W(id))) continue;
        ++kinds;
        maps += a.runCount;   // header: maps played / time once (sub-area time is already inside its map)
        subVisits += a.count - a.runCount;
        total += a.runMs;
        std::wstring lvl = a.minLvl == a.maxLvl ? std::to_wstring(a.minLvl)
                                                : std::to_wstring(a.minLvl) + L"-" + std::to_wstring(a.maxLvl);
        lv.push_back({{areaName(a.name, id), lvl, W(id), std::to_wstring(a.count), W(fmtDuration(a.totalMs)),
                       W(fmtDuration(a.totalMs / a.count)), W(fmtDuration(a.bestMs))},
                      0});
    }
    sortRows(hSumList, lv);
    writeRows(hSumList, lv);
    setTextIfChanged(hSumHdr, std::to_wstring(kinds) + L" different maps  \u00b7  " + std::to_wstring(maps) +
                                  L" played  \u00b7  total " + W(fmtDuration(total)) + L"  \u00b7  average " +
                                  W(fmtDuration(maps ? total / maps : 0)) +
                                  (subVisits ? L"  \u00b7  " + std::to_wstring(subVisits) + L" sub-area visits (time inside their maps)" : L"") +
                                  (withCur ? L"  (incl. current map)" : L""));
}

static HWND hTip = nullptr;

static std::wstring gTipKey, gTipTitle;

static std::vector<TipRow> gTipRows;

static const int kTipPad = 10, kTipLine = 20;

static std::vector<TipRow> levelBreakdown(const std::string& areaId) {
    std::map<int, TipRow> m;  // same runs as the Summary rows
    auto put = [&](int lvl, uint64_t ms) {
        TipRow& t = m[lvl];
        t.lvl = lvl;
        t.count++;
        t.ms += ms;
    };
    auto add = [&](const Run& r, uint64_t ms) {
        if (r.entries == 0 || ms == 0) return;
        if (r.areaId == areaId) put(r.level, ms);
        for (auto& s : r.subs)
            if (s.areaId == areaId && s.ms > 0) put(s.level, s.ms);
    };
    for (auto& [r, ms] : g.summaryRuns(nowTick())) add(*r, ms);
    std::vector<TipRow> v;
    for (auto it = m.rbegin(); it != m.rend(); ++it) v.push_back(it->second);  // highest level first
    return v;
}

static void paintTip(HDC dc, RECT rc) {
    fillRound(dc, rc, 8, C::Card, C::Line);
    SetBkMode(dc, TRANSPARENT);
    int x = kTipPad, y = kTipPad, w = rc.right - 2 * kTipPad;
    RECT t = {x, y, x + w, y + kTipLine};
    SelectObject(dc, fBold);
    SetTextColor(dc, C::Fg);
    DrawTextW(dc, gTipTitle.c_str(), -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    y += kTipLine + 4;
    RECT sep = {x, y, x + w, y + 1};
    HBRUSH b = CreateSolidBrush(C::Line);
    FillRect(dc, &sep, b);
    DeleteObject(b);
    y += 5;
    SelectObject(dc, fNormal);
    for (auto& r : gTipRows) {
        RECT a = {x, y, x + 70, y + kTipLine}, c = {x + 70, y, x + 150, y + kTipLine}, d = {x + 150, y, x + w, y + kTipLine};
        SetTextColor(dc, C::Pink);
        DrawTextW(dc, (L"Lvl " + std::to_wstring(r.lvl)).c_str(), -1, &a, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(dc, C::Fg);
        DrawTextW(dc, (std::to_wstring(r.count) + (r.count == 1 ? L" map" : L" maps")).c_str(), -1, &c,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(dc, C::Comment);
        DrawTextW(dc, (W(fmtDuration(r.ms)) + L"   avg " + W(fmtDuration(r.ms / r.count))).c_str(), -1, &d,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        y += kTipLine;
    }
}

static LRESULT CALLBACK TipProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(mem, bmp);
        FillRect(mem, &rc, brBg);
        paintTip(mem, rc);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(h, msg, wp, lp);
}

void hideTip() {
    if (hTip) ShowWindow(hTip, SW_HIDE);
    gTipKey.clear();
}

static void showTip(const std::wstring& key, const std::wstring& title, const std::string& areaId) {
    if (key == gTipKey && hTip && IsWindowVisible(hTip)) return;
    if (!hTip) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = TipProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"Poe2MapStatTip";
        RegisterClassExW(&wc);
        hTip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP, 0, 0,
                               10, 10, hMain, nullptr, wc.hInstance, nullptr);
    }
    gTipKey = key;
    gTipTitle = title;
    gTipRows = levelBreakdown(areaId);
    if (gTipRows.empty()) return hideTip();
    int w = 320, hgt = 2 * kTipPad + kTipLine + 9 + (int)gTipRows.size() * kTipLine;
    POINT pt;
    GetCursorPos(&pt);
    MONITORINFO mi = {};
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    int x = std::min(pt.x + 16, mi.rcWork.right - w), y = pt.y + 20;
    if (y + hgt > mi.rcWork.bottom) y = std::max((int)mi.rcWork.top, (int)pt.y - 8 - hgt);  // flip above the cursor
    SetWindowRgn(hTip, CreateRoundRectRgn(0, 0, w + 1, hgt + 1, 8, 8), FALSE);
    SetWindowPos(hTip, HWND_TOPMOST, x, y, w, hgt, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(hTip, nullptr, TRUE);
}

// called from the list subclass on mouse move over the Summary list
void summaryHover(HWND lv, LPARAM lp) {
    LVHITTESTINFO ht = {};
    ht.pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
    ListView_SubItemHitTest(lv, &ht);
    if (ht.iItem < 0 || ht.iSubItem != 1) return hideTip();
    wchar_t name[256] = L"", id[256] = L"";
    ListView_GetItemText(lv, ht.iItem, 0, name, 255);
    ListView_GetItemText(lv, ht.iItem, 2, id, 255);
    TRACKMOUSEEVENT t = {};
    t.cbSize = sizeof t;
    t.dwFlags = TME_LEAVE;
    t.hwndTrack = lv;
    TrackMouseEvent(&t);
    showTip(std::to_wstring(ht.iItem) + L"|" + id, name, U8(id));
}
