// Reading LatestClient.txt (track only new lines, start at the last area change) and finding the file.
#include "app.h"

static void resetReader();
static std::vector<std::wstring> logCandidates();

static void resetReader() {  // log reading state + UI caches; stored runs/zones are kept
    gOffset = 0;
    gPartial.clear();
    gLinesRead = 0;
    gCmpShownAct = "\x01";
    gLastCurZone = -2;
    gHistDirty = true;
}

// After the start-up replay the current area was entered some time ago. Up to 15 minutes that time counts
// (app opened during the map); a longer gap means a logout or a break, so the current stay starts now.
static void limitReplayedStay() {
    const uint32_t now = GetTickCount(), kMax = 15u * 60u * 1000u;
    auto fresh = [&](uint32_t& start) {
        if ((uint32_t)(now - start) > kMax) start = now;  // also when the tick is from before a reboot
    };
    if (g.hasCurrent && g.cur.inSegment) fresh(g.cur.segStartTick);
    if (g.hasCurrent && g.cur.inHideout) fresh(g.cur.hideoutStartTick);
    if (g.curZone >= 0 && g.zones[g.curZone].inSegment) fresh(g.zones[g.curZone].segStartTick);
    if (g.inHideoutNow) fresh(g.hideoutStayTick);
}

void pollLog() {
    bool replayed = false;
    HANDLE h = CreateFileW(gLogPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        gFileOk = false;
        return;
    }
    gFileOk = true;
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(h, &info)) {
        CloseHandle(h);
        return;
    }
    uint64_t size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    std::string head(256, '\0');
    {
        DWORD got = 0;
        ReadFile(h, &head[0], (DWORD)head.size(), &got, nullptr);
        head.resize(got);
        LARGE_INTEGER z = {};
        SetFilePointerEx(h, z, nullptr, FILE_BEGIN);
    }
    bool firstOpen = gHead.empty();
    bool newSession = !firstOpen && head.compare(0, std::min(head.size(), gHead.size()), gHead, 0,
                                                 std::min(head.size(), gHead.size())) != 0;
    if (!firstOpen && !newSession && size < gOffset) {
        // Same session but the file is shorter (being rewritten by the game). Keep the read position:
        // nothing is re-read, reading continues once the file grows past the old position.
        CloseHandle(h);
        return;
    }
    if (firstOpen || newSession) {
        // First open: track only (unless "Load history"). New session while running: everything in the
        // new file happened after the app started, so it is read from the start.
        if (newSession) gSkipExisting = false;
        if (newSession) g.suspend(g.lastTick);  // game restarted: close what was open in the old session
        resetReader();
        g.resetSession();
        gHead = head;
        if (gSkipExisting && size > 0) {
            // Track only: don't replay what is already in the log. Read just the first line (session id)
            // and the last line (current tick), then continue from the end of the last complete line.
            std::vector<char> head(4096), tail(1 << 20);  // tail: up to 1 MB to find the last area change
            DWORD got = 0;
            ReadFile(h, head.data(), (DWORD)head.size(), &got, nullptr);
            std::string first(head.data(), got);
            {  // session id = first timestamped line (the very first line is usually a banner)
                std::stringstream fl(first);
                for (std::string ln; std::getline(fl, ln);) {
                    LogLine ll;
                    if (parseLine(ln, ll)) {
                        g.sessionStart = ll.date + " " + ll.time;
                        break;
                    }
                }
            }
            uint64_t from = size > tail.size() ? size - tail.size() : 0;
            LARGE_INTEGER li;
            li.QuadPart = (LONGLONG)from;
            SetFilePointerEx(h, li, nullptr, FILE_BEGIN);
            got = 0;
            ReadFile(h, tail.data(), (DWORD)tail.size(), &got, nullptr);
            std::string t(tail.data(), got);
            size_t lastNl = t.rfind('\n');
            if (lastNl != std::string::npos) {
                size_t prev = t.rfind('\n', lastNl ? lastNl - 1 : 0);
                std::string line = t.substr(prev == std::string::npos ? 0 : prev + 1,
                                            lastNl - (prev == std::string::npos ? 0 : prev + 1));
                LogLine ll;
                if (parseLine(line, ll)) {
                    g.lastTick = ll.tick;
                    g.lastTime = ll.time;
                }
                gOffset = from + lastNl + 1;
                // Game running: replay from the last area change, so the current map, hideout or zone is
                // active at once (otherwise nothing shows until the next area change). A stored row of the same
                // map instance is continued, as after a crash.
                size_t gen = findGame() ? t.rfind("] Generating level ") : std::string::npos;
                if (gen != std::string::npos) {
                    size_t ls = t.rfind('\n', gen);
                    if (ls != std::string::npos) {  // a complete line inside the window
                        gOffset = from + ls + 1;
                        g.lastTick = 0;
                        g.lastTime.clear();
                        g.sawLoadingScreen = t.find("] [LOADING SCREEN] (") != std::string::npos;
                        replayed = true;
                    }
                }
            } else {
                gOffset = size;
            }
            gHistDirty = true;
        }
        gSkipExisting = false;
    }
    if (size > gOffset) {
        LARGE_INTEGER li;
        li.QuadPart = (LONGLONG)gOffset;
        SetFilePointerEx(h, li, nullptr, FILE_BEGIN);
        static std::vector<char> buf(1 << 20);
        while (gOffset < size) {
            DWORD want = (DWORD)std::min<uint64_t>(buf.size(), size - gOffset), got = 0;
            if (!ReadFile(h, buf.data(), want, &got, nullptr) || got == 0) break;
            gOffset += got;
            gPartial.append(buf.data(), got);
            size_t start = 0, nl;
            while ((nl = gPartial.find('\n', start)) != std::string::npos) {
                g.feed(gPartial.substr(start, nl - start));
                ++gLinesRead;
                start = nl + 1;
            }
            gPartial.erase(0, start);
        }
    }
    CloseHandle(h);
    if (replayed) limitReplayedStay();
}

// (re)open the log: keep stored runs/zones, track only what happens from now on
void fullReload() {
    gSkipExisting = true;
    gHistoryLoaded = false;
    g.loadAreaNames(areasPath());
    for (auto& z : g.zones) g.noteAreaLevel(z.areaId, z.level);  // ranges also from runs stored before
    g.resetSession();
    resetReader();
    gHead.clear();
    tick();
}

void browse() {
    wchar_t file[MAX_PATH];
    GetWindowTextW(hPath, file, MAX_PATH);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = hMain;
    ofn.lpstrFilter = L"PoE2 log (*.txt)\0*.txt\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) SetWindowTextW(hPath, file);
}

void applyPath() {
    wchar_t buf[MAX_PATH];
    GetWindowTextW(hPath, buf, MAX_PATH);
    gLogPath = buf;
    WritePrivateProfileStringW(L"main", L"LogPath", gLogPath.c_str(), iniPath().c_str());
    fullReload();
}

// Known places, best first: the running game, Steam (default + every library), standalone installs.
static std::vector<std::wstring> logCandidates() {
    const std::wstring rel = L"\\logs\\LatestClient.txt";
    std::vector<std::wstring> c;
    if (HWND game = FindWindowW(L"POEWindowClass", nullptr)) {  // folder of the running game's exe
        DWORD pid = 0;
        GetWindowThreadProcessId(game, &pid);
        if (HANDLE ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
            wchar_t exe[MAX_PATH];
            DWORD n = MAX_PATH;
            if (QueryFullProcessImageNameW(ph, 0, exe, &n)) {
                std::wstring d = exe;
                c.push_back(d.substr(0, d.find_last_of(L"\\/")) + rel);
            }
            CloseHandle(ph);
        }
    }
    std::vector<std::wstring> steam;
    std::wstring sp = regString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (!sp.empty()) {
        for (auto& ch : sp)
            if (ch == L'/') ch = L'\\';
        steam.push_back(sp);
    }
    steam.push_back(L"C:\\Program Files (x86)\\Steam");
    std::vector<std::wstring> libs = steam;
    for (auto& s : steam) {  // libraryfolders.vdf: "path"  "D:\\SteamLibrary"
        std::string vdf = readFileUtf8(s + L"\\steamapps\\libraryfolders.vdf");
        for (size_t p = 0; (p = vdf.find("\"path\"", p)) != std::string::npos; p += 6) {
            size_t a = vdf.find('"', p + 6), b = a == std::string::npos ? a : vdf.find('"', a + 1);
            if (b == std::string::npos) break;
            std::string v = vdf.substr(a + 1, b - a - 1), u;
            for (size_t i = 0; i < v.size(); ++i) {
                if (v[i] == '\\' && i + 1 < v.size() && v[i + 1] == '\\') ++i;  // "\\\\" in the file
                u += v[i];
            }
            libs.push_back(W(u));
        }
    }
    for (auto& l : libs) c.push_back(l + L"\\steamapps\\common\\Path of Exile 2" + rel);
    for (const wchar_t* d : {L"C:\\Program Files (x86)\\Grinding Gear Games\\Path of Exile 2",
                             L"C:\\Program Files\\Grinding Gear Games\\Path of Exile 2"})
        c.push_back(std::wstring(d) + rel);
    return c;
}

void resolveLogPath() {
    wchar_t buf[MAX_PATH] = L"";
    GetPrivateProfileStringW(L"main", L"LogPath", L"", buf, MAX_PATH, iniPath().c_str());
    gLogPath = *buf ? buf : kDefaultLog;
    if (fileExists(gLogPath)) return;
    for (auto& p : logCandidates())
        if (fileExists(p)) {
            gLogPath = p;
            WritePrivateProfileStringW(L"main", L"LogPath", p.c_str(), iniPath().c_str());
            return;
        }
    MessageBoxW(nullptr,
                L"LatestClient.txt of Path of Exile 2 was not found in the usual places (Steam libraries, standalone "
                L"install, running game).\n\nPlease pick it: it is in the game folder under logs\\LatestClient.txt.",
                L"Poe2 | MapStat", MB_ICONINFORMATION);
    wchar_t file[MAX_PATH] = L"LatestClient.txt";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = L"LatestClient.txt\0LatestClient.txt\0Log files (*.txt)\0*.txt\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Pick LatestClient.txt (Path of Exile 2\\logs)";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        gLogPath = file;
        WritePrivateProfileStringW(L"main", L"LogPath", file, iniPath().c_str());
    }  // cancelled: keep the default; Options shows "not found" and Browse... can set it later
}
