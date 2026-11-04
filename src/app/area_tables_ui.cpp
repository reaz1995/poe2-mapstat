// Zone / act name tables (src/core/area_tables): the ones baked in from tables/*.txt, merged with the editable copy
// in %APPDATA%\Poe2MapStat\tables. Edits to that copy are picked up while the app runs; new ids the game shows are
// written to it as "unknown".
#include "app.h"
#include "area_tables.h"
#include "tables_gen.h"

namespace {

const char* kHeaderZones =
    "# Zone names by area id (Poe2 | MapStat). \"unknown\" = not known yet: the app shows the name from the game's\n"
    "# log, or the id. Type a name over \"unknown\" and the app uses it at once (layout folders are renamed).\n";
const char* kHeaderActs =
    "# Act names by the start of the area id: G1_2 -> G1, C_G1_2 -> C_G1, P1_3 -> P1. \"unknown\" = shown as the key\n"
    "# (G5). Type a name over \"unknown\" and the app uses it at once (layout folders are renamed).\n";

std::wstring dir() { return gDataDir + L"\\tables"; }
std::wstring pathOf(bool acts) { return dir() + (acts ? L"\\acts.txt" : L"\\zones.txt"); }
uint64_t gStamp[2] = {0, 0};  // last written time of the two files, as read / written by the app

uint64_t stamp(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return 0;
    return ((uint64_t)a.ftLastWriteTime.dwHighDateTime << 32 | a.ftLastWriteTime.dwLowDateTime) ^ a.nFileSizeLow;
}

AreaTable baked(bool acts) {
    AreaTable t;
    for (int i = 0; i < kEmbeddedTableCount; ++i)
        if (std::string(kEmbeddedTables[i].file) == (acts ? "acts" : "zones")) t.parse(kEmbeddedTables[i].text);
    return t;
}

void save(bool acts) {
    CreateDirectoryW(dir().c_str(), nullptr);
    std::string s = (acts ? actTable() : zoneTable()).text(acts ? kHeaderActs : kHeaderZones);
    HANDLE f = CreateFileW(pathOf(acts).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    WriteFile(f, s.data(), (DWORD)s.size(), &w, nullptr);
    CloseHandle(f);
    gStamp[acts] = stamp(pathOf(acts));
}

// the AppData copy, with what this app version knows filled in; written back when that changed anything
void load(bool acts) {
    AreaTable& t = acts ? actTable() : zoneTable();
    AreaTable mine;
    std::string text = readFileUtf8(pathOf(acts));
    bool exists = !text.empty();
    mine.parse(text);
    bool changed = mine.mergeFrom(baked(acts)) || !exists;
    // keep ids the tracker added while the file was being read
    for (auto& [k, v] : t.entries()) changed |= mine.add(k, v);
    t = mine;
    if (changed) save(acts);
    else gStamp[acts] = stamp(pathOf(acts));
}

}  // namespace

void loadAreaTables() {
    load(true);
    load(false);
}

// 0.5 s heartbeat: new ids from the tracker are saved; edits to the files are loaded (every 2 s)
void areaTablesTick() {
    if (g.tablesChanged) {
        g.tablesChanged = false;
        save(true);
        save(false);
        areaTablesChanged();
    }
    static int n = 0;
    if (++n % 4) return;
    bool changed = false;
    for (bool acts : {true, false})
        if (stamp(pathOf(acts)) != gStamp[acts]) {
            load(acts);
            changed = true;
        }
    if (changed) areaTablesChanged();
}

void areaTablesChanged() {
    layoutsFollowTables();  // G5 -> Act 5 folders
    refreshCampaign(true);
    refreshUi();
    stepsChanged();
    graphOverlayRefresh();
    if (hOverlay) InvalidateRect(hOverlay, nullptr, FALSE);
}
