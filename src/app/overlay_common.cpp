// Shared by both overlays: show only with the game, snapping while dragging, right-click menu.
#include "app.h"


// Windows feeds the snapped rect back into the next WM_MOVING, so snapping that rect would stick forever.
// Work from where the drag started + how far the mouse moved, then snap.
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
    auto tryX = [&](int x) { if (abs(x - r->left) < bestDx) { bestDx = abs(x - r->left); nx = x; } };
    auto tryY = [&](int y) { if (abs(y - r->top) < bestDy) { bestDy = abs(y - r->top); ny = y; } };
    for (HWND o : {hOverlay, hCmdBar}) {
        if (!o || o == self || !IsWindowVisible(o)) continue;
        RECT q;
        GetWindowRect(o, &q);
        bool nearV = r->bottom + T >= q.top && r->top - T <= q.bottom;   // vertically close -> x snaps make sense
        bool nearH = r->right + T >= q.left && r->left - T <= q.right;   // horizontally close -> y snaps make sense
        if (nearV) { tryX(q.left); tryX(q.right - w); tryX(q.right + G); tryX(q.left - w - G); }
        if (nearH) { tryY(q.top); tryY(q.bottom - h); tryY(q.bottom + G); tryY(q.top - h - G); }
    }
    MONITORINFO mi = {};
    mi.cbSize = sizeof mi;
    if (GetMonitorInfoW(MonitorFromRect(r, MONITOR_DEFAULTTONEAREST), &mi)) {
        tryX(mi.rcWork.left); tryX(mi.rcWork.right - w);
        tryY(mi.rcWork.top); tryY(mi.rcWork.bottom - h);
    }
    RECT g;
    if (gameClientRect(g)) {  // and to the inside edges of the game window
        tryX(g.left); tryX(g.right - w);
        tryY(g.top); tryY(g.bottom - h);
    }
    r->left = nx; r->right = nx + w;
    r->top = ny; r->bottom = ny + h;
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

// Overlays belong to the game: shown only while its window is open and not minimized, placed relative to it.
void updateOverlayVisibility() {
    static RECT lastGame = {};
    HWND game = findGame();
    gGameVisible = game && !IsIconic(game);
    bool wantOv = hOverlay && gOverlayOn && gGameVisible, wantCb = hCmdBar && gCmdOn && gGameVisible;
    RECT g;
    if ((wantOv || wantCb) && gameClientRect(g)) {
        // game moved / resized, or an overlay is about to appear: place it first, so it never flashes at the old spot
        bool appearing = (wantOv && !IsWindowVisible(hOverlay)) || (wantCb && !IsWindowVisible(hCmdBar));
        if (appearing || !EqualRect(&g, &lastGame)) {
            if (wantOv) anchorApply(hOverlay, L"MapTimerOverlay", g);
            if (wantCb) anchorApply(hCmdBar, kCbSec, g);
            lastGame = g;
        }
    }
    auto set = [](HWND w, bool on) {
        if (w && (IsWindowVisible(w) != 0) != on) ShowWindow(w, on ? SW_SHOWNOACTIVATE : SW_HIDE);
    };
    set(hOverlay, gOverlayOn && gGameVisible);
    set(hCmdBar, gCmdOn && gGameVisible);
}

// Right-click menu of an overlay (Dracula owner-drawn like the column menu).
void overlayMenu(HWND ov) {
    enum { CMD_OPEN = 30, CMD_CLOSE, CMD_PAUSE, CMD_STOP, CMD_RESET };
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
    } else if (cmd == CMD_CLOSE) {
        if (ov == hOverlay) {
            showOverlay(false);
            ovIniSet(L"On", 0);
        } else {
            showCmdBar(false);
            WritePrivateProfileStringW(kCbSec, L"On", L"0", iniPath().c_str());
        }
    }
}


