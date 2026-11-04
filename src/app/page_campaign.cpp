// Campaign tab: act pills, zone list and totals; the Act Summary pill (one row per act) once the run
// is stopped; the Summary pill (saved runs, their comparison).
#include "app.h"
#include "area_tables.h"

#include <algorithm>
#include <map>

static void syncActTabs();

// The first pill shows the whole run: all zones, and the header shows the campaign time instead of the zone time.
static const char* kAllActs = "Campaign";
// The last pill, once the run is stopped (or the summary was made from the timer's menu): one row per act.
static const char* kSummary = "Act Summary";
static bool gSumManual = false;  // Generate Act Summary (timer menu) for a run still going
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

// "21:13:15" -> seconds of the day, -1 if not readable
static int daySec(const std::string& t) {
    int h, m, s;
    return sscanf(t.c_str(), "%d:%d:%d", &h, &m, &s) == 3 ? h * 3600 + m * 60 + s : -1;
}

// The Act Summary pill: per act (in the order played) its time without loading screens, when it was started and left,
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

// Right-click on the big timer: the same clock controls as the overlay's menu, and the summary.
void campaignTimerMenu(POINT pt) {
    enum { CMD_SUMMARY = 1, CMD_PAUSE, CMD_STOP, CMD_CLEAR };
    bool stopped = g.cmpClock == Tracker::Clock::Stopped;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (stopped ? MF_GRAYED : 0), CMD_PAUSE,
                g.cmpClock == Tracker::Clock::Paused ? L"Resume" : L"Pause");
    AppendMenuW(m, MF_STRING | (stopped ? MF_GRAYED : 0), CMD_STOP, L"Stop");  // a new run after it: Save and Clear
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g.zones.empty() ? MF_GRAYED : 0), CMD_SUMMARY, L"Generate Act Summary");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CMD_CLEAR, L"Save and Clear");  // the run goes to the saved runs, a new one starts
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hMain, nullptr);
    DestroyMenu(m);
    if (cmd == CMD_PAUSE) campaignAction(HK_PAUSE);
    else if (cmd == CMD_STOP) campaignAction(HK_STOP);
    else if (cmd == CMD_CLEAR) resetCampaignRun();
    if (cmd == CMD_SUMMARY) {
        gSumManual = true;
        refreshCampaign(true);
        selectAct(kSummary);
        refreshCampaign(true);
    }
}

void refreshCampaign(bool force) {
    uint32_t now = nowTick();
    g.expireZones(now);
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
        std::wstring how = L"right-click the timer \u2192 Save and Clear to begin a new run";
        std::wstring key = hotkeyName(HK_START);
        setTextIfChanged(hCmpStatus, key != L"Not set" ? L"Press " + key + L" or " + how : L"R" + how.substr(1));
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

// ---------------------------------------------------------------- Summary pill (Session | Summary): the saved runs

namespace {

struct SavedRun {
    std::wstring path, name;
    std::map<std::string, uint64_t> acts;  // act name -> time without loading screens
    uint64_t total = 0;
    bool current = false, stopped = false;
    std::vector<std::string> order;  // acts in the order they were played
    // per act its zones in the order entered, with the fastest instance when a zone was played more than once
    std::map<std::string, std::vector<std::pair<std::string, uint64_t>>> zones;
    // a finished act: another one was played after it, or the run was stopped
    bool finished(const std::string& act) const { return stopped || (!order.empty() && order.back() != act); }
};
std::vector<SavedRun> gRuns;  // in list order
std::vector<std::string> gRunActs;  // the act columns, in campaign order
bool gRunsFilling = false;  // the list is being refilled: selection changes are not the user's
bool gRenaming = false;     // a name edit opened from the menu (clicking a selected run must not start one)

std::wstring runKey(const std::wstring& path) { return path.substr(path.find_last_of(L"\\/") + 1); }

// "CampaignSession 2026-11-03 15-03-44.tsv" -> "2026-11-03 15:03" unless the run was given a name
std::wstring runName(const std::wstring& path) {
    wchar_t b[200] = L"";
    GetPrivateProfileStringW(L"CampaignRuns", runKey(path).c_str(), L"", b, 200, iniPath().c_str());
    if (*b) return b;
    std::wstring n = runKey(path);
    size_t a = n.find(L' ');
    n = a == std::wstring::npos ? n : n.substr(a + 1);
    if (n.size() > 4) n.resize(n.size() - 4);  // .tsv
    if (n.size() >= 16 && n[13] == L'-') n[13] = L':', n.resize(16);  // drop the seconds
    return n;
}

// the acts in campaign order (as acts.txt lists them), acts it doesn't know after them
int actRank(const std::string& act) {
    const auto& e = actTable().entries();
    for (size_t i = 0; i < e.size(); ++i)
        if ((knownName(e[i].second) ? e[i].second : e[i].first) == act) return (int)i;
    return 1000;
}

// The run pinned for the overlay's zone comparison (right-click → Pin), by file name; "" = none (best of all runs)
std::wstring pinnedRun() {
    wchar_t b[260] = L"";
    GetPrivateProfileStringW(L"CampaignRuns", L"Pin", L"", b, 260, iniPath().c_str());
    return b;
}
const wchar_t* kPin = L"\U0001F4CC ";  // shown left of the pinned run's name

// The runs selected for comparison (Ctrl + click adds / removes), by file name in the order they were selected:
// the first one is the base the others are compared with.
std::vector<std::wstring> gCompare;
std::vector<int> gCmpRowRun;  // comparison list row -> index in gRuns (row 0 is the base)

void loadCompared() {
    gCompare.clear();
    wchar_t b[4096] = L"";
    GetPrivateProfileStringW(L"CampaignRuns", L"Compare", L"", b, 4096, iniPath().c_str());
    std::wstring v = b;
    for (size_t a = 0; a <= v.size();) {
        size_t e = v.find(L'|', a);
        if (e == std::wstring::npos) e = v.size();
        if (e > a) gCompare.push_back(v.substr(a, e - a));
        a = e + 1;
    }
}

void saveCompared() {
    std::wstring v;
    for (auto& k : gCompare) v += (v.empty() ? L"" : L"|") + k;
    WritePrivateProfileStringW(L"CampaignRuns", L"Compare", v.empty() ? nullptr : v.c_str(), iniPath().c_str());
}

const SavedRun* runOf(const std::wstring& key) {
    for (auto& r : gRuns)
        if (runKey(r.path) == key) return &r;
    return nullptr;
}

// "+15s", "-25s", "+2m03s", "+1h02m"
std::wstring diffText(int64_t ms) {
    wchar_t b[32];
    int64_t s = (ms < 0 ? -ms : ms) / 1000;
    const wchar_t* sign = ms < 0 ? L"-" : L"+";
    if (s < 60) swprintf(b, 32, L"%ls%llds", sign, (long long)s);
    else if (s < 3600) swprintf(b, 32, L"%ls%lldm%02llds", sign, (long long)(s / 60), (long long)(s % 60));
    else swprintf(b, 32, L"%ls%lldh%02lldm", sign, (long long)(s / 3600), (long long)(s % 3600 / 60));
    return b;
}

// Column order of both run lists, by column name ([order] CampaignRuns = Run|Act 2|Act 1|Campaign): names it doesn't
// list keep their place after the listed ones.
std::wstring columnName(HWND lv, int c) {
    wchar_t t[64] = L"";
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT;
    col.pszText = t;
    col.cchTextMax = 63;
    ListView_GetColumn(lv, c, &col);
    return t;
}

void applyRunsOrder(HWND lv) {
    int n = Header_GetItemCount(ListView_GetHeader(lv));
    wchar_t b[1024] = L"";
    GetPrivateProfileStringW(L"order", L"CampaignRuns", L"", b, 1024, iniPath().c_str());
    std::vector<std::wstring> saved;
    for (std::wstring v = b; !v.empty();) {
        size_t e = v.find(L'|');
        saved.push_back(v.substr(0, e));
        v = e == std::wstring::npos ? L"" : v.substr(e + 1);
    }
    std::vector<int> o;
    std::vector<bool> used(n, false);
    for (auto& name : saved)
        for (int c = 0; c < n; ++c)
            if (!used[c] && columnName(lv, c) == name) o.push_back(c), used[c] = true;
    for (int c = 0; c < n; ++c)
        if (!used[c]) o.push_back(c);
    ListView_SetColumnOrderArray(lv, n, o.data());
    InvalidateRect(lv, nullptr, TRUE);
}

void setColumns(HWND lv) {  // Run, one per act, Campaign
    while (ListView_DeleteColumn(lv, 0)) {}
    auto addCol = [lv](int i, const std::wstring& t, int w, bool right) {
        LVCOLUMNW c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        c.fmt = right ? LVCFMT_RIGHT : LVCFMT_LEFT;
        c.pszText = (LPWSTR)t.c_str();
        c.cx = w;
        ListView_InsertColumn(lv, i, &c);
    };
    addCol(0, L"Run", 200, false);
    for (size_t i = 0; i < gRunActs.size(); ++i) addCol((int)i + 1, W(gRunActs[i]), 105, true);
    addCol((int)gRunActs.size() + 1, L"Campaign", 110, true);
    applyRunsOrder(lv);
}

// The comparison list: the base run with its times, then each other selected run as the difference to it.
void fillComparison() {
    if (!hCmpCmpList) return;
    SendMessageW(hCmpCmpList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(hCmpCmpList);
    setColumns(hCmpCmpList);
    const SavedRun* base = gCompare.empty() ? nullptr : runOf(gCompare[0]);
    int row = 0;
    gCmpRowRun.clear();
    for (auto& key : gCompare) {
        const SavedRun* r = runOf(key);
        if (!r) continue;
        LVITEMW it = {};
        it.mask = LVIF_TEXT;
        it.iItem = row;
        std::wstring name = r == base ? r->name + L"  (base)" : r->name;
        it.pszText = (LPWSTR)name.c_str();
        ListView_InsertItem(hCmpCmpList, &it);
        auto cell = [&](int col, uint64_t mine, bool have, uint64_t theirs, bool baseHas) {
            std::wstring v = !have ? L"-" : r == base || !baseHas ? W(fmtDuration(mine))
                                                                  : diffText((int64_t)mine - (int64_t)theirs);
            ListView_SetItemText(hCmpCmpList, row, col, (LPWSTR)v.c_str());
        };
        for (size_t i = 0; i < gRunActs.size(); ++i) {
            auto f = r->acts.find(gRunActs[i]);
            auto b = base->acts.find(gRunActs[i]);
            cell((int)i + 1, f == r->acts.end() ? 0 : f->second, f != r->acts.end(),
                 b == base->acts.end() ? 0 : b->second, b != base->acts.end());
        }
        cell((int)gRunActs.size() + 1, r->total, true, base->total, true);
        gCmpRowRun.push_back((int)(r - gRuns.data()));
        ++row;
    }
    SendMessageW(hCmpCmpList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hCmpCmpList, nullptr, TRUE);
}

}  // namespace

void refreshCampaignRuns() {
    invalidateBestZones();
    if (!hCmpRunList) return;
    saveData();  // the current run's file is up to date
    uint32_t now = nowTick();
    gRuns.clear();
    std::vector<std::string> acts;
    for (auto& path : campaignRunFiles()) {
        SavedRun r;
        r.path = path;
        r.name = runName(path);
        r.current = path == gCmpFile.path;
        Tracker t;
        if (!r.current) t.deserialize(readFileUtf8(path), Tracker::kCampaign);
        const Tracker& src = r.current ? g : t;
        r.stopped = src.cmpClock == Tracker::Clock::Stopped;
        for (const Zone& z : src.zones) {
            uint64_t ms = (r.current ? z.activeAt(now) : z.activeMs) +
                          (gRunsLoad ? (uint64_t)(z.loadSec * 1000.0 + 0.5) : 0);  // Include loading time
            std::string a = Tracker::actOf(z.areaId);
            r.acts[a] += ms;
            r.total += ms;
            if (!z.town && ms) {
                auto& v = r.zones[a];
                std::string n = U8(areaName(z.name, z.areaId));
                auto f = std::find_if(v.begin(), v.end(), [&](auto& e) { return e.first == n; });
                if (f == v.end()) v.push_back({n, ms});
                else f->second = std::min(f->second, ms);
            }
            if (std::find(r.order.begin(), r.order.end(), a) == r.order.end()) r.order.push_back(a);
            if (std::find(acts.begin(), acts.end(), a) == acts.end()) acts.push_back(a);
        }
        gRuns.push_back(std::move(r));
    }
    std::stable_sort(acts.begin(), acts.end(), [](auto& a, auto& b) { return actRank(a) < actRank(b); });
    gRunActs = acts;
    // the best run per act (finished acts only) and for the whole campaign (stopped runs only)
    std::vector<int> bestOf(acts.size(), -1);
    int bestTotal = -1;
    for (size_t r = 0; r < gRuns.size(); ++r) {
        const SavedRun& x = gRuns[r];
        for (size_t i = 0; i < acts.size(); ++i) {
            auto f = x.acts.find(acts[i]);
            if (f == x.acts.end() || !f->second || !x.finished(acts[i])) continue;
            if (bestOf[i] < 0 || f->second < gRuns[bestOf[i]].acts.at(acts[i])) bestOf[i] = (int)r;
        }
        if (x.stopped && x.total && (bestTotal < 0 || x.total < gRuns[bestTotal].total)) bestTotal = (int)r;
    }
    gRunsFilling = true;
    loadCompared();
    size_t had = gCompare.size();
    gCompare.erase(std::remove_if(gCompare.begin(), gCompare.end(), [](auto& k) { return !runOf(k); }), gCompare.end());
    if (gCompare.size() != had) saveCompared();  // runs removed meanwhile leave the comparison
    if (std::wstring pin = pinnedRun(); !pin.empty() && !runOf(pin))  // the pinned run is gone: back to the best
        WritePrivateProfileStringW(L"CampaignRuns", L"Pin", nullptr, iniPath().c_str());

    // columns: Run, one per act, Campaign
    SendMessageW(hCmpRunList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(hCmpRunList);
    setColumns(hCmpRunList);

    for (size_t r = 0; r < gRuns.size(); ++r) {
        const SavedRun& x = gRuns[r];
        LVITEMW it = {};
        it.mask = LVIF_TEXT;
        it.iItem = (int)r;
        std::wstring name = (runKey(x.path) == pinnedRun() ? kPin : L"") + x.name;
        it.pszText = (LPWSTR)name.c_str();
        ListView_InsertItem(hCmpRunList, &it);
        for (size_t i = 0; i < acts.size(); ++i) {
            auto f = x.acts.find(acts[i]);
            std::wstring v = f == x.acts.end() ? L"-" : W(fmtDuration(f->second));
            if ((int)r == bestOf[i]) v += L"  \u2605";  // the best time of all runs for this act
            ListView_SetItemText(hCmpRunList, (int)r, (int)i + 1, (LPWSTR)v.c_str());
        }
        std::wstring tot = W(fmtDuration(x.total)) + ((int)r == bestTotal ? L"  \u2605" : L"");
        ListView_SetItemText(hCmpRunList, (int)r, (int)acts.size() + 1, (LPWSTR)tot.c_str());
        if (std::find(gCompare.begin(), gCompare.end(), runKey(x.path)) != gCompare.end())
            ListView_SetItemState(hCmpRunList, (int)r, LVIS_SELECTED, LVIS_SELECTED);
    }
    gRunsFilling = false;
    SendMessageW(hCmpRunList, WM_SETREDRAW, TRUE, 0);
    fillComparison();
    InvalidateRect(hCmpRunList, nullptr, TRUE);
}

LRESULT campaignRunsNotify(NMHDR* nh) {
    switch (nh->code) {
        case NM_CUSTOMDRAW: {  // the run being played: green
            auto* cd = (NMLVCUSTOMDRAW*)nh;
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (cd->nmcd.dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
            int row = (int)cd->nmcd.dwItemSpec;
            cd->nmcd.uItemState &= ~(CDIS_SELECTED | CDIS_FOCUS);
            if (row >= (int)gRuns.size()) return CDRF_DODEFAULT;
            bool cur = gRuns[row].current, sel = ListView_GetItemState(hCmpRunList, row, LVIS_SELECTED) != 0;
            cd->clrText = cur ? C::Green : sel ? C::Cyan : C::Fg;
            cd->clrTextBk = sel ? C::Line : row % 2 ? C::Stripe : C::Card;
            return CDRF_DODEFAULT;
        }
        case LVN_ITEMCHANGED: {  // the comparison follows the selection
            auto* c = (NMLISTVIEW*)nh;
            if (gRunsFilling || !(c->uChanged & LVIF_STATE) || !((c->uOldState ^ c->uNewState) & LVIS_SELECTED)) return 0;
            if (c->iItem < 0) {  // all rows at once (a plain click clears the others first)
                gCompare.clear();
                for (size_t r = 0; r < gRuns.size(); ++r)
                    if (ListView_GetItemState(hCmpRunList, (int)r, LVIS_SELECTED)) gCompare.push_back(runKey(gRuns[r].path));
            } else if (c->iItem < (int)gRuns.size()) {
                std::wstring k = runKey(gRuns[c->iItem].path);
                gCompare.erase(std::remove(gCompare.begin(), gCompare.end(), k), gCompare.end());
                if (c->uNewState & LVIS_SELECTED) gCompare.push_back(k);  // selected last: compared after the others
            }
            saveCompared();
            fillComparison();
            InvalidateRect(hCmpRunList, nullptr, FALSE);
            return 0;
        }
        case NM_DBLCLK: {  // load the run (the one being played is saved first, it stays in the list)
            int row = ((NMITEMACTIVATE*)nh)->iItem;
            if (row < 0 || row >= (int)gRuns.size() || gRuns[row].current) return 0;
            restoreSessionFrom(gCmpFile, gRuns[row].path);
            refreshCampaignRuns();
            return 0;
        }
        case NM_RCLICK: {  // on a run: Pin, Rename (the clicked run), Remove (all the selected runs)
            int row = ((NMITEMACTIVATE*)nh)->iItem;
            if (row < 0 || row >= (int)gRuns.size()) return 0;
            hideTip();
            POINT pt;
            GetCursorPos(&pt);
            bool pinned = runKey(gRuns[row].path) == pinnedRun();
            // the runs Remove acts on: the selected ones when the clicked run is among them, else just the clicked one
            std::vector<int> rows;
            if (ListView_GetItemState(hCmpRunList, row, LVIS_SELECTED))
                for (int i = 0; i < (int)gRuns.size(); ++i)
                    if (ListView_GetItemState(hCmpRunList, i, LVIS_SELECTED) && !gRuns[i].current) rows.push_back(i);
            if (rows.empty() && !gRuns[row].current) rows.push_back(row);
            bool many = rows.size() > 1;
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING | (gRuns[row].current ? MF_GRAYED : 0), 3,
                        pinned ? L"Unpin" : L"Pin for overlay comparison");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, 1, L"Rename");
            std::wstring rm = many ? L"Remove " + std::to_wstring(rows.size()) + L" runs" : L"Remove";
            AppendMenuW(m, MF_STRING | (rows.empty() ? MF_GRAYED : 0), 2, rm.c_str());
            int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hMain, nullptr);
            DestroyMenu(m);
            if (cmd == 3) {  // the overlay compares zone times with this run (unpinned: the best of all runs)
                WritePrivateProfileStringW(L"CampaignRuns", L"Pin", pinned ? nullptr : runKey(gRuns[row].path).c_str(),
                                           iniPath().c_str());
                refreshCampaignRuns();
                if (hOverlay) InvalidateRect(hOverlay, nullptr, FALSE);
            } else if (cmd == 1) {
                SetFocus(hCmpRunList);
                gRenaming = true;
                ListView_EditLabel(hCmpRunList, row);
                gRenaming = false;
            } else if (cmd == 2) {
                std::wstring q = many ? L"Remove " + std::to_wstring(rows.size()) + L" runs? Their files go to the Recycle Bin."
                                      : L"Remove the run \"" + gRuns[rows[0]].name + L"\"? Its file goes to the Recycle Bin.";
                if (MessageBoxW(hMain, q.c_str(), L"Campaign", MB_OKCANCEL | MB_ICONWARNING) != IDOK) return 0;
                std::wstring from;  // each path null terminated, the list ends with one more (SHFileOperation)
                for (int i : rows) from += gRuns[i].path + L'\0';
                SHFILEOPSTRUCTW op = {};
                op.hwnd = hMain;
                op.wFunc = FO_DELETE;
                op.pFrom = from.c_str();
                op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
                SHFileOperationW(&op);
                for (int i : rows) {  // its name; the comparison and the pin let go of it in the refresh
                    if (fileExists(gRuns[i].path)) continue;  // not removed (in use, or the user cancelled)
                    WritePrivateProfileStringW(L"CampaignRuns", runKey(gRuns[i].path).c_str(), nullptr, iniPath().c_str());
                }
                refreshCampaignRuns();
                if (hOverlay) InvalidateRect(hOverlay, nullptr, FALSE);  // its comparison may have changed
            }
            return 0;
        }
        case LVN_BEGINLABELEDITW: {
            if (!gRenaming) return TRUE;  // TRUE cancels: only Rename starts an edit
            int row = ((NMLVDISPINFOW*)nh)->item.iItem;
            if (HWND e = ListView_GetEditControl(hCmpRunList); e && row >= 0 && row < (int)gRuns.size())
                SetWindowTextW(e, gRuns[row].name.c_str());  // without the pin
            return FALSE;
        }
        case LVN_ENDLABELEDITW: {  // a new name (empty: back to the date)
            auto* di = (NMLVDISPINFOW*)nh;
            int row = di->item.iItem;
            if (!di->item.pszText || row < 0 || row >= (int)gRuns.size()) return FALSE;
            std::wstring n = di->item.pszText;
            while (!n.empty() && n.back() == L' ') n.pop_back();
            WritePrivateProfileStringW(L"CampaignRuns", runKey(gRuns[row].path).c_str(), n.empty() ? nullptr : n.c_str(),
                                       iniPath().c_str());
            PostMessageW(hMain, WM_COMMAND, MAKEWPARAM(ID_TAB_CRUNS, 0), 0);  // refresh after the edit closes
            return TRUE;
        }
    }
    return 0;
}

// The comparison list: differences green when faster, red when slower; the base row in the accent colour.
LRESULT campaignCompareNotify(NMHDR* nh) {
    if (nh->code != NM_CUSTOMDRAW) return 0;
    auto* cd = (NMLVCUSTOMDRAW*)nh;
    if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    int row = (int)cd->nmcd.dwItemSpec;
    if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
        cd->nmcd.uItemState &= ~(CDIS_SELECTED | CDIS_FOCUS);
        return CDRF_NOTIFYSUBITEMDRAW;
    }
    if (cd->nmcd.dwDrawStage != (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) return CDRF_DODEFAULT;
    wchar_t t[32] = L"";
    ListView_GetItemText(hCmpCmpList, row, cd->iSubItem, t, 31);
    cd->clrTextBk = row == 0 ? C::Line : row % 2 ? C::Stripe : C::Card;
    cd->clrText = row == 0 ? C::Cyan : t[0] == L'-' && t[1] ? C::Green : t[0] == L'+' ? C::Red : C::Fg;
    return CDRF_DODEFAULT;
}

// ---------------------------------------------------------------- best zone times (overlay comparison)

namespace {
std::map<std::string, uint64_t> gBestZone;  // area id -> best time in it of the saved runs (not the current one)
std::map<std::string, uint64_t> gBestAct;   // act name -> the same for whole acts
bool gBestZoneValid = false;
}  // namespace

void invalidateBestZones() { gBestZoneValid = false; }

// The time to beat in a zone: the pinned run's time there, or (nothing pinned) the best time over the saved runs,
// where a zone counts once it was left (another zone came after it in that run, or the run was stopped). Towns are
// not compared. -1 when there is none.
int64_t bestZoneMs(const std::string& areaId) {
    if (!gBestZoneValid) {
        gBestZoneValid = true;
        gBestZone.clear();
        gBestAct.clear();
        std::wstring pin = pinnedRun();
        for (auto& path : campaignRunFiles()) {
            if (path == gCmpFile.path) continue;
            std::wstring key = path.substr(path.find_last_of(L"\\/") + 1);
            if (!pin.empty() && key != pin && fileExists(gDataDir + L"\\sessions\\" + pin)) continue;  // only it
            Tracker t;
            t.deserialize(readFileUtf8(path), Tracker::kCampaign);
            std::map<std::string, uint64_t> mine, acts;
            std::string lastArea, lastAct;
            for (const Zone& z : t.zones) {
                std::string a = Tracker::actOf(z.areaId);
                acts[a] += z.activeMs;  // towns count towards their act
                lastAct = a;
                if (z.town) continue;
                mine[z.areaId] += z.activeMs;
                lastArea = z.areaId;
            }
            bool stopped = t.cmpClock == Tracker::Clock::Stopped;
            for (auto& [a, ms] : acts) {
                if (!ms || (pin.empty() && !stopped && a == lastAct)) continue;  // the act the run was left in
                auto f = gBestAct.find(a);
                if (f == gBestAct.end() || ms < f->second) gBestAct[a] = ms;
            }
            for (auto& [a, ms] : mine) {
                if (!ms || (pin.empty() && !stopped && a == lastArea)) continue;  // left in that zone: not finished
                auto f = gBestZone.find(a);
                if (f == gBestZone.end() || ms < f->second) gBestZone[a] = ms;
            }
        }
    }
    auto f = gBestZone.find(areaId);
    return f == gBestZone.end() ? -1 : (int64_t)f->second;
}

// "(+5s)" / "(-1m05s)"
std::wstring diffInParens(int64_t d) { return L"(" + diffText(d) + L")"; }

// The current run's time in the zone against the time to beat, "" without one
std::wstring zoneVsBest(const std::string& areaId, uint32_t now) {
    int64_t best = bestZoneMs(areaId);
    if (best < 0) return L"";
    uint64_t mine = 0;
    for (const Zone& z : g.zones)
        if (z.areaId == areaId && !z.town) mine += z.activeAt(now);
    return diffInParens((int64_t)mine - best);
}

// The time to beat for a whole act (pinned run, or the best finished one), -1 if none.
int64_t bestActMs(const std::string& act) {
    bestZoneMs("");  // the bests are up to date
    auto f = gBestAct.find(act);
    return f == gBestAct.end() ? -1 : (int64_t)f->second;
}

// The act played before the current one in this run: its name, final time and "(+3m15s)" against the pinned run or
// the best; false when the current act is the first.
bool previousAct(uint32_t now, std::wstring& name, std::wstring& time, std::wstring& cmp) {
    if (g.curZone < 0) return false;
    std::string cur = Tracker::actOf(g.zones[g.curZone].areaId), prev;
    std::vector<std::string> order;
    for (const Zone& z : g.zones) {
        std::string a = Tracker::actOf(z.areaId);
        if (std::find(order.begin(), order.end(), a) == order.end()) order.push_back(a);
    }
    auto at = std::find(order.begin(), order.end(), cur);
    if (at == order.begin() || at == order.end()) return false;
    prev = *(at - 1);
    uint64_t ms = 0;
    for (const Zone& z : g.zones)
        if (Tracker::actOf(z.areaId) == prev) ms += z.activeAt(now);
    bestZoneMs("");  // the bests are up to date
    auto f = gBestAct.find(prev);
    name = W(prev);
    time = W(fmtDuration(ms));
    cmp = f == gBestAct.end() ? L"" : diffInParens((int64_t)ms - (int64_t)f->second);
    return true;
}

void campaignRunsSaveOrder(HWND lv) {
    int n = Header_GetItemCount(ListView_GetHeader(lv));
    std::vector<int> o(n);
    if (!ListView_GetColumnOrderArray(lv, n, o.data())) return;
    std::wstring v;
    for (int i = 0; i < n; ++i) v += (i ? L"|" : L"") + columnName(lv, o[i]);
    WritePrivateProfileStringW(L"order", L"CampaignRuns", v.c_str(), iniPath().c_str());
    for (HWND l : {hCmpRunList, hCmpCmpList})  // the dragged one too: its rows are redrawn in the new order
        if (l) applyRunsOrder(l);
}

// Hovering an act cell of a saved run: its zones and their times (towns left out, the fastest instance of a zone
// played more than once).
void campaignRunsHover(HWND lv, LPARAM lp) {
    LVHITTESTINFO ht = {};
    ht.pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
    ListView_SubItemHitTest(lv, &ht);
    int act = ht.iSubItem - 1;  // column 0 is the run's name
    bool cmp = lv == hCmpCmpList;  // the comparison list: rows after the base show differences
    int run = !cmp ? ht.iItem : ht.iItem >= 0 && ht.iItem < (int)gCmpRowRun.size() ? gCmpRowRun[ht.iItem] : -1;
    if (run < 0 || run >= (int)gRuns.size() || act < 0 || act >= (int)gRunActs.size()) return hideTip();
    const SavedRun& r = gRuns[run];
    const std::string& actName = gRunActs[act];
    auto z = r.zones.find(actName);
    std::vector<std::pair<std::wstring, std::wstring>> lines;
    if (cmp && ht.iItem > 0) {  // each zone against the base's: a zone only one of them has counts in full
        const SavedRun& b = gRuns[gCmpRowRun[0]];
        auto bz = b.zones.find(actName);
        static const std::vector<std::pair<std::string, uint64_t>> none;
        const auto& mine = z == r.zones.end() ? none : z->second;
        const auto& theirs = bz == b.zones.end() ? none : bz->second;
        auto find = [](auto& v, const std::string& n) {
            return std::find_if(v.begin(), v.end(), [&](auto& e) { return e.first == n; });
        };
        for (auto& [name, ms] : mine) {
            auto f = find(theirs, name);
            lines.push_back({W(name), diffText((int64_t)ms - (f == theirs.end() ? 0 : (int64_t)f->second))});
        }
        for (auto& [name, ms] : theirs)
            if (find(mine, name) == mine.end()) lines.push_back({W(name) + L"  (base only)", diffText(-(int64_t)ms)});
    } else {
        if (z == r.zones.end()) return hideTip();
        for (auto& [name, ms] : z->second) lines.push_back({W(name), W(fmtDuration(ms))});
    }
    TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, lv, 0};
    TrackMouseEvent(&t);
    showListTip((cmp ? L"c" : L"r") + std::to_wstring(ht.iItem) + L"|" + std::to_wstring(act),
                r.name + L"  \u00b7  " + W(actName) + (cmp && ht.iItem > 0 ? L"  vs base" : L""), lines);
}
