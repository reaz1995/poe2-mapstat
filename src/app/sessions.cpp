// MapSession / CampaignSession / SummarySession files: save, load, reset into a new file, restore an older one.
#include "app.h"

#include <algorithm>

static std::wstring sessionsDir();
static std::wstring uniquePath(const std::wstring& base);
static void startNewSessionFile(SessionFile& sf);

SessionFile gMapFile = {L"MapSession", L"MapSession", Tracker::kMaps, L""};

SessionFile gCmpFile = {L"CampaignSession", L"CampaignSession", Tracker::kCampaign, L""};

SessionFile gSumFile = {L"SummarySession", L"SummarySession", Tracker::kSummary, L""};

static std::wstring sessionsDir() { return gDataDir + L"\\sessions"; }

static std::wstring uniquePath(const std::wstring& base) {
    std::wstring p = base + L".tsv";
    for (int i = 2; GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; ++i)
        p = base + L" (" + std::to_wstring(i) + L").tsv";
    return p;
}

// "<prefix> 2026-11-02 09-59-31" (+ optional suffix like " (Client.txt)")
std::wstring newSessionPath(const SessionFile& sf, const std::wstring& date, const std::wstring& suffix) {
    std::wstring d = date;
    if (d.empty()) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t b[64];
        swprintf(b, 64, L"%04d-%02d-%02d %02d-%02d-%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                 st.wSecond);
        d = b;
    }
    return uniquePath(sessionsDir() + L"\\" + sf.prefix + L" " + d + suffix);
}

std::wstring fileName(const std::wstring& p) { return p.substr(p.find_last_of(L"\\/") + 1); }

void setSessionFile(SessionFile& sf, const std::wstring& p) {
    sf.path = p;
    WritePrivateProfileStringW(L"main", sf.key, fileName(p).c_str(), iniPath().c_str());
}

// Start-up: open the last files. Older versions had one combined file (ini "Session=", or data.tsv): split it.
void initSessionFiles() {
    CreateDirectoryW(sessionsDir().c_str(), nullptr);
    {  // Summary got a file of its own later: older setups start with a new one (state comes from MapSession)
        wchar_t buf[MAX_PATH];
        GetPrivateProfileStringW(L"main", gSumFile.key, L"", buf, MAX_PATH, iniPath().c_str());
        std::wstring p = sessionsDir() + L"\\" + buf;
        if (*buf && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) gSumFile.path = p;
        else setSessionFile(gSumFile, newSessionPath(gSumFile));
    }
    bool missing = false;
    for (SessionFile* sf : {&gMapFile, &gCmpFile}) {
        wchar_t buf[MAX_PATH];
        GetPrivateProfileStringW(L"main", sf->key, L"", buf, MAX_PATH, iniPath().c_str());
        std::wstring p = sessionsDir() + L"\\" + buf;
        if (*buf && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) sf->path = p;
        else missing = true;
    }
    if (!missing) return;
    wchar_t buf[MAX_PATH];
    GetPrivateProfileStringW(L"main", L"Session", L"", buf, MAX_PATH, iniPath().c_str());
    std::wstring old = *buf ? sessionsDir() + L"\\" + buf : gDataDir + L"\\data.tsv";
    std::wstring base = *buf ? fileName(old) : L"";
    if (base.size() > 4) base.resize(base.size() - 4);  // drop ".tsv": keeps the old date (and suffix) in the new names
    for (SessionFile* sf : {&gMapFile, &gCmpFile}) {
        if (!sf->path.empty()) continue;
        std::wstring np =
            base.empty() ? newSessionPath(*sf) : uniquePath(sessionsDir() + L"\\" + sf->prefix + L" " + base);
        if (GetFileAttributesW(old.c_str()) != INVALID_FILE_ATTRIBUTES) CopyFileW(old.c_str(), np.c_str(), TRUE);
        setSessionFile(*sf, np);  // the old combined file stays; each new file loads only its own part
    }
    if (!*buf) DeleteFileW((gDataDir + L"\\data.tsv").c_str());
}

void saveFile(const SessionFile& sf) {
    if (sf.path.empty()) return;
    std::string d = g.serialize(sf.parts);
    std::wstring tmp = sf.path + L".tmp";
    FILE* f = _wfopen(tmp.c_str(), L"wb");
    if (!f) return;
    fwrite(d.data(), 1, d.size(), f);
    fclose(f);
    MoveFileExW(tmp.c_str(), sf.path.c_str(), MOVEFILE_REPLACE_EXISTING);  // atomic replace
}

void saveData() {
    saveFile(gMapFile);
    saveFile(gCmpFile);
    saveFile(gSumFile);
}

void loadData() {
    for (SessionFile* sf : {&gMapFile, &gCmpFile, &gSumFile}) {
        std::string d = readFileUtf8(sf->path);
        if (!d.empty()) g.deserialize(d, sf->parts);
    }
}

bool ask(const wchar_t* title, const wchar_t* text) {
    return MessageBoxW(hMain, text, title, MB_YESNO | MB_ICONQUESTION) == IDYES;
}

// Before a reset: the current file keeps the data as it was, the reset state goes to a new file of that kind.
static void startNewSessionFile(SessionFile& sf) {
    saveFile(sf);
    setSessionFile(sf, newSessionPath(sf));
}

void resetSummaryData() {
    if (!ask(L"Reset Summary",
             L"Reset the Summary?\n\nSummary starts empty and counts maps from now on. The map that is open now is not "
             L"counted. The Maps tab and Campaign are not affected. Export to CSV first if you want to keep the "
             L"data."))
        return;
    startNewSessionFile(gSumFile);
    g.resetSummary();
    saveData();
    refreshSummary();
}

void resetGeneralSession() {
    if (!ask(L"Reset Session",
             L"Reset the session on the Maps tab?\nThe current map is closed and the list is cleared.\n"
             L"Summary is not affected. Export first if you want to keep the list."))
        return;
    startNewSessionFile(gMapFile);
    g.resetGeneral();
    saveData();
    gHistDirty = true;
    refreshUi();
}

// Pick an older MapSession / CampaignSession file and continue with it (the current one is saved first).
// Only that part is replaced; a file of another kind (or an old combined file) is copied, never overwritten.
void restoreSession(SessionFile& sf) {
    bool maps = sf.parts == Tracker::kMaps;
    const wchar_t* title = maps ? L"Restore map session" : L"Restore campaign session";
    // Windows may prefer the last folder used by any open dialog of this app (e.g. the game's logs folder) over
    // lpstrInitialDir, so the current session file is preselected: its folder is then always used.
    std::wstring dir = sessionsDir();
    wchar_t file[MAX_PATH] = L"";
    if (GetFileAttributesW(sf.path.c_str()) != INVALID_FILE_ATTRIBUTES) lstrcpynW(file, sf.path.c_str(), MAX_PATH);
    std::wstring filter = std::wstring(sf.prefix) + L" files" + L'\0' + sf.prefix + L"*.tsv" + L'\0' +
                          L"All sessions (*.tsv)" + L'\0' + L"*.tsv" + L'\0' + L'\0';
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = hMain;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = dir.c_str();
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    restoreSessionFrom(sf, file);
}

// Continue with this MapSession / CampaignSession file (the current one is saved first).
void restoreSessionFrom(SessionFile& sf, const std::wstring& file) {
    bool maps = sf.parts == Tracker::kMaps;
    const wchar_t* title = maps ? L"Restore map session" : L"Load campaign run";
    std::string d = readFileUtf8(file);
    if (d.rfind("POE2MAPSTAT", 0) != 0) {
        MessageBoxW(hMain, L"This is not a Poe2 | MapStat session file.", title, MB_ICONERROR);
        return;
    }
    saveFile(sf);  // the current one stays in its own file
    Tracker t;
    t.deserialize(d, sf.parts);
    if (maps) g.replaceMaps(t);
    else g.replaceCampaign(t);
    std::wstring name = fileName(file);
    if (name.rfind(sf.prefix, 0) == 0) {
        setSessionFile(sf, file);
    } else {
        std::wstring base = name.size() > 4 ? name.substr(0, name.size() - 4) : name;
        setSessionFile(sf, uniquePath(sessionsDir() + L"\\" + sf.prefix + L" " + base));
    }
    saveData();
    gHistDirty = true;
    refreshUi();
    if (maps) refreshSummary();
    else invalidateBestZones(), refreshCampaign(true);
}

void resetCampaignRun() {
    if (!ask(L"Save and Clear",
             L"Save the campaign run and start a new one?\nThe current run is kept in Campaign \u2192 Summary; all zone "
             L"timers start from zero."))
        return;
    startCampaignRun();
}

// The campaign run files, newest first (their names start with the date they were made).
std::vector<std::wstring> campaignRunFiles() {
    std::vector<std::wstring> r;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((sessionsDir() + L"\\" + gCmpFile.prefix + L" *.tsv").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do r.push_back(sessionsDir() + L"\\" + fd.cFileName);
        while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    std::sort(r.begin(), r.end(), [](auto& a, auto& b) { return a > b; });
    return r;
}

// Copy a campaign run file (from another PC or a backup) into the sessions folder, so it shows in Campaign → Summary.
// A file named like the app's own keeps its name; any other gets one from its date and keeps its old name as the
// run's name.
void importCampaignRun() {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = hMain;
    ofn.lpstrFilter = L"Campaign run (*.tsv)\0*.tsv\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    std::string data = readFileUtf8(file);
    Tracker t;
    t.deserialize(data, Tracker::kCampaign);
    if (t.zones.empty()) {
        MessageBoxW(hMain, L"This file has no campaign zones in it.", L"Import campaign run", MB_ICONWARNING);
        return;
    }
    std::wstring src = file, base = src.substr(src.find_last_of(L"\\/") + 1), stem = base, name;
    if (size_t d = stem.rfind(L'.'); d != std::wstring::npos) stem.resize(d);
    std::wstring prefix = std::wstring(gCmpFile.prefix) + L" ";
    std::wstring dest;
    if (base.rfind(prefix, 0) == 0) dest = base;
    else {  // named after the file's date; the old name becomes the run's name
        WIN32_FILE_ATTRIBUTE_DATA a;
        SYSTEMTIME st = {};
        FILETIME lt;
        if (GetFileAttributesExW(file, GetFileExInfoStandard, &a) && FileTimeToLocalFileTime(&a.ftLastWriteTime, &lt))
            FileTimeToSystemTime(&lt, &st);
        else GetLocalTime(&st);
        wchar_t b[64];
        swprintf(b, 64, L"%04d-%02d-%02d %02d-%02d-%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        dest = prefix + b + L".tsv";
        name = stem;
    }
    std::wstring dir = sessionsDir();
    CreateDirectoryW(dir.c_str(), nullptr);
    if (fileExists(dir + L"\\" + dest)) {
        if (readFileUtf8(dir + L"\\" + dest) == data) {
            MessageBoxW(hMain, L"This run is already in the list.", L"Import campaign run", MB_ICONINFORMATION);
            return;
        }
        std::wstring stemD = dest.substr(0, dest.size() - 4);
        for (int i = 2; fileExists(dir + L"\\" + dest); ++i) dest = stemD + L" (" + std::to_wstring(i) + L").tsv";
    }
    if (!CopyFileW(file, (dir + L"\\" + dest).c_str(), TRUE)) {
        MessageBoxW(hMain, L"Cannot copy the file.", L"Import campaign run", MB_ICONERROR);
        return;
    }
    if (!name.empty()) WritePrivateProfileStringW(L"CampaignRuns", dest.c_str(), name.c_str(), iniPath().c_str());
    refreshCampaignRuns();
}

void startCampaignRun() {
    invalidateBestZones();  // the run that ends here becomes one of the saved ones
    startNewSessionFile(gCmpFile);  // the finished run stays in its own file (Campaign -> Summary)
    g.resetCampaign(nowTick(), nowClock());
    saveData();
    refreshCampaign(true);
    ovSyncFooter();
}
