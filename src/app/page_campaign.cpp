// Campaign tab: act pills, zone list and totals.
#include "app.h"

static void syncActTabs();

// The first pill shows the whole run: all zones, and the header shows the campaign time instead of the zone time.
static const char* kAllActs = "Campaign";

// Act sub-tabs: "Campaign" first, then one pill per act seen in the zones list, in order of first visit.
static void syncActTabs() {
    std::vector<std::string> acts = {kAllActs};
    for (auto& z : g.zones) {
        std::string a = Tracker::actOf(z.areaId);
        if (std::find(acts.begin(), acts.end(), a) == acts.end()) acts.push_back(a);
    }
    if (acts == gActs) return;
    for (HWND h : gActBtns) {
        auto& pg = gPages[PG_CAMPAIGN];
        pg.erase(std::remove(pg.begin(), pg.end(), h), pg.end());
        gBtnKind.erase(h);
        DestroyWindow(h);
    }
    gActBtns.clear();
    gActs = acts;
    for (size_t i = 0; i < acts.size(); ++i) {
        HWND h = btn(W(acts[i]).c_str(), ID_ACT_FIRST + (int)i, PG_CAMPAIGN, Btn::Pill);
        gActBtns.push_back(h);
        if (gPage == PG_CAMPAIGN) ShowWindow(h, SW_SHOW);
    }
    if (std::find(acts.begin(), acts.end(), gSelAct) == acts.end()) gSelAct = acts.empty() ? "" : acts.back();
    placeActs();
}

void selectAct(const std::string& act) {
    if (act == gSelAct) return;
    gSelAct = act;
    gCmpShownAct = "\x01";  // force list rebuild
    for (HWND h : gActBtns) InvalidateRect(h, nullptr, FALSE);
}

// Pause / Stop buttons follow the campaign clock.
static void syncClockButtons() {
    setTextIfChanged(hCmpPause, g.cmpClock == Tracker::Clock::Paused ? L"Resume" : L"Pause");
    setTextIfChanged(hCmpStop, g.cmpClock == Tracker::Clock::Stopped ? L"Start" : L"Stop");
}

void refreshCampaign(bool force) {
    uint32_t now = nowTick();
    g.expireZones(now);
    syncClockButtons();
    bool stopped = g.cmpClock == Tracker::Clock::Stopped;
    syncActTabs();
    bool all = gSelAct == kAllActs;
    if (all) {  // whole run: the sum of all zones, so pause / stop / reset apply to it the same way
        uint64_t ms = 0;
        double load = 0;
        for (auto& z : g.zones) {
            ms += z.activeAt(now);
            load += z.loadSec;
        }
        const char* state = stopped ? "Stopped" : g.cmpClock == Tracker::Clock::Paused ? "Paused" : "Running";
        std::string where = g.curZone >= 0 ? "  \u00b7  in " + g.zones[g.curZone].name + " (" +
                                                 Tracker::actOf(g.zones[g.curZone].areaId) + ")"
                                           : "";
        setTextIfChanged(hCmpTitle, L"Campaign");
        setTextIfChanged(hCmpTimer, W(fmtDuration(ms)));
        setTextIfChanged(hCmpTimerLoad, W(fmtDuration(ms + (uint64_t)(load * 1000.0 + 0.5))) + L" with loading");
        setTextIfChanged(hCmpStatus,
                         W(std::string(state) + "  \u00b7  " + std::to_string(g.zones.size()) + " zone runs" + where));
    } else if (g.curZone >= 0 && unnamed(g.zones[g.curZone].name, g.zones[g.curZone].areaId)) {
        // name not known yet: keep the previous title
    } else if (g.curZone >= 0) {
        const Zone& z = g.zones[g.curZone];
        const char* state = g.cmpClock == Tracker::Clock::Paused ? "Paused  \u00b7  "
                            : z.inSegment                        ? ""
                                                                 : "Loading  \u00b7  ";
        setTextIfChanged(hCmpTitle, areaName(z.name, z.areaId) + L"  \u00b7  lvl " + std::to_wstring(z.level));
        setTextIfChanged(hCmpTimer, W(fmtDuration(z.activeAt(now))));
        setTextIfChanged(hCmpTimerLoad,
                         W(fmtDuration(z.activeAt(now) + (uint64_t)(z.loadSec * 1000.0 + 0.5))) + L" with loading");
        setTextIfChanged(hCmpStatus,
                         W(std::string(state) + Tracker::actOf(z.areaId) + "  \u00b7  " + z.areaId + "  \u00b7  seed " +
                           std::to_string(z.seed) + "  \u00b7  entries " + std::to_string(z.entries)));
    } else if (stopped) {
        uint64_t total = 0;
        for (auto& z : g.zones) total += z.activeMs;
        setTextIfChanged(hCmpTitle, L"Campaign run stopped");
        setTextIfChanged(hCmpTimer, W(fmtDuration(total)));
        setTextIfChanged(hCmpTimerLoad, L"");
        setTextIfChanged(hCmpStatus, gHotkeysOn ? L"Press " + hotkeyName(HK_START) + L" or Start to begin a new run"
                                                : std::wstring(L"Press Start to begin a new run"));
    } else {
        setTextIfChanged(hCmpTimerLoad, L"");
        setTextIfChanged(hCmpTitle, L"Not in a campaign zone");
        setTextIfChanged(hCmpTimer, L"00:00");
        setTextIfChanged(hCmpStatus, W("In " + (g.locationName.empty() ? g.location : g.locationName)));
    }
    InvalidateRect(hCmpTimer, nullptr, FALSE);
    InvalidateRect(hCmpTimerLoad, nullptr, FALSE);

    // entering a zone (or a new act) switches to that act's tab, unless the whole campaign is shown
    if (g.curZone != gLastCurZone) {
        gLastCurZone = g.curZone;
        if (g.curZone >= 0 && gSelAct != kAllActs) selectAct(Tracker::actOf(g.zones[g.curZone].areaId));
    }

    // rows of the selected act
    std::vector<int> rows;
    for (size_t i = 0; i < g.zones.size(); ++i)
        if ((gSelAct == kAllActs || Tracker::actOf(g.zones[i].areaId) == gSelAct) &&
            !unnamed(g.zones[i].name, g.zones[i].areaId) &&
            searchMatch(hCmpList, areaName(g.zones[i].name, g.zones[i].areaId), W(g.zones[i].areaId)))
            rows.push_back((int)i);
    bool rebuild = force || gCmpShownAct != gSelAct || rows.size() != gCmpRows.size();
    gCmpShownAct = gSelAct;
    uint64_t total = 0;
    int entries = 0;
    double load = 0;
    std::string first, last;
    std::vector<LvRow> lv;
    for (size_t r = 0; r < rows.size(); ++r) {
        const Zone& z = g.zones[rows[r]];
        uint64_t ms = z.activeAt(now);
        total += ms;
        entries += z.entries;
        load += z.loadSec;
        if (first.empty() && !z.firstEnter.empty()) first = z.firstEnter;
        if (!z.lastEnter.empty()) last = z.lastEnter;
        lv.push_back({{std::to_wstring(r + 1), areaName(z.name, z.areaId) + (z.town ? L"  (town)" : L""),
                       std::to_wstring(z.level), W(z.areaId), std::to_wstring(z.seed), W(z.firstEnter), W(z.lastEnter),
                       std::to_wstring(z.entries), W(fmtDuration(ms)), secs(z.loadSec),
                       z.entries ? secs(z.loadSec / z.entries) : L"-"},
                      rows[r]});
    }
    sortRows(hCmpList, lv);
    writeRows(hCmpList, lv);
    gCmpRows.clear();
    for (auto& row : lv) gCmpRows.push_back(row.tag);
    size_t nz = rows.size();
    std::wstring sum = std::to_wstring(nz) + L" zone runs  \u00b7  total " + W(fmtDuration(total));
    if (nz) {
        char avgEnt[16];
        snprintf(avgEnt, sizeof avgEnt, "%.1f", (double)entries / nz);
        sum += L"  \u00b7  average " + W(fmtDuration(total / nz)) + L"  \u00b7  load " + secs(load) + L" (avg " +
               (entries ? secs(load / entries) : L"-") + L")  \u00b7  entries " + std::to_wstring(entries) + L" (avg " +
               W(avgEnt) + L")  \u00b7  " + W(first) + L" -> " + W(last);
    }
    setTextIfChanged(hCmpSum, sum);
    if (rebuild) {
        int sel = -1;
        for (size_t r = 0; r < gCmpRows.size(); ++r)
            if (gCmpRows[r] == g.curZone) sel = (int)r;
        if (sel >= 0) ListView_EnsureVisible(hCmpList, sel, FALSE);
        else if (nz) ListView_EnsureVisible(hCmpList, (int)nz - 1, FALSE);
    }
    InvalidateRect(hCmpList, nullptr, FALSE);  // current-zone highlight
}

void placeActs() {
    int x = gActRowX;
    HDC dc = GetDC(hMain);
    HGDIOBJ of = SelectObject(dc, fBold);
    for (HWND h : gActBtns) {
        wchar_t t[64];
        GetWindowTextW(h, t, 64);
        SIZE sz;
        GetTextExtentPoint32W(dc, t, (int)wcslen(t), &sz);
        int w = sz.cx + 32;
        MoveWindow(h, x, gActRowY, w, 30, TRUE);
        x += w + 8;
    }
    SelectObject(dc, of);
    ReleaseDC(hMain, dc);
}
