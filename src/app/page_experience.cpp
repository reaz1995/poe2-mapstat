// Experience tab: character name and level (updated from the log on level up), experience to the next
// level and how the area level changes the experience gained. Optional Safe / Best lines on MapTimerOverlay.
#include "app.h"
#include "experience.h"

std::wstring gExpName;
int gExpLevel = 0;
bool gExpOnOverlay = false;

static int gExpArea = 0;             // area level used for the calculation
static int gExpAreaSeen = -1;        // area level last taken from the game
static bool gExpSetting = false;     // edits are being filled by code, not typed

static void iniSet(const wchar_t* key, const std::wstring& v) {
    WritePrivateProfileStringW(L"experience", key, v.c_str(), iniPath().c_str());
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

// Multiplier as a change in percent with 2 decimals, e.g. x0.7816 -> "-21.84%".
std::wstring expPct(double m) {
    wchar_t b[32];
    swprintf(b, 32, L"%+.2f%%", (m - 1.0) * 100.0);
    return b;
}

static std::wstring pct(double m) { return expPct(m); }

// Experience per monster of the area level after the penalty: 2 decimals below 100, grouped above.
static std::wstring grouped(uint64_t v);

static std::wstring perMonster(int charLevel, int areaLevel) {
    double per = xp::expectedExp(charLevel, areaLevel);
    if (per >= 100) return grouped((uint64_t)(per + 0.5));
    wchar_t b[32];
    swprintf(b, 32, L"%.2f", per);
    return b;
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

// Area level of what the timer overlay shows: campaign zone, current or paused map, else where the player is.
int expAreaLevel() {
    if (g.curZone >= 0) return g.zones[g.curZone].level;
    if (g.hasCurrent) return g.cur.level;
    return g.locationLevel;
}

void loadExperience() {
    wchar_t b[128];
    GetPrivateProfileStringW(L"experience", L"Name", L"", b, 128, iniPath().c_str());
    gExpName = b;
    gExpLevel = std::clamp((int)GetPrivateProfileIntW(L"experience", L"Level", 0, iniPath().c_str()), 0, 100);
    gExpOnOverlay = GetPrivateProfileIntW(L"experience", L"Overlay", 0, iniPath().c_str()) != 0;
    g.onLevelUp = [](const std::string& name, int level) {
        if (gExpName.empty() || _wcsicmp(W(name).c_str(), gExpName.c_str()) != 0) return;
        gExpLevel = level;
        iniSet(L"Level", std::to_wstring(level));
        if (hExpLevel) setEdit(hExpLevel, std::to_wstring(level));
        refreshExperience();
        ovSyncFooter();
    };
}

void refreshExperience() {
    if (!hExpInfo) return;
    int area = expAreaLevel();
    if (area != gExpAreaSeen) {  // the game moved on: follow it (a typed area level stays until then)
        gExpAreaSeen = area;
        gExpArea = area;
        setEdit(hExpArea, area > 0 ? std::to_wstring(area) : L"");
    }
    std::wstring v;
    int cl = gExpLevel, al = gExpArea;
    if (cl < 1) {
        v = L"-\r\n-\r\n-\r\n-\r\n-\r\n-";
    } else {
        int safe = xp::safeRange(cl);
        v = cl < 100 ? grouped(xp::toNextLevel(cl)) + L"   (level " + std::to_wstring(cl) + L" to " + std::to_wstring(cl + 1) + L")"
                    : std::wstring(L"max level");
        v += L"\r\n±" + std::to_wstring(safe) + L" levels   (areas " + std::to_wstring(std::max(1, cl - safe)) + L" to " +
             std::to_wstring(cl + safe) + L" give no penalty)";
        if (al < 1) {
            v += L"\r\n-\r\n-\r\n-";
        } else {
            int base = xp::monsterExp(al), own = xp::monsterExp(cl);
            double pm = xp::penaltyMultiplier(cl, al), em = xp::expectedMultiplier(cl, al);
            wchar_t b[96];
            swprintf(b, 96, L"%ls   (area %d;  %ls at your level %d)", grouped(base).c_str(), al, grouped(own).c_str(), cl);
            v += L"\r\n" + std::wstring(b);
            int ed = std::max(std::abs(cl - al) - safe, 0);
            swprintf(b, 96, L"x%.4f   (%ls)   %d levels outside the safe range", pm, pct(pm).c_str(), ed);
            v += L"\r\n" + std::wstring(b);
            swprintf(b, 96, L"%ls per monster   (%ls vs a monster of your level)", perMonster(cl, al).c_str(), pct(em).c_str());
            v += L"\r\n" + std::wstring(b);
        }
        int best = xp::bestArea(cl);
        wchar_t b[96];
        swprintf(b, 96, L"area %d:   %ls per monster   (%ls vs a monster of your level)", best, perMonster(cl, best).c_str(),
                 pct(xp::expectedMultiplier(cl, best)).c_str());
        v += L"\r\n" + std::wstring(b);
    }
    setTextIfChanged(hExpInfo, v);
}

void expEditChanged(HWND h) {
    if (gExpSetting) return;
    std::wstring t = editText(h);
    if (h == hExpName) {
        gExpName = t;
        iniSet(L"Name", t);
    } else if (h == hExpLevel) {
        gExpLevel = std::clamp(_wtoi(t.c_str()), 0, 100);
        iniSet(L"Level", std::to_wstring(gExpLevel));
    } else if (h == hExpArea) {
        gExpArea = std::clamp(_wtoi(t.c_str()), 0, 100);
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
