// GraphOverlay: the graph of the zone the player is in (Layouts tab: Make graph, or any picture with "graph" in its
// name). Without one it says so; with several, arrows on the left and right page through them (newest first).
#include "app.h"

HWND hGraphOv = nullptr, hGraphChk = nullptr;
bool gGraphOn = false;

namespace {

const wchar_t* kSec = L"GraphOverlay";
const int kArrow = 30;  // width of the arrow areas at the sides
const int kPad = 6;

std::vector<std::wstring> gGraphs;  // of the current zone, newest first
std::wstring gZoneName;  // shown when the zone has no graph
int gIdx = 0;
HBITMAP gPic = nullptr;  // the shown graph, scaled down once (it is scaled to the window when painted)
int gPicW = 0, gPicH = 0;
int gHover = 0;  // -1 left arrow, 1 right arrow, 0 none

void loadPicture() {
    if (gPic) DeleteObject(gPic);
    gPic = nullptr;
    if (gIdx >= 0 && gIdx < (int)gGraphs.size()) gPic = layoutPicture(gGraphs[gIdx], 1100, 750, gPicW, gPicH);
    if (hGraphOv) InvalidateRect(hGraphOv, nullptr, FALSE);
}

bool arrows() { return gGraphs.size() > 1; }

void page(int step) {
    if (!arrows()) return;
    int n = (int)gGraphs.size();
    gIdx = ((gIdx + step) % n + n) % n;
    loadPicture();
}

void paint(HDC dc, RECT rc) {
    FillRect(dc, &rc, brDarkest);
    fillRound(dc, rc, 12, C::Darkest, C::Line);
    RECT area = {rc.left + kPad, rc.top + kPad, rc.right - kPad, rc.bottom - kPad};
    if (arrows()) area.left += kArrow - kPad, area.right -= kArrow - kPad;
    if (!gPic) {
        SetBkMode(dc, TRANSPARENT);
        SelectObject(dc, fOvSmall ? fOvSmall : fNormal);
        SetTextColor(dc, C::Comment);
        std::wstring t = gZoneName.empty() ? L"GraphOverlay: no graph here" : L"No graph for " + gZoneName;
        DrawTextW(dc, t.c_str(), -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }
    if (area.right > area.left && area.bottom > area.top) {  // fit, keep the shape
        double s = std::min((double)(area.right - area.left) / gPicW, (double)(area.bottom - area.top) / gPicH);
        int w = std::max(1, (int)(gPicW * s)), h = std::max(1, (int)(gPicH * s));
        int x = (area.left + area.right - w) / 2, y = (area.top + area.bottom - h) / 2;
        HDC pdc = CreateCompatibleDC(dc);
        HGDIOBJ o = SelectObject(pdc, gPic);
        SetStretchBltMode(dc, HALFTONE);
        StretchBlt(dc, x, y, w, h, pdc, 0, 0, gPicW, gPicH, SRCCOPY);
        SelectObject(pdc, o);
        DeleteDC(pdc);
    }
    if (!arrows()) return;
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, fOvHead ? fOvHead : fBold);
    for (int side : {-1, 1}) {
        RECT a = side < 0 ? RECT{rc.left, rc.top, rc.left + kArrow, rc.bottom}
                          : RECT{rc.right - kArrow, rc.top, rc.right, rc.bottom};
        SetTextColor(dc, gHover == side ? C::Purple : C::Fg);
        DrawTextW(dc, side < 0 ? L"‹" : L"›", -1, &a, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    std::wstring n = std::to_wstring(gIdx + 1) + L"/" + std::to_wstring(gGraphs.size());
    RECT t = {rc.left, rc.bottom - 22, rc.right, rc.bottom - 4};
    SelectObject(dc, fOvSmall ? fOvSmall : fNormal);
    SetTextColor(dc, C::Comment);
    DrawTextW(dc, n.c_str(), -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

int arrowAt(HWND h, POINT pt) {  // client coordinates
    if (!arrows()) return 0;
    RECT rc;
    GetClientRect(h, &rc);
    if (pt.x < kArrow) return -1;
    if (pt.x >= rc.right - kArrow) return 1;
    return 0;
}

void savePos() {
    RECT wr;
    if (!hGraphOv || !GetWindowRect(hGraphOv, &wr)) return;
    WritePrivateProfileStringW(kSec, L"X", std::to_wstring(wr.left).c_str(), iniPath().c_str());
    WritePrivateProfileStringW(kSec, L"Y", std::to_wstring(wr.top).c_str(), iniPath().c_str());
    WritePrivateProfileStringW(kSec, L"W", std::to_wstring(wr.right - wr.left).c_str(), iniPath().c_str());
    WritePrivateProfileStringW(kSec, L"H", std::to_wstring(wr.bottom - wr.top).c_str(), iniPath().c_str());
    anchorSave(hGraphOv, kSec);
}

LRESULT CALLBACK GraphProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCHITTEST: {  // edges resize, arrows click, the rest drags
            POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
            RECT wr;
            GetWindowRect(h, &wr);
            const int E = 6;
            bool L = pt.x < wr.left + E, R = pt.x >= wr.right - E, T = pt.y < wr.top + E, B = pt.y >= wr.bottom - E;
            if (T && L) return HTTOPLEFT;
            if (T && R) return HTTOPRIGHT;
            if (B && L) return HTBOTTOMLEFT;
            if (B && R) return HTBOTTOMRIGHT;
            if (L) return HTLEFT;
            if (R) return HTRIGHT;
            if (T) return HTTOP;
            if (B) return HTBOTTOM;
            ScreenToClient(h, &pt);
            return arrowAt(h, pt) ? HTCLIENT : HTCAPTION;
        }
        case WM_NCCALCSIZE:  // no visible frame: the resize border is inside the window
            if (wp) return 0;
            break;
        case WM_MOUSEMOVE: {
            int a = arrowAt(h, {(short)LOWORD(lp), (short)HIWORD(lp)});
            if (a != gHover) {
                gHover = a;
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
            gHover = 0;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
            page(arrowAt(h, {(short)LOWORD(lp), (short)HIWORD(lp)}));
            return 0;
        case WM_NCLBUTTONDBLCLK:  // double-click: the graph in the Windows viewer
            if (gIdx < (int)gGraphs.size())
                ShellExecuteW(nullptr, L"open", gGraphs[gIdx].c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        case WM_NCRBUTTONDOWN:
            return 0;
        case WM_NCRBUTTONUP:
        case WM_CONTEXTMENU:
        case WM_RBUTTONUP:
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
        case WM_EXITSIZEMOVE:
            savePos();
            return 0;
        case WM_MOVING:
            snapOverlay(h, (RECT*)lp);
            return TRUE;
        case WM_SIZE: {
            RECT rc;
            GetClientRect(h, &rc);
            SetWindowRgn(h, CreateRoundRectRgn(0, 0, rc.right + 1, rc.bottom + 1, 12, 12), TRUE);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_GETMINMAXINFO:
            ((MINMAXINFO*)lp)->ptMinTrackSize = {160, 110};
            return 0;
        case WM_NCACTIVATE:
            return DefWindowProcW(h, msg, wp, -1);
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
            paint(mem, rc);
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

}  // namespace

void graphOverlayRefresh() {
    std::vector<std::wstring> now = gGraphOn ? currentZoneGraphs() : std::vector<std::wstring>{};
    std::wstring zone = W(g.locationName.empty() ? g.nameOf(g.location) : g.locationName);
    if (Tracker::isHideout(g.location)) zone.clear();
    if (zone != gZoneName && hGraphOv) InvalidateRect(hGraphOv, nullptr, FALSE);
    gZoneName = zone;
    if (now == gGraphs) {
        updateOverlayVisibility();
        return;
    }
    std::wstring shown = gIdx < (int)gGraphs.size() ? gGraphs[gIdx] : L"";
    bool newer = !now.empty() && (gGraphs.empty() || now[0] != gGraphs[0]);  // a graph was just made: show it
    gGraphs = now;
    gIdx = 0;
    if (!newer)
        for (int i = 0; i < (int)gGraphs.size(); ++i)
            if (gGraphs[i] == shown) gIdx = i;
    loadPicture();
    updateOverlayVisibility();
}

void graphOverlayTick() {
    if (!gGraphOn) return;
    static std::string lastArea;
    static int n = 0;
    if (g.location != lastArea || ++n % 6 == 0) {  // zone changed, or every 3 s (a graph made meanwhile)
        lastArea = g.location;
        graphOverlayRefresh();
    }
}

void showGraphOverlay(bool on) {
    gGraphOn = on;
    if (on && !hGraphOv) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = GraphProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"GraphOverlay";
        wc.style = CS_DBLCLKS;
        RegisterClassExW(&wc);
        auto ini = [](const wchar_t* k, int d) { return (int)GetPrivateProfileIntW(kSec, k, d, iniPath().c_str()); };
        // first start: under the timer overlay (same width), else inside the game window
        RECT pos = {40, 300, 460, 580}, o, gr;
        if (hOverlay && IsWindowVisible(hOverlay) && GetWindowRect(hOverlay, &o)) {
            int w = std::max(300, (int)(o.right - o.left));
            pos = {o.left, o.bottom + 8, o.left + w, o.bottom + 8 + w * 2 / 3};
        } else if (gameClientRect(gr)) {
            pos = {gr.right - 460, gr.top + 300, gr.right - 40, gr.top + 580};
        }
        bool saved = ini(L"X", -100000) != -100000;
        int x = saved ? ini(L"X", 40) : pos.left, y = saved ? ini(L"Y", 300) : pos.top;
        int w = saved ? ini(L"W", 420) : pos.right - pos.left, h = saved ? ini(L"H", 280) : pos.bottom - pos.top;
        hGraphOv = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
                                   wc.lpszClassName, L"GraphOverlay", WS_POPUP | WS_THICKFRAME, x, y, w, h, nullptr,
                                   nullptr, wc.hInstance, nullptr);
        SetLayeredWindowAttributes(hGraphOv, 0, overlayAlpha(), LWA_ALPHA);
        hideFromCapture(hGraphOv);
        if (!saved) savePos();  // the anchor to the game is kept from here
    }
    if (!on) {
        gGraphs.clear();
        loadPicture();
    }
    graphOverlayRefresh();
    updateOverlayVisibility();
    if (hGraphChk) InvalidateRect(hGraphChk, nullptr, FALSE);
}
