// Campaign tab: act pills, zone list and totals; the Summary pill (one row per act) once the run is stopped.
#include "app.h"

static void syncActTabs();

// The first pill shows the whole run: all zones, and the header shows the campaign time instead of the zone time.
static const char* kAllActs = "Campaign";
// The last pill, once the run is stopped (or the summary was made from the timer's menu): one row per act.
static const char* kSummary = "Summary";
static bool gSumManual = false;  // Generate Summary (timer menu) for a run still going
static bool gWasStopped = false;

// Act sub-tabs: "Campaign" first, then one pill per act seen in the zones list, in order of first visit.
static void syncActTabs() {
    std::vector<std::string> acts = {kAllActs};
    for (auto& z : g.zones) {
        std::string a = Tracker::actOf(z.areaId);
        if (std::find(acts.begin(), acts.end(), a) == acts.end()) acts.push_back(a);
    }
    if ((g.cmpClock == Tracker::Clock::Stopped || gSumManual) && !g.zones.empty()) acts.push_back(kSummary);
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
    setTextIfChanged(hCmpStop, g.cmpClock == Tracker::Clock::Stopped ? L"Reset" : L"Stop");
}

// "21:13:15" -> seconds of the day, -1 if not readable
static int daySec(const std::string& t) {
    int h, m, s;
    return sscanf(t.c_str(), "%d:%d:%d", &h, &m, &s) == 3 ? h * 3600 + m * 60 + s : -1;
}

// The Summary pill: per act (in the order played) its time without loading screens, when it was started and left,
// its loading time, how many zones (towns not counted) and the average time per zone.
static void fillActSummary(uint32_t now) {
    struct Act {
        std::string name, start, end;
        int startSec = -1, endSec = -1;  // end: seconds after the start's day (a run can pass midnight)
        uint64_t ms = 0, zoneMs = 0;
        double load = 0;
        int zones = 0;
    };
    std::vector<Act> acts;
    for (const Zone& z : g.zones) {
        std::string a = Tracker::actOf(z.areaId);
        auto it = std::find_if(acts.begin(), acts.end(), [&](const Act& x) { return x.name == a; });
        if (it == acts.end()) {
            acts.push_back(Act{});
            acts.back().name = a;
            it = acts.end() - 1;
        }
        Act& x = *it;
        uint64_t ms = z.activeAt(now);
        x.ms += ms;
        x.load += z.loadSec;
        if (!z.town) x.zones++, x.zoneMs += ms;
        if (x.start.empty() && !z.firstEnter.empty()) x.start = z.firstEnter, x.startSec = daySec(z.firstEnter);
        std::string end = z.inSegment ? g.lastTime : !z.lastLeft.empty() ? z.lastLeft : z.lastEnter;
        int e = daySec(end);
        if (e >= 0 && x.startSec >= 0) {
            if (e < x.startSec) e += 86400;
            if (e > x.endSec) x.endSec = e, x.end = end;
        }
    }
    std::vector<LvRow> rows;
    uint64_t total = 0, totalZoneMs = 0;
    double load = 0;
    int zones = 0;
    for (size_t i = 0; i < acts.size(); ++i) {
        const Act& a = acts[i];
        total += a.ms, totalZoneMs += a.zoneMs, load += a.load, zones += a.zones;
        rows.push_back({{W(a.name), W(fmtDuration(a.ms)), W(a.start), W(a.end), secs(a.load), std::to_wstring(a.zones),
                         a.zones ? W(fmtDuration(a.zoneMs / a.zones)) : L"-"},
                        (int)i});
    }
    sortRows(hCmpActList, rows);
    if (!acts.empty())
        rows.push_back({{L"Campaign", W(fmtDuration(total)), W(acts.front().start), W(acts.back().end), secs(load),
                         std::to_wstring(zones), zones ? W(fmtDuration(totalZoneMs / zones)) : L"-"},
                        (int)acts.size()});
    writeRows(hCmpActList, rows);
    setTextIfChanged(hCmpSum, std::to_wstring(acts.size()) + L" acts  \u00b7  " + std::to_wstring(zones) +
                                  L" zones  \u00b7  total " + W(fmtDuration(total)) + L"  \u00b7  load " + secs(load) +
                                  L"  \u00b7  times without loading screens");
}

void campaignShowLists() {
    if (!hCmpActList) return;
    bool page = gPage == PG_CAMPAIGN, sum = gSelAct == kSummary;
    ShowWindow(hCmpList, page && !sum ? SW_SHOW : SW_HIDE);
    ShowWindow(hCmpActList, page && sum ? SW_SHOW : SW_HIDE);
    setTextIfChanged(hLblCmp, sum ? L"ACTS" : L"ZONES");
}

void campaignTimerMenu(POINT pt) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (g.zones.empty() ? MF_GRAYED : 0), 1, L"Generate Summary");
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hMain, nullptr);
    DestroyMenu(m);
    if (cmd == 1) {
        gSumManual = true;
        refreshCampaign(true);
        selectAct(kSummary);
        refreshCampaign(true);
    }
}

void refreshCampaign(bool force) {
    uint32_t now = nowTick();
    g.expireZones(now);
    syncClockButtons();
    bool stopped = g.cmpClock == Tracker::Clock::Stopped;
    if (g.zones.empty()) gSumManual = false;  // a new run
    syncActTabs();
    if (stopped && !gWasStopped && !g.zones.empty()) selectAct(kSummary);  // the run was just stopped
    gWasStopped = stopped;
    bool all = gSelAct == kAllActs || gSelAct == kSummary;
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
        setTextIfChanged(hCmpStatus, gHotkeysOn ? L"Press " + hotkeyName(HK_START) + L" or Reset to begin a new run"
                                                : std::wstring(L"Press Reset to begin a new run"));
    } else {
        setTextIfChanged(hCmpTimerLoad, L"");
        setTextIfChanged(hCmpTitle, L"Not in a campaign zone");
        setTextIfChanged(hCmpTimer, L"00:00");
        setTextIfChanged(hCmpStatus, W("In " + g.locationDisplay()));
    }
    InvalidateRect(hCmpTimer, nullptr, FALSE);
    InvalidateRect(hCmpTimerLoad, nullptr, FALSE);

    // entering a zone (or a new act) switches to that act's tab, unless the whole campaign is shown
    if (g.curZone != gLastCurZone) {
        gLastCurZone = g.curZone;
        if (g.curZone >= 0 && gSelAct != kAllActs && gSelAct != kSummary)
            selectAct(Tracker::actOf(g.zones[g.curZone].areaId));
    }
    campaignShowLists();
    if (gSelAct == kSummary) {
        fillActSummary(now);
        return;
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
