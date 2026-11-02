// Entry point, main window procedure and the 0.5 s heartbeat.
#include "app.h"

static void setOnTop(bool on);
static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show);

void tick() {
    updateOverlayVisibility();
    if (gImportPct >= 0) setImportButton();
    pollLog();
    refreshUi();
    refreshExperience();  // follows level ups and area changes even while another tab is open
    if (hOverlay && gOverlayOn) {
        ovSyncFooter();
        InvalidateRect(hOverlay, nullptr, FALSE);
    }
    static int n = 0;
    if (++n % 20 == 0) saveData();  // every 10 s
}

static void setOnTop(bool on) {
    gOnTopOn = on;
    SetWindowPos(hMain, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    InvalidateRect(hOnTop, nullptr, FALSE);
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            hMain = h;
            loadData();
            darkTitleBar(h);
            loadHotkeys();
            loadExperience();
            createControls();
            installKeyHook();
            updateThemeLabel();
            SetTimer(h, ID_TIMER_POLL, 500, nullptr);
            fullReload();
            return 0;
        case WM_SIZE:
            layout(LOWORD(lp), HIWORD(lp));
            return 0;
        case WM_GETMINMAXINFO:
            ((MINMAXINFO*)lp)->ptMinTrackSize = {860, 660};
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            paintMain(dc, rc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_DRAWITEM:
            if (((DRAWITEMSTRUCT*)lp)->CtlType == ODT_MENU) drawMenu((DRAWITEMSTRUCT*)lp);
            else drawButton((DRAWITEMSTRUCT*)lp);
            return TRUE;
        case WM_MEASUREITEM:
            if (((MEASUREITEMSTRUCT*)lp)->CtlType == ODT_MENU) measureMenu((MEASUREITEMSTRUCT*)lp);
            return TRUE;
        case WM_NOTIFY: {
            NMHDR* nh = (NMHDR*)lp;
            bool isList = nh->hwndFrom == hHist || nh->hwndFrom == hSumList || nh->hwndFrom == hCmpList;
            if (nh->code == NM_CUSTOMDRAW && isList) return listCustomDraw((NMLVCUSTOMDRAW*)lp);
            if (nh->code == NM_RCLICK && isList) {  // right click on a row: menu for the column under the cursor
                LVHITTESTINFO ht = {};
                GetCursorPos(&ht.pt);
                ScreenToClient(nh->hwndFrom, &ht.pt);
                ListView_SubItemHitTest(nh->hwndFrom, &ht);
                int col = ht.iItem >= 0 ? ht.iSubItem : -1;
                if (col >= 0 && ((gHiddenCols[nh->hwndFrom] >> col) & 1)) col = -1;
                showColumnMenu(nh->hwndFrom, col);
                return 0;
            }
            return 0;
        }
        case WM_TIMER:
#ifdef UPDATE_REPO
            if (wp == ID_TIMER_UPD) {
                KillTimer(h, ID_TIMER_UPD);
                updCheck(false);
                return 0;
            }
#endif
            tick();
            return 0;
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wp;
            HWND c = (HWND)lp;
            bool onCard = std::find(gOnCard.begin(), gOnCard.end(), c) != gOnCard.end();
            SetBkColor(dc, onCard ? C::Card : C::Bg);
            SetTextColor(dc, staticColor(c));
            return (LRESULT)(onCard ? brCard : brBg);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = (HDC)wp;
            SetBkColor(dc, C::Card);
            SetTextColor(dc, C::Fg);
            return (LRESULT)brCard;
        }
        case WM_HOTKEY_ACT:
            campaignAction((int)wp);
            return 0;
        case WM_KEY_CAPTURED:
            keyCaptured((UINT)wp, (int)lp);
            return 0;
        case WM_COMMAND:
            if (HIWORD(wp) == EN_CHANGE && LOWORD(wp) >= ID_EXP_NAME && LOWORD(wp) <= ID_EXP_GAIN) {
                expEditChanged((HWND)lp);
                return 0;
            }
            switch (LOWORD(wp)) {
                case ID_TAB_GEN:
                    showMapsPage();
                    break;
                case ID_TAB_SES:
                    showPage(PG_GENERAL);
                    break;
                case ID_TAB_SUM:
                    showPage(PG_SUMMARY);
                    break;
                case ID_TAB_CMP:
                    showPage(PG_CAMPAIGN);
                    break;
                case ID_TAB_EXP:
                    showPage(PG_EXPERIENCE);
                    break;
                case ID_EXP_OVCHK:
                    toggleExpOverlay();
                    break;
                case ID_GEAR:
                    showPage(PG_OPTIONS);
                    break;
                default:
                    if (LOWORD(wp) >= ID_ACT_FIRST && LOWORD(wp) < ID_ACT_FIRST + (int)gActs.size()) {
                        selectAct(gActs[LOWORD(wp) - ID_ACT_FIRST]);
                        refreshCampaign(false);
                    }
                    break;
                case ID_BROWSE: browse(); applyPath(); break;
                case ID_RELOAD: applyPath(); break;
                case ID_LOAD_HIST: pastEventsMenu(); break;
                case ID_THEME_SEL: themeMenu(); break;
                case ID_RESTORE_GEN: restoreSession(gMapFile); break;
                case ID_RESTORE_CMP: restoreSession(gCmpFile); break;
                case ID_CMP_RESET: resetCampaignRun(); break;
                case ID_GEN_RESET: resetGeneralSession(); break;
                case ID_SUM_RESET: resetSummaryData(); break;
                case ID_EXP_GEN: exportListView(hHist, exportName(L"maps").c_str()); break;
                case ID_EXP_SUM: exportListView(hSumList, exportName(L"summary").c_str()); break;
                case ID_EXP_CMP: exportListView(hCmpList, exportName(L"campaign").c_str()); break;
                case ID_REC_MINUS:
                case ID_REC_PLUS:
                    setOverlayRecords(gOvRecords + (LOWORD(wp) == ID_REC_PLUS ? 1 : -1));
                    SetWindowTextW(hRecVal, std::to_wstring(gOvRecords).c_str());
                    break;
                case ID_ALPHA_MINUS:
                case ID_ALPHA_PLUS:
                    setOverlayTransparency(gOvTransparency + (LOWORD(wp) == ID_ALPHA_PLUS ? 10 : -10));
                    break;
                case ID_CLOSE_CHK:
                    gStartInTray = !gStartInTray;
                    WritePrivateProfileStringW(L"main", L"StartInTray", gStartInTray ? L"1" : L"0", iniPath().c_str());
                    InvalidateRect(hCloseChk, nullptr, FALSE);
                    break;
#ifdef UPDATE_REPO
                case ID_UPD_CHK:
                    gAutoUpdate = !gAutoUpdate;
                    WritePrivateProfileStringW(L"main", L"AutoUpdate", gAutoUpdate ? L"1" : L"0", iniPath().c_str());
                    InvalidateRect(hUpdChk, nullptr, FALSE);
                    break;
                case ID_UPD_TAB: updCheck(true); break;
#endif
                case ID_EXIT:
                    gExiting = true;
                    DestroyWindow(h);
                    break;
                case ID_CMDBAR:
                    showCmdBar(!gCmdOn);
                    WritePrivateProfileStringW(kCbSec, L"On", gCmdOn ? L"1" : L"0", iniPath().c_str());
                    break;
                case ID_OVERLAY:
                    showOverlay(!gOverlayOn);
                    ovIniSet(L"On", gOverlayOn ? 1 : 0);
                    break;
                case ID_CMP_STOP:
                    campaignAction(g.cmpClock == Tracker::Clock::Stopped ? HK_START : HK_STOP);
                    break;
                case ID_CMP_PAUSE: campaignAction(HK_PAUSE); break;
                case ID_KEY_BTN0:
                case ID_KEY_BTN1:
                case ID_KEY_BTN2:
                    beginKeyCapture(LOWORD(wp) - ID_KEY_BTN0);
                    break;
                case ID_KEYS_CHK:
                    toggleHotkeys();
                    break;
                case ID_ONTOP:
                    setOnTop(!gOnTopOn);
                    WritePrivateProfileStringW(L"main", L"OnTop", gOnTopOn ? L"1" : L"0", iniPath().c_str());
                    break;
            }
            return 0;
        case WM_CLOSE:  // X keeps tracking in the tray; Exit (tray menu / Options) really quits
            if (!gExiting) {
                ShowWindow(h, SW_HIDE);
                return 0;
            }
            break;
        case WM_SYSCOMMAND:
            if ((wp & 0xFFF0) == SC_MINIMIZE) {  // no taskbar button, so minimize = hide to tray
                ShowWindow(h, SW_HIDE);
                return 0;
            }
            break;
        case WM_TRAY:
            switch (LOWORD(lp)) {
                case WM_LBUTTONUP:
                    if (IsWindowVisible(h) && GetForegroundWindow() == h) ShowWindow(h, SW_HIDE);
                    else showMain();
                    break;
                case WM_RBUTTONUP:
                case WM_CONTEXTMENU:
                    trayMenu();
                    break;
            }
            return 0;
        case WM_IMPORTED:
            importDone((ImportResult*)lp);
            return 0;
#ifdef UPDATE_REPO
        case WM_UPD_FOUND:
            updFound((UpdResult*)lp);
            return 0;
        case WM_UPD_DOWNLOADED:
            updInstall((UpdResult*)lp);
            return 0;
#endif
        case WM_SHOWMAIN:  // a second copy was started
            showMain();
            return 0;
        case WM_DESTROY:
            removeKeyHook();
            trayIcon(NIM_DELETE);
            if (hOverlay) saveOverlayPos();
            g.suspend(nowTick());  // close the open map/zone with the time played so far (no-op after an update)
            saveData();
            PostQuitMessage(0);
            return 0;
    }
    if (gTaskbarCreated && msg == gTaskbarCreated) trayIcon(NIM_ADD);
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    // One copy only: two copies would track the same log twice and overwrite each other's stored data.
    bool updated = wcsstr(GetCommandLineW(), L"--updated") != nullptr;
    gMutex = CreateMutexW(nullptr, TRUE, L"Local\\Poe2MapStat_SingleInstance");
    for (int i = 0; updated && GetLastError() == ERROR_ALREADY_EXISTS && i < 50; ++i) {  // old copy still closing
        CloseHandle(gMutex);
        Sleep(100);
        gMutex = CreateMutexW(nullptr, TRUE, L"Local\\Poe2MapStat_SingleInstance");
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(L"Poe2MapStat", nullptr)) {
            AllowSetForegroundWindow(ASFW_ANY);
            PostMessageW(other, WM_SHOWMAIN, 0, 0);
        }
        return 0;
    }
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    gExeDir = exe;
    gExeDir = gExeDir.substr(0, gExeDir.find_last_of(L"\\/"));
    DeleteFileW((gExeDir + L"\\Poe2MapStat.old.exe").c_str());  // left over from the last update
    {
        const wchar_t* ad = _wgetenv(L"APPDATA");
        gDataDir = (ad && *ad ? std::wstring(ad) : gExeDir) + L"\\Poe2MapStat";
        CreateDirectoryW(gDataDir.c_str(), nullptr);
        // one-time move of files that older versions kept next to the exe
        CopyFileW((gExeDir + L"\\poe2_maptracker.ini").c_str(), iniPath().c_str(), TRUE);
        CopyFileW((gExeDir + L"\\areas.txt").c_str(), (gDataDir + L"\\areas.txt").c_str(), TRUE);
    }

    bool firstRun = GetFileAttributesW(iniPath().c_str()) == INVALID_FILE_ATTRIBUTES;
    initSessionFiles();
    resolveLogPath();

    INITCOMMONCONTROLSEX icc = {};

    icc.dwSize = sizeof icc;

    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_DATE_CLASSES;
    InitCommonControlsEx(&icc);
    g.onHistoryChanged = [] { gHistDirty = true; };

    loadStartupTheme();
    initDarkMode();
    setThemeColors();

    WNDCLASSEXW wc = {};

    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = brBg;
    wc.lpszClassName = L"Poe2MapStat";
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(2), IMAGE_ICON, 32, 32, 0);
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(2), IMAGE_ICON, 16, 16, 0);
    RegisterClassExW(&wc);

    // owned by a hidden window -> no taskbar button; the app lives in the tray
    HWND owner = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    HWND h = CreateWindowExW(0, wc.lpszClassName, L"Poe2 | MapStat", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1060, 800, owner, nullptr, inst, nullptr);
    gTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    trayIcon(NIM_ADD);
    if (GetPrivateProfileIntW(L"main", L"OnTop", 0, iniPath().c_str())) setOnTop(true);
    gOvRecords = std::clamp((int)ovIniInt(L"Records", 4), 0, 10);
    SetWindowTextW(hRecVal, std::to_wstring(gOvRecords).c_str());
    setOverlayTransparency(ovIniInt(L"Transparency", 10));
    gStartInTray = GetPrivateProfileIntW(L"main", L"StartInTray", 1, iniPath().c_str()) != 0;
    gGameVisible = false;
    // react at once when the game is minimized / restored (the 0.5 s poll covers start / close)
    SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr,
                    [](HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) { updateOverlayVisibility(); }, 0, 0,
                    WINEVENT_OUTOFCONTEXT);
    if (ovIniInt(L"On", 1)) showOverlay(true);
    if (GetPrivateProfileIntW(kCbSec, L"On", 0, iniPath().c_str())) showCmdBar(true);
    if (firstRun || !gStartInTray || updated) ShowWindow(h, show);
#ifdef UPDATE_REPO
    gAutoUpdate = GetPrivateProfileIntW(L"main", L"AutoUpdate", 1, iniPath().c_str()) != 0;
    if (updated) MessageBoxW(h, (L"Updated to v" + W(APP_VERSION) + L".").c_str(), L"Poe2 | MapStat", MB_ICONINFORMATION);
    else if (gAutoUpdate) SetTimer(h, ID_TIMER_UPD, 5000, nullptr);  // check a few seconds after start
#endif  // first start shows the window so it can be found

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(h, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    return 0;
}
