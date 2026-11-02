// General tab: current map, details, NPC dialogue and the maps of the session.
#include "app.h"

static int mapCategory(const std::string& a);
static const wchar_t* abyssFaction(const std::string& id);
static std::wstring areaNameRaw(const std::string& name, const std::string& id);

static int mapCategory(const std::string& a) {
    auto starts = [&](const char* p) { return a.compare(0, strlen(p), p) == 0; };
    if (starts("Expedition")) return CAT_EXPEDITION;  // logbooks + expedition boss arenas
    if (starts("Map") && !starts("MapUberBoss_") && !starts("MapUnique")) return CAT_MAPS;
    return CAT_SPECIAL;  // Vaal Ruins, Abyss, pinnacle arenas, unique maps, Breach, future zones
}

// True while the game has not logged the area's real name yet (a moment after "Generating ...").
// During that moment the UI keeps showing the previous area and switches once the name is ready.
bool unnamed(const std::string& name, const std::string& id) { return name.empty() || name == id; }

// The game names all three Abyssal Depths variants the same; the id tells the faction (monsters per poe2db).
static const wchar_t* abyssFaction(const std::string& id) {
    if (id == "Abyss_Depths1") return L"Amanamu";  // Lightless
    if (id == "Abyss_Depths2") return L"Kurgal";   // Blackblooded
    if (id == "Abyss_Depths3") return L"Ulaman";   // ... of the Pit
    return nullptr;
}

// Display name of an area: the name from the log, otherwise a readable form of its ID; Abyssal Depths get their faction.
std::wstring areaName(const std::string& name, const std::string& id) {
    std::wstring n = areaNameRaw(name, id);
    if (const wchar_t* f = abyssFaction(id)) n += std::wstring(L" - ") + f;
    return n;
}

static std::wstring areaNameRaw(const std::string& name, const std::string& id) {
    if (!unnamed(name, id)) return W(name);
    std::string t = id.compare(0, 3, "Map") == 0 && id.size() > 3 ? id.substr(3) : id, out;
    for (size_t i = 0; i < t.size(); ++i) {  // "SinterRift" -> "Sinter Rift"
        char c = t[i];
        if (c == '_') { out += ' '; continue; }
        if (i > 0 && isupper((unsigned char)c) && islower((unsigned char)t[i - 1])) out += ' ';
        out += c;
    }
    return W(out);
}

void refreshUi() {
    uint32_t now = nowTick();

    if (g.hasCurrent && unnamed(g.cur.name, g.cur.areaId)) {
        // new area, name not known yet: keep showing what was there
    } else if (g.hasCurrent) {
        const Run& r = g.cur;
        setTextIfChanged(hTitle, areaName(r.name, r.areaId) + L"  \u00b7  lvl " + std::to_wstring(r.level));
        std::wstring st = W(stateName(r.state));
        if (r.state == RunState::Paused) {
            if (r.inHideout) st += L"  -  back in hideout";
            else st += L"  -  in " + W(g.locationName.empty() ? g.location : g.locationName);
        }
        setTextIfChanged(hStatus, st);
        setTextIfChanged(hTimer, W(fmtDuration(r.activeAt(now))));
        std::string subs;
        for (auto& s : r.subs)
            subs += (subs.empty() ? "" : ", ") + U8(areaName(g.nameOf(s.areaId), s.areaId)) + " " + fmtDuration(s.ms);
        char load[32];
        snprintf(load, sizeof load, "%.2f s", r.loadSec);
        std::wstring d = W(r.areaId) + L"\r\n" + std::to_wstring(r.seed) + L"\r\n" + W(r.genTime) +
                         L"     entered " + W(r.enterTime.empty() ? "-" : r.enterTime) + L"     load " + W(load) +
                         L"     entries " + std::to_wstring(r.entries) + L"\r\n" +
                         (r.hideoutId.empty() ? L"-"
                                              : W((r.hideoutName.empty() ? r.hideoutId : r.hideoutName) + "     returns " +
                                                  std::to_string(r.hideoutReturns) + "     time " + fmtDuration(r.hideoutAt(now)))) +
                         L"\r\n" + (subs.empty() ? L"-" : W(subs));
        setTextIfChanged(hDetails, d);
    } else {
        setTextIfChanged(hTitle, L"No active map");
        setTextIfChanged(hStatus, gFileOk ? W("In " + (g.locationName.empty() ? (g.location.empty() ? std::string("-") : g.location)
                                                                               : g.locationName))
                                          : L"Log file not found - set the path in \u2699 options");
        setTextIfChanged(hTimer, L"00:00");
        setTextIfChanged(hDetails, L"-\r\n-\r\n-\r\n-\r\n-");
    }
    InvalidateRect(hStatus, nullptr, TRUE);
    InvalidateRect(hTimer, nullptr, FALSE);

    // NPC dialogue
    int runId = g.hasCurrent ? g.cur.id : -1;
    if (runId != gNpcRunId || g.npcLines.size() != gNpcShown) {
        if (runId != gNpcRunId || g.npcLines.size() < gNpcShown || gNpcShown == (size_t)-1) {
            SendMessageW(hNpc, LB_RESETCONTENT, 0, 0);
            gNpcShown = 0;
        }
        for (size_t i = gNpcShown; i < g.npcLines.size(); ++i)
            SendMessageW(hNpc, LB_ADDSTRING, 0, (LPARAM)W(g.npcLines[i]).c_str());
        gNpcShown = g.npcLines.size();
        gNpcRunId = runId;
        SendMessageW(hNpc, LB_SETTOPINDEX, gNpcShown ? gNpcShown - 1 : 0, 0);
    }

    // Maps list (#1 at top). The current map gets its row as soon as it is generated and ticks live.
    {
        if (gHistDirty) {
            gHistDirty = false;
            if (gPage == PG_SUMMARY) refreshSummary();
        }
        std::vector<LvRow> rows;
        int no = 0, cnt = 0, ent = 0;
        uint64_t ms = 0;
        double load = 0;
        auto put = [&](const Run& r, uint64_t t) {
            if (&r != &g.cur && r.entries == 0) return;  // generated but never entered
            ++no;  // # stays the position in the session, also when filtered
            if (gMapFilter != CAT_ALL && mapCategory(r.areaId) != gMapFilter) return;
            if (!searchMatch(hHist, areaName(r.name, r.areaId), W(r.areaId))) return;
            ++cnt;
            rows.push_back({{std::to_wstring(no), areaName(r.name, r.areaId), std::to_wstring(r.level), W(r.areaId), std::to_wstring(r.seed),
                             r.enterTime.empty() ? L"-" : W(r.enterTime), secs(r.loadSec), W(fmtDuration(t)),
                             std::to_wstring(r.entries)},
                            r.id});
            ent += r.entries;
            ms += t;
            load += r.loadSec;
        };
        for (size_t i = g.generalStart; i < g.history.size(); ++i) put(g.history[i], g.history[i].activeMs);
        if (g.hasCurrent && !unnamed(g.cur.name, g.cur.areaId)) put(g.cur, g.cur.activeAt(now));
        sortRows(hHist, rows);
        size_t before = ListView_GetItemCount(hHist);
        writeRows(hHist, rows);
        gHistCurRow = -1;
        for (size_t i = 0; i < rows.size(); ++i)
            if (g.hasCurrent && rows[i].tag == g.cur.id) gHistCurRow = (int)i;
        if (rows.size() != before && gSort[hHist].col < 0 && !rows.empty())
            ListView_EnsureVisible(hHist, (int)rows.size() - 1, FALSE);
        std::wstring sum = (gMapFilter != CAT_ALL ? std::wstring(kCatName[gMapFilter]) + L" only  \u00b7  " : L"") +
                           std::to_wstring(cnt) + L" maps  \u00b7  total " + W(fmtDuration(ms));
        if (cnt) {
            char avgEnt[16];
            snprintf(avgEnt, sizeof avgEnt, "%.1f", (double)ent / cnt);
            sum += L"  ·  average " + W(fmtDuration(ms / cnt)) + L"  ·  load " + secs(load) + L" (avg " +
                   secs(load / cnt) + L")  ·  entries " + std::to_wstring(ent) + L" (avg " + W(avgEnt) + L")";
        }
        setTextIfChanged(hSummary, sum);
        if (g.hasCurrent) InvalidateRect(hHist, nullptr, FALSE);  // live row colour (active / paused)
    }
    if (g.areaNamesChanged) { g.areaNamesChanged = false; g.saveAreaNames(areasPath()); }
    if (gPage == PG_CAMPAIGN) refreshCampaign(false);

    setTextIfChanged(hOptInfo, std::wstring(gFileOk ? L"Reading log OK" : L"Log file NOT found") +
                                   (gHistoryLoaded ? L"  \u00b7  history loaded" : L"  \u00b7  tracking new events only") + L"\r\n" +
                                   std::to_wstring(gOffset / 1024) + L" KB, " + std::to_wstring(gLinesRead) + L" lines read\r\n" +
                                   W(g.sessionStart.empty() ? "-" : g.sessionStart) + L"\r\n" +
                                   W(g.locationName.empty() ? g.location : g.locationName) + L"\r\n" +
                                   fileName(gMapFile.path) + L"\r\n" + fileName(gCmpFile.path) + L"\r\n" +
                                   fileName(gSumFile.path));
}
