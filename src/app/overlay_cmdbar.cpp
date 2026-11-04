// CommandBarOverlay: buttons that type chat commands (/hideout, /exit) into the game.
#include "app.h"

// Small bar with buttons that type a chat command into Path of Exile 2 (one click = one command).
struct CmdButton {
    const wchar_t* label;
    const wchar_t* key;
    const wchar_t* def;
    const COLORREF* color;
};

static std::wstring cmdText(int i);
static void sendChatCommand(const std::wstring& cmd);
static RECT cmdButtonRect(int i);
static void paintCmdBar(HDC dc, RECT rc);
static LRESULT CALLBACK CmdBarProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);

static const CmdButton kCmds[] = {
    {L"Hideout", L"Hideout", L"/hideout", &C::Cyan},
    {L"Exit", L"Exit", L"/exit", &C::Orange},  // exit to character selection
};

static const int kCmdN = sizeof(kCmds) / sizeof(kCmds[0]);

static const int kCbGrip = 16, kCbBtnW = 92, kCbH = 36, kCbPad = 5, kCbGap = 5;

static int gCmdHover = -1;

static std::wstring cmdText(int i) {
    wchar_t buf[256];
    GetPrivateProfileStringW(kCbSec, kCmds[i].key, kCmds[i].def, buf, 256, iniPath().c_str());
    return buf;
}

// Bring the game to the front and type: Enter, command, Enter.
static void sendChatCommand(const std::wstring& cmd) {
    HWND game = findGame();
    if (!game) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    if (IsIconic(game)) ShowWindow(game, SW_RESTORE);
    SetForegroundWindow(game);
    Sleep(60);
    std::vector<INPUT> in;
    auto vk = [&](WORD k) {
        INPUT d = {};
        d.type = INPUT_KEYBOARD;
        d.ki.wVk = k;
        in.push_back(d);
        d.ki.dwFlags = KEYEVENTF_KEYUP;
        in.push_back(d);
    };
    auto ch = [&](wchar_t c) {
        INPUT d = {};
        d.type = INPUT_KEYBOARD;
        d.ki.wScan = c;
        d.ki.dwFlags = KEYEVENTF_UNICODE;
        in.push_back(d);
        d.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        in.push_back(d);
    };
    vk(VK_RETURN);
    for (wchar_t c : cmd) ch(c);
    vk(VK_RETURN);
    SendInput((UINT)in.size(), in.data(), sizeof(INPUT));
}

static RECT cmdButtonRect(int i) {
    int x = kCbGrip + kCbPad + i * (kCbBtnW + kCbGap);
    return {x, kCbPad, x + kCbBtnW, kCbH - kCbPad};
}

static void paintCmdBar(HDC dc, RECT rc) {
    FillRect(dc, &rc, brDarkest);
    HPEN pen = CreatePen(PS_SOLID, 1, C::Line);
    HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, 12, 12);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(pen);
    // grip: two columns of dots
    HBRUSH dot = CreateSolidBrush(C::Comment);
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 2; ++col) {
            RECT d = {6 + col * 4, 10 + row * 4, 8 + col * 4, 12 + row * 4};
            FillRect(dc, &d, dot);
        }
    DeleteObject(dot);
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, fBold);
    for (int i = 0; i < kCmdN; ++i) {
        RECT b = cmdButtonRect(i);
        bool hov = gCmdHover == i;
        fillRound(dc, b, 10, hov ? C::Line : C::Card, hov ? *kCmds[i].color : C::Line);
        SetTextColor(dc, *kCmds[i].color);
        DrawTextW(dc, kCmds[i].label, -1, &b, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

static LRESULT CALLBACK CmdBarProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCHITTEST: {  // the grip drags, the buttons are clickable
            POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
            ScreenToClient(h, &pt);
            return pt.x < kCbGrip ? HTCAPTION : HTCLIENT;
        }
        case WM_MOUSEMOVE: {
            int x = (short)LOWORD(lp), y = (short)HIWORD(lp), hov = -1;
            for (int i = 0; i < kCmdN; ++i) {
                RECT b = cmdButtonRect(i);
                if (x >= b.left && x < b.right && y >= b.top && y < b.bottom) hov = i;
            }
            if (hov != gCmdHover) {
                gCmdHover = hov;
                InvalidateRect(h, nullptr, FALSE);
                TRACKMOUSEEVENT t = {};
                t.cbSize = sizeof t;
                t.dwFlags = TME_LEAVE;
                t.hwndTrack = h;
                TrackMouseEvent(&t);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            gCmdHover = -1;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
            if (gCmdHover >= 0) sendChatCommand(cmdText(gCmdHover));
            return 0;
        case WM_NCLBUTTONDBLCLK:  // double-click the grip: open the main window
            showMain();
            return 0;
        case WM_NCRBUTTONDOWN:  // no system menu on the drag area
            return 0;
        case WM_NCRBUTTONUP:
        case WM_CONTEXTMENU:
            overlayMenu(h);
            return 0;
        case WM_MEASUREITEM:
            measureMenu((MEASUREITEMSTRUCT*)lp);
            return TRUE;
        case WM_DRAWITEM:
            drawMenu((DRAWITEMSTRUCT*)lp);
            return TRUE;
        case WM_ENTERSIZEMOVE:
            dragStart(h);
            return 0;
        case WM_EXITSIZEMOVE: {
            RECT wr;
            GetWindowRect(h, &wr);
            WritePrivateProfileStringW(kCbSec, L"X", std::to_wstring(wr.left).c_str(), iniPath().c_str());
            WritePrivateProfileStringW(kCbSec, L"Y", std::to_wstring(wr.top).c_str(), iniPath().c_str());
            anchorSave(h, kCbSec);
            return 0;
        }
        case WM_NCACTIVATE:
            return DefWindowProcW(h, msg, wp, -1);
        case WM_MOVING:
            snapOverlay(h, (RECT*)lp);
            return TRUE;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ ob = SelectObject(mem, bmp);
            paintCmdBar(mem, rc);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, ob);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(h, &ps);
            return 0;
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void showCmdBar(bool on) {
    gCmdOn = on;
    if (on && !hCmdBar) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = CmdBarProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_HAND);
        wc.lpszClassName = L"CommandBarOverlay";
        wc.style = CS_DBLCLKS;
        RegisterClassExW(&wc);
        int w = kCbGrip + kCbPad * 2 + kCmdN * kCbBtnW + (kCmdN - 1) * kCbGap;
        int x = GetPrivateProfileIntW(kCbSec, L"X", 40, iniPath().c_str());
        int y = GetPrivateProfileIntW(kCbSec, L"Y", 220, iniPath().c_str());
        hCmdBar =
            CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE, wc.lpszClassName,
                            L"CommandBarOverlay", WS_POPUP, x, y, w, kCbH, nullptr, nullptr, wc.hInstance, nullptr);
        SetLayeredWindowAttributes(hCmdBar, 0, overlayAlpha(), LWA_ALPHA);
        hideFromCapture(hCmdBar);
        SetWindowRgn(hCmdBar, CreateRoundRectRgn(0, 0, w + 1, kCbH + 1, 12, 12), TRUE);
    }
    updateOverlayVisibility();
    if (hCmdChk) InvalidateRect(hCmdChk, nullptr, FALSE);
}
