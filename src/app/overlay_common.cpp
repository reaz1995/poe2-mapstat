// Shared by the overlays: shown only with the game and kept right above its window, snapping while dragging,
// right-click menu, click-through.
#include "app.h"

#include <dwmapi.h>

// Windows feeds the snapped rect back into the next WM_MOVING, so snapping that rect would stick forever.
// Work from where the drag started + how far the mouse moved, then snap.
static ULONGLONG gHiddenUntil = 0;  // overlays a capture would show are hidden until then (snipping tool)

static POINT gDragPt;
static RECT gDragRc;

void dragStart(HWND self) {
    GetCursorPos(&gDragPt);
    GetWindowRect(self, &gDragRc);
}

void snapOverlay(HWND self, RECT* r) {
    const int T = 14, G = 6;  // snap distance, gap between overlays
    int w = r->right - r->left, h = r->bottom - r->top;
    POINT pt;
    GetCursorPos(&pt);
    r->left = gDragRc.left + pt.x - gDragPt.x;
    r->top = gDragRc.top + pt.y - gDragPt.y;
    r->right = r->left + w;
    r->bottom = r->top + h;
    int bestDx = T + 1, bestDy = T + 1, nx = r->left, ny = r->top;
    auto tryX = [&](int x) {
        if (abs(x - r->left) < bestDx) {
            bestDx = abs(x - r->left);
            nx = x;
        }
    };
    auto tryY = [&](int y) {
        if (abs(y - r->top) < bestDy) {
            bestDy = abs(y - r->top);
            ny = y;
        }
    };
    for (HWND o : {hOverlay, hCmdBar, hGraphOv, hStepsOv}) {
        if (!o || o == self || !IsWindowVisible(o)) continue;
        RECT q;
        GetWindowRect(o, &q);
        bool nearV = r->bottom + T >= q.top && r->top - T <= q.bottom;  // vertically close -> x snaps make sense
        bool nearH = r->right + T >= q.left && r->left - T <= q.right;  // horizontally close -> y snaps make sense
        if (nearV) {
            tryX(q.left);
            tryX(q.right - w);
            tryX(q.right + G);
            tryX(q.left - w - G);
        }
        if (nearH) {
            tryY(q.top);
            tryY(q.bottom - h);
            tryY(q.bottom + G);
            tryY(q.top - h - G);
        }
    }
    MONITORINFO mi = {};
    mi.cbSize = sizeof mi;
    if (GetMonitorInfoW(MonitorFromRect(r, MONITOR_DEFAULTTONEAREST), &mi)) {
        tryX(mi.rcWork.left);
        tryX(mi.rcWork.right - w);
        tryY(mi.rcWork.top);
        tryY(mi.rcWork.bottom - h);
    }
    RECT g;
    if (gameClientRect(g)) {  // and to the inside edges of the game window
        tryX(g.left);
        tryX(g.right - w);
        tryY(g.top);
        tryY(g.bottom - h);
    }
    r->left = nx;
    r->right = nx + w;
    r->top = ny;
    r->bottom = ny + h;
}

HWND findGame() {
    HWND w = FindWindowW(L"POEWindowClass", nullptr);  // the game's window class (PoE 1 and 2)
    if (!w) w = FindWindowW(nullptr, L"Path of Exile 2");
    return w;
}

// ---- position relative to the game window
// Each overlay keeps its distance to the nearest corner of the game's client area ("Anchor=right,bottom,dx,dy" in
// its settings section). When the game window moves, changes size or goes to another monitor, the overlays follow.
bool gameClientRect(RECT& r) {
    HWND game = findGame();
    if (!game || IsIconic(game) || !GetClientRect(game, &r)) return false;
    POINT a = {r.left, r.top}, b = {r.right, r.bottom};
    ClientToScreen(game, &a);
    ClientToScreen(game, &b);
    r = {a.x, a.y, b.x, b.y};
    return r.right > r.left && r.bottom > r.top;
}
// remember where the overlay is now, relative to the game (after the user moved / resized it)
void anchorSave(HWND ov, const wchar_t* sec) {
    RECT g, w;
    if (!ov || !gameClientRect(g) || !GetWindowRect(ov, &w)) return;
    bool right = (w.left + w.right) / 2 > (g.left + g.right) / 2;
    bool bottom = (w.top + w.bottom) / 2 > (g.top + g.bottom) / 2;
    int dx = right ? g.right - w.right : w.left - g.left;
    int dy = bottom ? g.bottom - w.bottom : w.top - g.top;
    wchar_t v[64];
    swprintf(v, 64, L"%d,%d,%d,%d", right ? 1 : 0, bottom ? 1 : 0, dx, dy);
    WritePrivateProfileStringW(sec, L"Anchor", v, iniPath().c_str());
}
static void anchorApply(HWND ov, const wchar_t* sec, const RECT& g) {
    wchar_t v[64];
    GetPrivateProfileStringW(sec, L"Anchor", L"", v, 64, iniPath().c_str());
    int right, bottom, dx, dy;
    if (swscanf(v, L"%d,%d,%d,%d", &right, &bottom, &dx, &dy) != 4) {  // older settings: take it from where it is
        anchorSave(ov, sec);
        return;
    }
    RECT w;
    GetWindowRect(ov, &w);
    int ww = w.right - w.left, wh = w.bottom - w.top;
    int x = right ? g.right - dx - ww : g.left + dx, y = bottom ? g.bottom - dy - wh : g.top + dy;
    if (x != w.left || y != w.top) SetWindowPos(ov, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static bool isOverlay(HWND w) {
    for (HWND o : {hOverlay, hCmdBar, hGraphOv, hStepsOv})
        if (o && w == o) return true;
    return false;
}

// The overlays sit right above the game window in the window order (not always on top): whatever is in front of
// the game (a browser, this app's window, the snipping tool) covers them too. When the game comes to the front it is
// put above them, so they are moved back above it.
static void keepAboveGame(HWND game) {
    // the first window above the game that isn't an overlay: the overlays go right under it
    HWND above = GetWindow(game, GW_HWNDPREV);
    while (above && isOverlay(above)) above = GetWindow(above, GW_HWNDPREV);
    bool belowTopmost = above && (GetWindowLongPtrW(above, GWL_EXSTYLE) & WS_EX_TOPMOST);
    for (HWND w : {hOverlay, hCmdBar, hGraphOv, hStepsOv}) {
        if (!w || !IsWindowVisible(w)) continue;
        // already above the game?
        bool ok = false;
        for (HWND x = GetWindow(game, GW_HWNDPREV); x; x = GetWindow(x, GW_HWNDPREV)) {
            if (x == w) {
                ok = true;
                break;
            }
            if (!isOverlay(x)) break;  // something else between the game and the overlay
        }
        if (ok) continue;
        SetWindowPos(w, !above || belowTopmost ? HWND_TOP : above, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}

// Overlays belong to the game: shown while its window is open and not minimized, placed relative to it.
void updateOverlayVisibility() {
    static RECT lastGame = {};
    HWND game = findGame();
    gGameVisible = game && IsWindowVisible(game) && !IsIconic(game);
    bool wantOv = hOverlay && gOverlayOn && gGameVisible, wantCb = hCmdBar && gCmdOn && gGameVisible;
    bool wantGr = hGraphOv && gGraphOn && gGameVisible, wantSt = hStepsOv && gStepsOn && gGameVisible;
    RECT g;
    if ((wantOv || wantCb || wantGr || wantSt) && gameClientRect(g)) {
        // game moved / resized, or an overlay is about to appear: place it first, so it never flashes at the old spot
        bool appearing = (wantOv && !IsWindowVisible(hOverlay)) || (wantCb && !IsWindowVisible(hCmdBar)) ||
                         (wantGr && !IsWindowVisible(hGraphOv)) || (wantSt && !IsWindowVisible(hStepsOv));
        if (appearing || !EqualRect(&g, &lastGame)) {
            if (wantOv) anchorApply(hOverlay, L"MapTimerOverlay", g);
            if (wantCb) anchorApply(hCmdBar, kCbSec, g);
            if (wantGr) anchorApply(hGraphOv, L"GraphOverlay", g);
            if (wantSt) anchorApply(hStepsOv, L"StepsOverlay", g);
            lastGame = g;
        }
    }
    const bool capHide = GetTickCount64() < gHiddenUntil;
    auto set = [capHide](HWND w, bool on) {
        if (capHide && capturedOnScreen(w)) on = false;
        if (w && (IsWindowVisible(w) != 0) != on) ShowWindow(w, on ? SW_SHOWNOACTIVATE : SW_HIDE);
    };
    set(hOverlay, gOverlayOn && gGameVisible);
    set(hCmdBar, gCmdOn && gGameVisible);
    set(hGraphOv, wantGr);
    set(hStepsOv, wantSt);

    if (gGameVisible) keepAboveGame(game);
}

// Right-click menu of an overlay (Dracula owner-drawn like the column menu).
void overlayMenu(HWND ov) {
    enum { CMD_OPEN = 30, CMD_CLOSE, CMD_PAUSE, CMD_STOP, CMD_RESET, CMD_FIT };
    // The timer overlay in the campaign also controls the campaign clock: Pause / Resume and Stop, or Reset
    // (a new run from zero) once it is stopped.
    bool campaign = ov == hOverlay && ovCampaignShown();
    bool stopped = g.cmpClock == Tracker::Clock::Stopped;
    std::vector<std::pair<int, const wchar_t*>> items;  // id 0 = separator
    if (campaign) {
        if (!stopped) items.push_back({CMD_PAUSE, g.cmpClock == Tracker::Clock::Paused ? L"Resume" : L"Pause"});
        items.push_back(stopped ? std::make_pair((int)CMD_RESET, L"Reset") : std::make_pair((int)CMD_STOP, L"Stop"));
        items.push_back({0, L""});
    }
    items.push_back({CMD_OPEN, L"Open Poe2 | MapStat"});
    if (ov == hStepsOv && stepsOverlayFixedHeight()) items.push_back({CMD_FIT, L"Fit height to steps"});
    items.push_back({0, L""});
    items.push_back({CMD_CLOSE, L"Close overlay"});
    gMenuItems.clear();
    gMenuItems.reserve(items.size());  // item data pointers must stay valid while the menu is open
    for (auto& [id, text] : items) gMenuItems.push_back({text, id == 0});
    HMENU m = CreatePopupMenu();
    MENUINFO mi = {};
    mi.cbSize = sizeof mi;
    mi.fMask = MIM_BACKGROUND;
    mi.hbrBack = brCard;
    SetMenuInfo(m, &mi);
    for (size_t i = 0; i < items.size(); ++i)
        AppendMenuW(m, MF_OWNERDRAW | (items[i].first ? 0 : MF_DISABLED), items[i].first, (LPCWSTR)&gMenuItems[i]);
    POINT pt;
    GetCursorPos(&pt);
    // Opened with Alt held (click-through): letting go of Alt would close the menu at once, so it opens once Alt
    // is up.
    for (ULONGLONG t0 = GetTickCount64(); (GetAsyncKeyState(VK_MENU) & 0x8000) && GetTickCount64() - t0 < 5000;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_SYSKEYUP || msg.message == WM_KEYUP) continue;  // the Alt release itself
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(15);
    }
    SetForegroundWindow(ov);  // so the menu closes when clicking elsewhere
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, ov, nullptr);
    PostMessageW(ov, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (cmd == CMD_PAUSE) {
        campaignAction(HK_PAUSE);
    } else if (cmd == CMD_STOP) {
        campaignAction(HK_STOP);
    } else if (cmd == CMD_RESET) {
        campaignAction(HK_START);
    } else if (cmd == CMD_OPEN) {
        showMain();
    } else if (cmd == CMD_FIT) {
        stepsOverlayFitHeight();
    } else if (cmd == CMD_CLOSE) {
        if (ov == hOverlay) {
            showOverlay(false);
            ovIniSet(L"On", 0);
        } else if (ov == hStepsOv) {
            showStepsOverlay(false);
            WritePrivateProfileStringW(L"StepsOverlay", L"On", L"0", iniPath().c_str());
        } else if (ov == hGraphOv) {
            showGraphOverlay(false);
            WritePrivateProfileStringW(L"GraphOverlay", L"On", L"0", iniPath().c_str());
        } else {
            showCmdBar(false);
            WritePrivateProfileStringW(kCbSec, L"On", L"0", iniPath().c_str());
        }
    }
}

// Click-through: the overlays get WS_EX_TRANSPARENT (with WS_EX_LAYERED, clicks then reach the window below: the
// game). Holding Alt takes it off, so they can be clicked, dragged, resized or right-clicked; a drag started with
// Alt goes on until the mouse button is let go.
bool gClickThru = true;
HWND hClickChk = nullptr;
static bool gThruNow = false;  // what the overlays have now

static void applyThru(bool thru) {
    for (HWND o : {hOverlay, hCmdBar, hGraphOv, hStepsOv}) {
        if (!o) continue;
        LONG_PTR ex = GetWindowLongPtrW(o, GWL_EXSTYLE);
        LONG_PTR want = thru ? ex | WS_EX_TRANSPARENT : ex & ~(LONG_PTR)WS_EX_TRANSPARENT;
        if (want != ex) SetWindowLongPtrW(o, GWL_EXSTYLE, want);
    }
    gThruNow = thru;
}

void clickThruPoll() {
    bool alt = GetAsyncKeyState(VK_MENU) & 0x8000;
    bool button = (GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON)) & 0x8000;
    bool thru = gClickThru && !alt && !(button && !gThruNow);  // keep usable mid-drag
    if (thru != gThruNow || thru) applyThru(thru);  // also catches overlays created since
}

void setClickThru(bool on) {
    gClickThru = on;
    ovIniSet(L"ClickThrough", on ? 1 : 0);
    applyThru(on);
    if (hClickChk) InvalidateRect(hClickChk, nullptr, FALSE);
}

// Options "Hide overlays in screenshots" (on by default): overlays are left out of screenshots (Windows 10 2004 and
// later), so the game's own capture, the snipping tool and other screenshot tools see the game under them. Off (or
// on older Windows) they show in screenshots; the app's own layout screenshots still hide them for a moment.
bool gHideInCapture = true;
HWND hCaptureChk = nullptr;

bool hideFromCapture(HWND h) {
    typedef BOOL(WINAPI * Fn)(HWND, DWORD);
    static Fn f = (Fn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowDisplayAffinity");
    const DWORD WDA_NONE_ = 0, WDA_EXCLUDEFROMCAPTURE_ = 0x11;
    if (!h || !f) return false;
    bool hidden = gHideInCapture && f(h, WDA_EXCLUDEFROMCAPTURE_);
    if (!gHideInCapture) f(h, WDA_NONE_);
    if (hidden) SetPropW(h, L"Poe2MapStat.NoCapture", (HANDLE)1);
    else RemovePropW(h, L"Poe2MapStat.NoCapture");
    return hidden;
}

bool capturedOnScreen(HWND h) { return h && !GetPropW(h, L"Poe2MapStat.NoCapture"); }

void setHideInCapture(bool on) {
    gHideInCapture = on;
    ovIniSet(L"HideInScreenshots", on ? 1 : 0);
    for (HWND o : {hOverlay, hCmdBar, hGraphOv, hStepsOv})
        if (o) hideFromCapture(o);
    if (hCaptureChk) InvalidateRect(hCaptureChk, nullptr, FALSE);
}

// Overlays that screenshots would show are hidden for ms (the snipping tool takes its picture as it opens).
bool hideCapturedOverlays(int ms) {
    bool any = false;
    for (HWND o : {hOverlay, hCmdBar, hGraphOv, hStepsOv}) any |= o && IsWindowVisible(o) && capturedOnScreen(o);
    if (!any) return false;
    gHiddenUntil = GetTickCount64() + (ULONGLONG)ms;
    updateOverlayVisibility();
    DwmFlush();
    return true;
}
