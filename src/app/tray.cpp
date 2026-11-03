// Notification-area (tray) icon and its menu.
#include "app.h"

void showMain() {
    ShowWindow(hMain, IsIconic(hMain) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hMain);
}

void trayIcon(DWORD op) {
    NOTIFYICONDATAW n = {};
    n.cbSize = sizeof n;
    n.hWnd = hMain;
    n.uID = 1;
    n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    n.uCallbackMessage = WM_TRAY;
    n.hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(2), IMAGE_ICON,
                                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wcscpy(n.szTip, L"Poe2 | MapStat");
    Shell_NotifyIconW(op, &n);
    if (n.hIcon) DestroyIcon(n.hIcon);
}

// Short notification from the tray icon (e.g. a layout screenshot was saved).
void trayBalloon(const std::wstring& title, const std::wstring& text) {
    NOTIFYICONDATAW n = {};
    n.cbSize = sizeof n;
    n.hWnd = hMain;
    n.uID = 1;
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    lstrcpynW(n.szInfoTitle, title.c_str(), ARRAYSIZE(n.szInfoTitle));
    lstrcpynW(n.szInfo, text.c_str(), ARRAYSIZE(n.szInfo));
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void trayMenu() {
    gMenuItems.clear();
    gMenuItems.reserve(8);
    const wchar_t* tick = L"\u2713  ";
    const wchar_t* none = L"     ";
    gMenuItems.push_back({L"Open Poe2 | MapStat", false});
    gMenuItems.push_back({L"", true});
    gMenuItems.push_back({std::wstring(gOverlayOn ? tick : none) + L"MapTimerOverlay", false});
    gMenuItems.push_back({std::wstring(gCmdOn ? tick : none) + L"CommandBarOverlay", false});
    gMenuItems.push_back({L"", true});
    gMenuItems.push_back({L"Exit", false});
    const int ids[] = {40, 0, 41, 42, 0, 43};
    HMENU m = CreatePopupMenu();
    MENUINFO mi = {};
    mi.cbSize = sizeof mi;
    mi.fMask = MIM_BACKGROUND;
    mi.hbrBack = brCard;
    SetMenuInfo(m, &mi);
    for (int i = 0; i < 6; ++i)
        AppendMenuW(m, MF_OWNERDRAW | (gMenuItems[i].separator ? MF_DISABLED : 0), ids[i], (LPCWSTR)&gMenuItems[i]);
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hMain);  // so the menu closes when clicking elsewhere
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hMain, nullptr);
    PostMessageW(hMain, WM_NULL, 0, 0);
    DestroyMenu(m);
    switch (cmd) {
        case 40: showMain(); break;
        case 41: SendMessageW(hMain, WM_COMMAND, ID_OVERLAY, 0); break;
        case 42: SendMessageW(hMain, WM_COMMAND, ID_CMDBAR, 0); break;
        case 43: SendMessageW(hMain, WM_COMMAND, ID_EXIT, 0); break;
    }
}
