// Load past Map session events: LatestClient.txt / Client.txt (last N days, everything, custom range).
#include "app.h"

static std::wstring clientTxtPath();
static bool isDate(const char* p, size_t n);
static std::string dateAt(HANDLE h, uint64_t off);
static uint64_t seekDate(HANDLE h, uint64_t size, const std::string& cutoff);
static void importWorker(std::wstring path, std::string cutoff, std::string endCut, std::map<std::string, std::string> names);
static std::string logStamp(const SYSTEMTIME& st);
static void startImport(const std::wstring& path, const std::string& from, const std::string& to = "");
static std::string daysAgo(int days);
static LRESULT CALLBACK RangeProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
static void customRangeDialog();

static std::wstring clientTxtPath() {
    size_t s = gLogPath.find_last_of(L"\\/");
    return (s == std::wstring::npos ? std::wstring() : gLogPath.substr(0, s + 1)) + L"Client.txt";
}

static bool isDate(const char* p, size_t n) {  // "2026/11/02 10:00:00"
    return n >= 19 && isdigit((unsigned char)p[0]) && p[4] == '/' && p[7] == '/' && p[10] == ' ' && p[13] == ':';
}

// date of the first complete line at/after `off` ("" if none found nearby)
static std::string dateAt(HANDLE h, uint64_t off) {
    char buf[8192];
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)off;
    SetFilePointerEx(h, li, nullptr, FILE_BEGIN);
    DWORD got = 0;
    if (!ReadFile(h, buf, sizeof buf, &got, nullptr) || !got) return "";
    for (size_t i = 0; i < got; ++i) {
        if (buf[i] != '\n' && !(off == 0 && i == 0)) continue;
        size_t b = buf[i] == '\n' ? i + 1 : i;
        if (b < got && isDate(buf + b, got - b)) return std::string(buf + b, 19);
    }
    return "";
}

// offset of a line start before the first line dated >= cutoff (lines are in time order)
static uint64_t seekDate(HANDLE h, uint64_t size, const std::string& cutoff) {
    uint64_t lo = 0, hi = size;
    while (hi - lo > (1u << 16)) {
        uint64_t mid = lo + (hi - lo) / 2;
        std::string d = dateAt(h, mid);
        if (d.empty() || d >= cutoff) hi = mid;
        else lo = mid;
    }
    return lo;
}

static void importWorker(std::wstring path, std::string cutoff, std::string endCut, std::map<std::string, std::string> names) {
    auto* r = new ImportResult;
    r->source = U8(path.substr(path.find_last_of(L"\\/") + 1));
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        r->err = "Cannot open " + U8(path);
        PostMessageW(hMain, WM_IMPORTED, 0, (LPARAM)r);
        return;
    }
    LARGE_INTEGER sz;
    GetFileSizeEx(h, &sz);
    uint64_t size = (uint64_t)sz.QuadPart;
    uint64_t start = cutoff.empty() ? 0 : seekDate(h, size, cutoff), off = start;
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)start;
    SetFilePointerEx(h, li, nullptr, FILE_BEGIN);
    Tracker t;
    t.areaNames = std::move(names);
    bool started = cutoff.empty(), stop = false;
    std::vector<char> buf(4u << 20);
    std::string part;
    auto line = [&](const std::string& ln) {
        if (!started) {  // skip the few lines before the cutoff left over from the search
            if (!isDate(ln.data(), ln.size()) || ln.compare(0, 19, cutoff) < 0) return;
            started = true;
        }
        if (!endCut.empty() && isDate(ln.data(), ln.size()) && ln.compare(0, 19, endCut) > 0) {  // past "To"
            stop = true;
            return;
        }
        if (ln.find("***** LOG FILE OPENING *****") != std::string::npos) {  // next game session
            t.suspend(t.lastTick);
            t.resetSession();
        }
        if (r->firstDate.empty() && isDate(ln.data(), ln.size())) r->firstDate = ln.substr(0, 19);
        t.feed(ln);
        ++r->lines;
    };
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(h, buf.data(), (DWORD)buf.size(), &got, nullptr) || !got) break;
        off += got;
        part.append(buf.data(), got);
        size_t b = 0, nl;
        while (!stop && (nl = part.find('\n', b)) != std::string::npos) {
            line(part.substr(b, nl - b));
            b = nl + 1;
        }
        if (stop) break;
        part.erase(0, b);
        gImportPct = size > start ? (int)((off - start) * 100 / (size - start)) : 100;
    }
    if (!part.empty() && !stop) line(part);
    CloseHandle(h);
    t.suspend(t.lastTick);  // whatever was open is paused; re-entering the same map continues its row
    r->data = t.serialize();
    r->names = t.areaNames;
    r->levels = t.areaLevels;
    PostMessageW(hMain, WM_IMPORTED, 0, (LPARAM)r);
}

void setImportButton() {
    int p = gImportPct;
    SetWindowTextW(hLoadHist, p < 0 ? L"Load past Map session events..." : (L"Loading... " + std::to_wstring(p) + L"%").c_str());
    EnableWindow(hLoadHist, p < 0);
}

static std::string logStamp(const SYSTEMTIME& st) {  // "2026/11/02 10:00:00" like the log lines
    char b[32];
    snprintf(b, sizeof b, "%04d/%02d/%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return b;
}

// from/to = "" -> from the start / to the end of the file
static void startImport(const std::wstring& path, const std::string& from, const std::string& to) {
    gImportPct = 0;
    setImportButton();
    std::thread(importWorker, path, from, to, g.areaNames).detach();
}

static std::string daysAgo(int days) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u = {{ft.dwLowDateTime, ft.dwHighDateTime}};
    u.QuadPart -= (ULONGLONG)days * 24 * 3600 * 10000000ULL;
    ft = {u.LowPart, u.HighPart};
    FILETIME lt;
    SYSTEMTIME st;
    FileTimeToLocalFileTime(&ft, &lt);
    FileTimeToSystemTime(&lt, &st);
    return logStamp(st);
}

void importDone(ImportResult* r) {
    gImportPct = -1;
    setImportButton();
    if (!r->err.empty()) {
        MessageBoxW(hMain, W(r->err).c_str(), L"Load past Map session events", MB_ICONERROR);
        delete r;
        return;
    }
    saveFile(gMapFile);  // current maps stay in their own file (Restore map session)
    std::string d = r->firstDate.empty() ? std::string("empty") : r->firstDate;
    for (char& c : d)
        if (c == '/' || c == ':') c = '-';
    setSessionFile(gMapFile, newSessionPath(gMapFile, W(d), L" (" + W(r->source) + L")"));
    Tracker imp;
    imp.deserialize(r->data);
    g.replaceMaps(imp);  // Maps list; Summary adds the imported maps; Campaign and the live map/zone stay
    for (auto& [k, v] : r->names) g.areaNames[k] = v;
    for (auto& [k, v] : r->levels) {
        g.noteAreaLevel(k, v.first);
        g.noteAreaLevel(k, v.second);
    }
    g.saveAreaNames(areasPath());
    gHistoryLoaded = true;
    saveData();
    gHistDirty = true;
    refreshUi();
    refreshSummary();
    MessageBoxW(hMain, (std::to_wstring(r->lines) + L" lines read from " + W(r->source) + L".\n" +
                        std::to_wstring(g.history.size()) + L" maps loaded.").c_str(),
                L"Load past Map session events", MB_ICONINFORMATION);
    delete r;
}

// ---- "Custom range...": pick From / To dates (calendar drop-downs) and which Client.txt to read
static HWND hRangeDlg, hRangeFrom, hRangeTo, hRangeFile;

static bool gRangeOk = false;

static SYSTEMTIME gRgFrom, gRgTo;

static wchar_t gRgFile[MAX_PATH];

static LRESULT CALLBACK RangeProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            HWND c = (HWND)lp;
            if (c == hRangeFrom || c == hRangeTo || GetParent(c) == hRangeFrom || GetParent(c) == hRangeTo)
                break;  // date fields draw their own (dark) text: keep them in the standard light style
            bool edit = msg == WM_CTLCOLOREDIT;
            SetTextColor((HDC)wp, edit ? C::Fg : C::Comment);
            SetBkColor((HDC)wp, edit ? C::Card : C::Bg);
            return (LRESULT)(edit ? brCard : brBg);
        }
        case WM_DRAWITEM:
            drawButton((DRAWITEMSTRUCT*)lp);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wp) == ID_RG_BROWSE) {
                wchar_t file[MAX_PATH];
                GetWindowTextW(hRangeFile, file, MAX_PATH);
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof ofn;
                ofn.hwndOwner = h;
                ofn.lpstrFilter = L"PoE log (*.txt)\0*.txt\0All files\0*.*\0";
                ofn.lpstrFile = file;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
                if (GetOpenFileNameW(&ofn)) SetWindowTextW(hRangeFile, file);
            } else if (LOWORD(wp) == ID_RG_OK || LOWORD(wp) == IDOK) {
                DateTime_GetSystemtime(hRangeFrom, &gRgFrom);
                DateTime_GetSystemtime(hRangeTo, &gRgTo);
                GetWindowTextW(hRangeFile, gRgFile, MAX_PATH);
                gRangeOk = true;
                DestroyWindow(h);
            } else if (LOWORD(wp) == ID_RG_CANCEL || LOWORD(wp) == IDCANCEL) {
                DestroyWindow(h);
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            hRangeDlg = nullptr;
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void customRangeDialog() {
    static bool reg = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!reg) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = RangeProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"Poe2MapStatRange";
        RegisterClassExW(&wc);
        reg = true;
    }
    const int W_ = 520, H_ = 230;
    RECT mr;
    GetWindowRect(hMain, &mr);
    RECT wr = {0, 0, W_, H_};
    AdjustWindowRectEx(&wr, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    hRangeDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"Poe2MapStatRange", L"Load past Map session events - custom range",
                                WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, (mr.left + mr.right - ww) / 2,
                                (mr.top + mr.bottom - wh) / 2, ww, wh, hMain, nullptr, inst, nullptr);
    SetClassLongPtrW(hRangeDlg, GCLP_HBRBACKGROUND, (LONG_PTR)brBg);
    darkTitleBar(hRangeDlg);
    auto ctl = [&](const wchar_t* cls, const wchar_t* txt, DWORD st, int x, int y, int w, int hh, int id) {
        HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | st, x, y, w, hh, hRangeDlg, (HMENU)(INT_PTR)id, inst, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)fNormal, FALSE);
        return c;
    };
    auto button = [&](const wchar_t* txt, int x, int y, int w, int id, Btn kind) {
        HWND b = ctl(L"BUTTON", txt, BS_OWNERDRAW | WS_TABSTOP, x, y, w, 34, id);
        SendMessageW(b, WM_SETFONT, (WPARAM)fBold, FALSE);
        gBtnKind[b] = kind;
        SetWindowSubclass(b, BtnProc, 1, 0);
        return b;
    };
    ctl(L"STATIC", L"From", 0, 20, 24, 60, 20, 0);
    hRangeFrom = ctl(DATETIMEPICK_CLASSW, L"", DTS_SHORTDATECENTURYFORMAT | WS_TABSTOP, 80, 20, 160, 28, 0);
    ctl(L"STATIC", L"To", 0, 270, 24, 40, 20, 0);
    hRangeTo = ctl(DATETIMEPICK_CLASSW, L"", DTS_SHORTDATECENTURYFORMAT | WS_TABSTOP, 310, 20, 160, 28, 0);
    ctl(L"STATIC", L"Log file", 0, 20, 78, 60, 20, 0);
    hRangeFile = ctl(L"EDIT", clientTxtPath().c_str(), ES_AUTOHSCROLL | WS_TABSTOP | WS_BORDER, 80, 74, 300, 28, 0);
    button(L"Browse...", 390, 71, 110, ID_RG_BROWSE, Btn::Normal);
    ctl(L"STATIC", L"Both days are included (From 00:00:00 to To 23:59:59).", 0, 20, 122, 480, 20, 0);
    button(L"Load", W_ - 250, H_ - 56, 110, ID_RG_OK, Btn::Accent);
    button(L"Cancel", W_ - 130, H_ - 56, 110, ID_RG_CANCEL, Btn::Normal);
    for (HWND d : {hRangeFrom, hRangeTo}) {  // calendar colours follow the theme
        DateTime_SetMonthCalColor(d, MCSC_BACKGROUND, C::Card);
        DateTime_SetMonthCalColor(d, MCSC_MONTHBK, C::Card);
        DateTime_SetMonthCalColor(d, MCSC_TEXT, C::Fg);
        DateTime_SetMonthCalColor(d, MCSC_TITLEBK, C::Darkest);
        DateTime_SetMonthCalColor(d, MCSC_TITLETEXT, C::Purple);
        DateTime_SetMonthCalColor(d, MCSC_TRAILINGTEXT, C::Comment);
    }
    SYSTEMTIME now, weekAgo;
    GetLocalTime(&now);
    {
        std::string w = daysAgo(7);  // default: last 7 days
        weekAgo = now;
        sscanf(w.c_str(), "%hu/%hu/%hu", &weekAgo.wYear, &weekAgo.wMonth, &weekAgo.wDay);
    }
    DateTime_SetSystemtime(hRangeFrom, GDT_VALID, &weekAgo);
    DateTime_SetSystemtime(hRangeTo, GDT_VALID, &now);
    gRangeOk = false;
    ShowWindow(hRangeDlg, SW_SHOW);
    EnableWindow(hMain, FALSE);
    MSG m;
    while (hRangeDlg && GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (hRangeDlg && m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) { DestroyWindow(hRangeDlg); continue; }
        if (!hRangeDlg || !IsDialogMessageW(hRangeDlg, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    EnableWindow(hMain, TRUE);
    SetForegroundWindow(hMain);
    if (!gRangeOk) return;
    SYSTEMTIME a = gRgFrom, b = gRgTo;
    const wchar_t* file = gRgFile;
    a.wHour = a.wMinute = a.wSecond = 0;
    b.wHour = 23, b.wMinute = 59, b.wSecond = 59;
    std::string from = logStamp(a), to = logStamp(b);
    if (from > to) std::swap(from, to), from.replace(11, 8, "00:00:00"), to.replace(11, 8, "23:59:59");
    if (GetFileAttributesW(file) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(hMain, (std::wstring(L"File not found:\n") + file).c_str(), L"Load past Map session events", MB_ICONERROR);
        return;
    }
    if (!ask(L"Load past Map session events",
             (L"Replace the maps on the Maps tab with " + W(from.substr(0, 10)) + L" - " + W(to.substr(0, 10)) +
              L"?\nSummary keeps its maps and adds these. Campaign is not changed.\n\n"
              L"The current list stays in its own MapSession file - Maps > Restore map session... "
              L"brings it back.")
                 .c_str()))
        return;
    startImport(file, from, to);
}

void pastEventsMenu() {
    gMenuItems.clear();
    gMenuItems.reserve(8);
    const wchar_t* labels[] = {L"This game session  (LatestClient.txt)", L"", L"Last 24 hours  (Client.txt)",
                               L"Last 7 days  (Client.txt)", L"Last 30 days  (Client.txt)", L"Everything  (Client.txt)",
                               L"", L"Custom range...  (any Client.txt)"};
    const int ids[] = {50, 0, 51, 52, 53, 54, 0, 55};
    for (auto l : labels) gMenuItems.push_back({l, !*l});
    HMENU m = CreatePopupMenu();
    MENUINFO mi = {};
    mi.cbSize = sizeof mi;
    mi.fMask = MIM_BACKGROUND;
    mi.hbrBack = brCard;
    SetMenuInfo(m, &mi);
    for (int i = 0; i < 8; ++i)
        AppendMenuW(m, MF_OWNERDRAW | (gMenuItems[i].separator ? MF_DISABLED : 0), ids[i], (LPCWSTR)&gMenuItems[i]);
    RECT br;
    GetWindowRect(hLoadHist, &br);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTBUTTON, br.left, br.bottom + 2, 0, hMain, nullptr);
    DestroyMenu(m);
    if (!cmd) return;
    if (cmd == 55) {
        customRangeDialog();
        return;
    }
    if (!ask(L"Load past Map session events",
             L"Replace the maps on the Maps tab with the past events?\nSummary keeps its maps and adds these. "
             L"Campaign is not changed.\n\nThe current list stays in its own MapSession file - Maps > Restore map "
             L"session... brings it back."))
        return;
    if (cmd == 50) {
        startImport(gLogPath, "");
        return;
    }
    if (GetFileAttributesW(clientTxtPath().c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(hMain, (L"Client.txt not found next to the log file:\n" + clientTxtPath()).c_str(), L"Load past Map session events",
                    MB_ICONERROR);
        return;
    }
    startImport(clientTxtPath(), cmd == 54 ? std::string() : daysAgo(cmd == 51 ? 1 : cmd == 52 ? 7 : 30));
}
