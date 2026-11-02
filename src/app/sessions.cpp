// MapSession / CampaignSession / SummarySession files: save, load, reset into a new file, restore an older one.
#include "app.h"

static std::wstring sessionsDir();
static std::wstring uniquePath(const std::wstring& base);
static void startNewSessionFile(SessionFile& sf);

SessionFile gMapFile = {L"MapSession", L"MapSession", Tracker::kMaps, L""};

SessionFile gCmpFile = {L"CampaignSession", L"CampaignSession", Tracker::kCampaign, L""};

SessionFile gSumFile = {L"SummarySession", L"SummarySession", Tracker::kSummary, L""};

static std::wstring sessionsDir() { return gDataDir + L"\\sessions"; }

static std::wstring uniquePath(const std::wstring& base) {
    std::wstring p = base + L".tsv";
    for (int i = 2; GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; ++i) p = base + L" (" + std::to_wstring(i) + L").tsv";
    return p;
}

// "<prefix> 2026-11-02 09-59-31" (+ optional suffix like " (Client.txt)")
std::wstring newSessionPath(const SessionFile& sf, const std::wstring& date, const std::wstring& suffix) {
    std::wstring d = date;
    if (d.empty()) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t b[64];
        swprintf(b, 64, L"%04d-%02d-%02d %02d-%02d-%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
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
        std::wstring np = base.empty() ? newSessionPath(*sf) : uniquePath(sessionsDir() + L"\\" + sf->prefix + L" " + base);
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
             L"counted. The General tab and Campaign are not affected. Export to CSV first if you want to keep the data."))
        return;
    startNewSessionFile(gSumFile);
    g.resetSummary();
    saveData();
    refreshSummary();
}

void resetGeneralSession() {
    if (!ask(L"Reset Session", L"Reset the session on the General tab?\nThe current map is closed and the list and dialogue are cleared.\n"
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
    wchar_t file[MAX_PATH] = L"";
    std::wstring dir = sessionsDir();
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
    else refreshCampaign(true);
}

void resetCampaignRun() {
    if (!ask(L"Reset Campaign Run", L"Reset the campaign run?\nAll zone timers start from zero. Export first if you want to keep the current run."))
        return;
    startCampaignRun();
}

void startCampaignRun() {
    startNewSessionFile(gCmpFile);  // the finished run stays in its own file (Restore campaign session)
    g.resetCampaign(nowTick(), nowClock());
    saveData();
    refreshCampaign(true);
    ovSyncFooter();
}
