// Experience tab: character name and level (updated from the log on level up), experience to the next
// level and how the area level changes the experience gained, the level ups list (Campaign / Maps). The experience
// lines on MapTimerOverlay are turned on in Options.
#include <atomic>
#include <mutex>
#include <thread>

#include "app.h"
#include "experience.h"
#include "levelups.h"

std::wstring gExpName;
int gExpLevel = 0;
int gTabEff = 0, gMapEff = 0, gExpGain = 0;
bool gExpOnOverlay = false;

static int gExpArea = 0;  // area level used for the calculation
static int gExpAreaSeen = -1;  // area level last taken from the game
static bool gExpSetting = false;  // edits are being filled by code, not typed

// ---- level ups of the character named on the tab: kept in levelups\<name>.tsv; earlier ones are read from the
// whole log once per name (on a worker thread), new ones are added as they happen.
static std::vector<LevelUp> gUps;  // oldest first
static std::wstring gUpsName;  // the name gUps belong to
static ULONGLONG gNameTypedAt = 0;  // the log is read a moment after the name stops changing
static std::wstring gScannedName;  // the log was read for this name
static std::mutex gScanMx;
static std::vector<LevelUp> gScanFound;  // found by the worker, for gScanFor
static std::wstring gScanFor;
static std::atomic<int> gScanState{0};  // 0 idle, 1 reading, 2 done (result waiting)
static int gUpsShown = -1;  // rows in the list

static std::wstring upsPath(const std::wstring& name) {
    std::wstring n;
    for (wchar_t c : name) n += wcschr(L"\\/:*?\"<>|", c) ? L'_' : c;
    return gDataDir + L"\\levelups\\" + lowerW(n) + L".tsv";
}

static void saveUps() {
    if (gUpsName.empty()) return;
    CreateDirectoryW((gDataDir + L"\\levelups").c_str(), nullptr);
    std::string t = saveLevelUps(gUps);
    if (FILE* f = _wfopen(upsPath(gUpsName).c_str(), L"wb")) {
        fwrite(t.data(), 1, t.size(), f);
        fclose(f);
    }
}

static void loadUps(const std::wstring& name) {
    gUpsName = name;
    gUps = name.empty() ? std::vector<LevelUp>{} : loadLevelUps(readFileUtf8(upsPath(name)));
    gUpsShown = -1;
}

// Reads the whole log for the named character's level ups (worker thread).
static void scanLog(std::wstring path, std::wstring name) {
    std::vector<LevelUp> found;
    if (FILE* f = _wfopen(path.c_str(), L"rb")) {
        std::vector<char> buf(1 << 20);
        std::string pending;  // a line cut by the end of a block
        size_t have = 0, pos = 0;
        auto next = [&](std::string& line) {
            line.clear();
            for (;;) {
                if (pos == have) {
                    have = fread(buf.data(), 1, buf.size(), f), pos = 0;
                    if (!have) return !line.empty();
                }
                char* nl = (char*)memchr(buf.data() + pos, '\n', have - pos);
                size_t end = nl ? (size_t)(nl - buf.data()) : have;
                line.append(buf.data() + pos, end - pos);
                pos = nl ? end + 1 : have;
                if (nl) return true;
            }
        };
        found = scanLevelUps(next, U8(name));
        fclose(f);
    }
    std::lock_guard<std::mutex> lock(gScanMx);
    gScanFound = std::move(found);
    gScanFor = name;
    gScanState = 2;
}

// The level ups follow the name typed on the tab; a new name is looked up in the log once it stops changing.
static void syncLevelUps() {
    if (_wcsicmp(gUpsName.c_str(), gExpName.c_str()) != 0) loadUps(gExpName);
    if (gScanState == 2) {
        std::lock_guard<std::mutex> lock(gScanMx);
        if (_wcsicmp(gScanFor.c_str(), gUpsName.c_str()) == 0) {
            size_t before = gUps.size();
            mergeLevelUps(gUps, gScanFound);
            if (gUps.size() != before) saveUps(), gUpsShown = -1;
        }
        gScanState = 0;
    }
    if (gScanState == 0 && !gExpName.empty() && !gLogPath.empty() &&
        _wcsicmp(gScannedName.c_str(), gExpName.c_str()) != 0 && GetTickCount64() - gNameTypedAt > 1500) {
        gScannedName = gExpName;
        gScanState = 1;
        // Client.txt (all game sessions) next to LatestClient.txt, else the log being followed
        std::wstring dir = gLogPath.substr(0, gLogPath.find_last_of(L"\\/") + 1), all = dir + L"Client.txt";
        std::thread(scanLog, fileExists(all) ? all : gLogPath, gExpName).detach();
    }
}

// Seconds as "1h 05m", "12m 30s"
static std::wstring span(int64_t s) {
    wchar_t b[48];
    if (s < 0) return L"-";
    if (s >= 86400) swprintf(b, 48, L"%lldd %02lldh", (long long)(s / 86400), (long long)(s % 86400 / 3600));
    else if (s >= 3600) swprintf(b, 48, L"%lldh %02lldm", (long long)(s / 3600), (long long)(s % 3600 / 60));
    else swprintf(b, 48, L"%lldm %02llds", (long long)(s / 60), (long long)(s % 60));
    return b;
}

// Experience as "1.23M", "456.7k"
static std::wstring shortNum(double v) {
    wchar_t b[32];
    if (v >= 1e9) swprintf(b, 32, L"%.2fB", v / 1e9);
    else if (v >= 1e6) swprintf(b, 32, L"%.2fM", v / 1e6);
    else if (v >= 1e3) swprintf(b, 32, L"%.1fk", v / 1e3);
    else swprintf(b, 32, L"%.0f", v);
    return b;
}

// Experience per hour while level ups[i] was gained (the whole level's experience over the time it took), -1 if
// that time is not known or the level was not gained in one zone (a zone change, a town visit, a relog).
static double upExpPerHour(size_t i) {
    int64_t t = levelSeconds(gUps, i);
    if (t <= 0 || !gUps[i].oneZone) return -1;  // only a level gained wholly in one zone
    return (double)xp::toNextLevel(gUps[i].from) * 3600.0 / t;
}

static void fillLevelList();

void expListChanged() {
    gUpsShown = -1;
    fillLevelList();
}

// For the overlay: experience to the next level and exp/h of the last level, "" when not known.
std::wstring expRequiredText() { return gExpLevel >= 1 && gExpLevel < 100 ? shortNum((double)xp::toNextLevel(gExpLevel)) : L""; }
std::wstring expPerHourText() {
    for (size_t i = gUps.size(); i-- > 0;) {
        double v = upExpPerHour(i);
        if (v > 0) return shortNum(v);
    }
    return L"";
}

// Exp/h of the whole campaign run: the experience of the level ups in campaign zones (of the same difficulty as the
// zone the player is in) over the campaign time; "" when not known yet.
std::wstring expCampaignPerHourText(uint64_t campaignMs) {
    if (campaignMs < 60000) return L"";
    bool cruel = g.location.rfind("C_", 0) == 0;
    double exp = 0;
    for (const LevelUp& u : gUps)
        if (Tracker::isCampaign(u.areaId) && (u.areaId.rfind("C_", 0) == 0) == cruel)
            exp += (double)xp::toNextLevel(u.from);
    return exp > 0 ? shortNum(exp * 3600000.0 / (double)campaignMs) : L"";
}

static int64_t nowSeconds() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char d[16], h[16];
    snprintf(d, sizeof d, "%04d/%02d/%02d", t.wYear, t.wMonth, t.wDay);
    snprintf(h, sizeof h, "%02d:%02d:%02d", t.wHour, t.wMinute, t.wSecond);
    return logSeconds(d, h);
}

bool gLvlMaps = false;

// Level ups list: campaign zones or maps (everything outside the campaign), newest first, numbered in order.
static void fillLevelList() {
    if (!hExpList || gUpsShown == (int)gUps.size()) return;
    gUpsShown = (int)gUps.size();
    std::vector<size_t> shown;
    for (size_t i = 0; i < gUps.size(); ++i)
        if (Tracker::isCampaign(gUps[i].areaId) != gLvlMaps) shown.push_back(i);
    std::vector<LvRow> rows;
    for (size_t n = shown.size(); n-- > 0;) {
        size_t i = shown[n];
        const LevelUp& u = gUps[i];
        double eph = upExpPerHour(i);
        std::string zone = u.areaName.empty() ? g.nameOf(u.areaId) : u.areaName;
        rows.push_back({{std::to_wstring(n + 1), areaName(zone, u.areaId), u.areaLevel ? std::to_wstring(u.areaLevel) : L"-",
                         std::to_wstring(u.from), std::to_wstring(u.to), span(levelSeconds(gUps, i)),
                         eph > 0 ? shortNum(eph) : L"-", W(u.date + " " + u.time)},
                        (int)i});
    }
    sortRows(hExpList, rows);
    writeRows(hExpList, rows);
}

static void iniSet(const wchar_t* key, const std::wstring& v) {
    WritePrivateProfileStringW(L"experience", key, v.c_str(), iniPath().c_str());
}

void setLevelMode(bool maps) {
    gLvlMaps = maps;
    iniSet(L"LevelUpsMode", maps ? L"Maps" : L"Campaign");
    gUpsShown = -1;
    fillLevelList();
    InvalidateRect(hLvlCmp, nullptr, FALSE);
    InvalidateRect(hLvlMaps, nullptr, FALSE);
}

static std::wstring editText(HWND h) {
    wchar_t b[128] = L"";
    GetWindowTextW(h, b, 128);
    return b;
}

static void setEdit(HWND h, const std::wstring& t) {
    if (editText(h) == t) return;
    gExpSetting = true;
    SetWindowTextW(h, t.c_str());
    gExpSetting = false;
}

// Multiplier as a change in percent with 2 decimals, e.g. x0.7816 -> "-21.84%". A tiny amount that is left never
// rounds to -100.00%: it shows as -99.99%, so -100.00% means really nothing.
std::wstring expPct(double m) {
    double p = (m - 1.0) * 100.0;
    if (m > 0 && p < -99.99) p = -99.99;
    wchar_t b[32];
    swprintf(b, 32, L"%+.2f%%", p);
    return b;
}

static std::wstring pct(double m) { return expPct(m); }

static std::wstring grouped(uint64_t v);

// Experience amount as text: 2 decimals below 100, grouped above.
static std::wstring num(double v) {
    if (v >= 100) return grouped((uint64_t)(v + 0.5));
    wchar_t b[32];
    swprintf(b, 32, L"%.2f", v);
    return b;
}

// Experience per monster of the area level after the penalty and the tablet / map bonuses.
static std::wstring perMonster(int charLevel, int areaLevel) {
    return num(xp::expectedExp(charLevel, areaLevel, expEffMult() * expGainMult()));
}

static std::wstring grouped(uint64_t v) {
    std::wstring s = std::to_wstring(v);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, L",");
    return s;
}

// True where no monsters are (hideout, campaign town): the overlay leaves the experience line out.
bool expNoMonsters() {
    if (g.curZone >= 0) return g.zones[g.curZone].town;
    return g.inHideoutNow;
}

// Area level used on the Experience tab and the overlay: follows the game, or what was typed until the next area.
int expArea() { return gExpArea; }

// Both multiply the experience, so the order does not change the result: effectiveness scales the monster's
// base experience, increased experience gain applies to what is left after the level penalty.
double expEffMult() { return xp::effectivenessMultiplier(gTabEff + gMapEff); }
double expGainMult() { return 1.0 + gExpGain / 100.0; }

// Area level of what the timer overlay shows: campaign zone, sub-area, current or paused map, else where the player is.
int expAreaLevel() {
    if (g.curZone >= 0) return g.zones[g.curZone].level;
    if (g.inSubArea()) return g.locationLevel;  // Abyssal Depths etc. have their own level
    if (g.hasCurrent) return g.cur.level;
    return g.locationLevel;
}

void loadExperience() {
    wchar_t b[128];
    GetPrivateProfileStringW(L"experience", L"Name", L"", b, 128, iniPath().c_str());
    gExpName = b;
    gExpLevel = std::clamp((int)GetPrivateProfileIntW(L"experience", L"Level", 0, iniPath().c_str()), 0, 100);
    gExpOnOverlay = GetPrivateProfileIntW(L"experience", L"Overlay", 0, iniPath().c_str()) != 0;
    {
        wchar_t m[16] = L"";
        GetPrivateProfileStringW(L"experience", L"LevelUpsMode", L"Campaign", m, 16, iniPath().c_str());
        gLvlMaps = std::wstring(m) == L"Maps";
    }
    auto pctIni = [](const wchar_t* key, int def) {
        return std::max(0, (int)GetPrivateProfileIntW(L"experience", key, def, iniPath().c_str()));
    };
    gTabEff = pctIni(L"TabletEffectiveness", 0);
    gMapEff = pctIni(L"MapEffectiveness", 0);
    gExpGain = pctIni(L"TabletExpGain", 0);
    g.onLevelUp = [](const std::string& name, int level, const LogLine& l) {
        if (gExpName.empty() || _wcsicmp(W(name).c_str(), gExpName.c_str()) != 0) return;
        if (_wcsicmp(gUpsName.c_str(), gExpName.c_str()) != 0) loadUps(gExpName);
        static int lastUpAt = -1;  // g.areasEntered at the last level up (this session)
        bool oneZone = lastUpAt == g.areasEntered;
        lastUpAt = g.areasEntered;
        if (level >= 2) {
            mergeLevelUps(gUps, {{l.date, l.time, g.location, g.locationDisplay(),
                                  g.locationLevel, level - 1, level, oneZone}});
            saveUps();
            gUpsShown = -1;
        }
        gExpLevel = level;
        iniSet(L"Level", std::to_wstring(level));
        if (hExpLevel) setEdit(hExpLevel, std::to_wstring(level));
        refreshExperience();
        ovSyncFooter();
    };
}

void refreshExperience() {
    if (!hExpInfo) return;
    syncLevelUps();
    fillLevelList();
    int area = expAreaLevel();
    if (area != gExpAreaSeen) {  // the game moved on: follow it (a typed area level stays until then)
        gExpAreaSeen = area;
        gExpArea = area;
        setEdit(hExpArea, area > 0 ? std::to_wstring(area) : L"");
    }
    std::wstring v;
    int cl = gExpLevel, al = gExpArea;
    if (cl < 1) {
        v = L"-\r\n-\r\n-\r\n-\r\n-\r\n-\r\n-\r\n-";
    } else {
        int safe = xp::safeRange(cl);
        v = cl < 100 ? grouped(xp::toNextLevel(cl)) + L"   (level " + std::to_wstring(cl) + L" to " +
                           std::to_wstring(cl + 1) + L")"
                     : std::wstring(L"max level");
        v += L"\r\n±" + std::to_wstring(safe) + L" levels   (areas " + std::to_wstring(std::max(1, cl - safe)) +
             L" to " + std::to_wstring(cl + safe) + L" give no penalty)";
        // Order: monster base exp -> effectiveness -> level penalty -> increased exp gain -> expected exp.
        const double eff = expEffMult(), gain = expGainMult(), bonus = eff * gain;
        wchar_t b[160];
        if (al < 1) {
            v += L"\r\n-\r\n-\r\n-\r\n-\r\n-";
        } else {
            int base = xp::monsterExp(al), own = xp::monsterExp(cl);
            double pm = xp::penaltyMultiplier(cl, al);
            swprintf(b, 160, L"%ls   (area %d;  %ls at your level %d)", grouped(base).c_str(), al, grouped(own).c_str(),
                     cl);
            v += L"\r\n" + std::wstring(b);
            swprintf(b, 160, L"%d%%   |   base exp %ls -> %ls", gTabEff + gMapEff, grouped(base).c_str(),
                     grouped((uint64_t)(base * eff + 0.5)).c_str());
            v += L"\r\n" + std::wstring(b);
            int ed = std::max(std::abs(cl - al) - safe, 0);
            swprintf(b, 160, L"x%.4f   (%ls)   %d levels outside the safe range", pm, pct(pm).c_str(), ed);
            v += L"\r\n" + std::wstring(b);
            double afterPenalty = base * eff * pm;
            swprintf(b, 160, L"%d%%   |   exp after penalties %ls -> %ls", gExpGain, num(afterPenalty).c_str(),
                     num(afterPenalty * gain).c_str());
            v += L"\r\n" + std::wstring(b);
            swprintf(b, 160, L"%ls per monster   (%ls vs a monster of your level)", perMonster(cl, al).c_str(),
                     pct(xp::expectedMultiplier(cl, al, bonus)).c_str());
            v += L"\r\n" + std::wstring(b);
        }
        int best = xp::bestArea(cl, bonus);
        swprintf(b, 160, L"area %ls:   %ls per monster   (%ls vs a monster of your level)",
                 W(xp::bestAreaText(cl, bonus)).c_str(), perMonster(cl, best).c_str(),
                 pct(xp::expectedMultiplier(cl, best, bonus)).c_str());
        v += L"\r\n" + std::wstring(b);
    }
    // level ups (the log's ": Name (Class) is now level N" lines)
    if (gUps.empty()) {
        v += gExpName.empty()                       ? L"\r\n-   (enter the character name)\r\n-\r\n-"
             : gScanState == 1                      ? L"\r\nreading the log...\r\n-\r\n-"
                                                    : L"\r\nnone found in the log yet\r\n-\r\n-";
    } else {
        const LevelUp& u = gUps.back();
        std::string zone = u.areaName.empty() ? g.nameOf(u.areaId) : u.areaName;
        int64_t ago = nowSeconds() - logSeconds(u.date, u.time);
        v += L"\r\n" + W(u.date + " " + u.time.substr(0, 5)) + L"   (level " + std::to_wstring(u.from) + L" to " +
             std::to_wstring(u.to) + L" in " + areaName(zone, u.areaId) + L";  " + span(ago) + L" ago)";
        int64_t t = levelSeconds(gUps, gUps.size() - 1);
        v += L"\r\n" + span(t) + (t > 0 ? L"   (level " + std::to_wstring(u.from) + L" to " + std::to_wstring(u.to) + L")"
                                         : std::wstring(L"   (the level before is not in the log)"));
        double eph = upExpPerHour(gUps.size() - 1);
        v += L"\r\n" + (eph > 0 ? shortNum(eph) + L" per hour   (the experience of level " + std::to_wstring(u.from) +
                                        L" over the time it took)"
                                  : std::wstring(L"-"));
    }
    setTextIfChanged(hExpInfo, v);
}

void expEditChanged(HWND h) {
    if (gExpSetting) return;
    std::wstring t = editText(h);
    if (h == hExpName) {
        gExpName = t;
        gNameTypedAt = GetTickCount64();
        iniSet(L"Name", t);
    } else if (h == hExpLevel) {
        gExpLevel = std::clamp(_wtoi(t.c_str()), 0, 100);
        iniSet(L"Level", std::to_wstring(gExpLevel));
    } else if (h == hExpArea) {
        gExpArea = std::clamp(_wtoi(t.c_str()), 0, 100);
    } else if (h == hExpTabEff || h == hExpMapEff || h == hExpGain) {
        int v = std::clamp(_wtoi(t.c_str()), 0, 9999);
        (h == hExpTabEff ? gTabEff : h == hExpMapEff ? gMapEff : gExpGain) = v;
        iniSet(h == hExpTabEff   ? L"TabletEffectiveness"
               : h == hExpMapEff ? L"MapEffectiveness"
                                 : L"TabletExpGain",
               std::to_wstring(v));
    }
    refreshExperience();
    ovSyncFooter();
    if (hOverlay) InvalidateRect(hOverlay, nullptr, FALSE);
}

void toggleExpOverlay() {
    gExpOnOverlay = !gExpOnOverlay;
    iniSet(L"Overlay", gExpOnOverlay ? L"1" : L"0");
    InvalidateRect(hExpOvChk, nullptr, FALSE);
    ovSyncFooter();
}
