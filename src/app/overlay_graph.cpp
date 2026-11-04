// Graph / outline overlay (GraphOverlay): the graphs and outlines of the zone the player is in (Layouts tab: Make
// graph, any picture with "graph" in its name, and the "<seed>" outlines). Graphs come first, newest
// first, then the outlines; arrows on the left and right page through them. A layout screenshot shows its outline.
// Options can limit it to graphs.
#include "app.h"

HWND hGraphOv = nullptr, hGraphChk = nullptr, hGraphOnlyChk = nullptr;
bool gGraphOn = false, gGraphOnly = false;

namespace {

const wchar_t* kSec = L"GraphOverlay";
const int kArrow = 30;  // width of the arrow areas at the sides
const int kPad = 6;

std::vector<std::wstring> gGraphs;  // pictures of the current zone: graphs, then outlines
std::wstring gZoneName;  // shown when the zone has no graph
int gIdx = 0;
HBITMAP gPic = nullptr;  // the shown picture, scaled down once (it is scaled to the window when painted)
int gPicW = 0, gPicH = 0;
RECT gCrop = {};  // the part with the map (outline pictures have wide empty margins)
std::wstring gWant;  // a picture to show once it is listed (the outline of a screenshot just taken)
int gHover = 0;  // -1 left arrow, 1 right arrow, 0 none

void loadPicture() {
    if (gPic) DeleteObject(gPic);
    gPic = nullptr;
    if (gIdx >= 0 && gIdx < (int)gGraphs.size()) gPic = layoutPicture(gGraphs[gIdx], 1400, 900, gPicW, gPicH);
    gCrop = {0, 0, gPicW, gPicH};
    if (gPic) {  // crop to what is drawn
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = gPicW, bi.bmiHeader.biHeight = -gPicH;
        bi.bmiHeader.biPlanes = 1, bi.bmiHeader.biBitCount = 32;
        std::vector<uint32_t> px((size_t)gPicW * gPicH);
        HDC dc = GetDC(nullptr);
        if (GetDIBits(dc, gPic, 0, gPicH, px.data(), &bi, DIB_RGB_COLORS)) {
            int x0 = gPicW, y0 = gPicH, x1 = -1, y1 = -1;
            for (int y = 0; y < gPicH; ++y)
                for (int x = 0; x < gPicW; ++x) {
                    uint32_t p = px[(size_t)y * gPicW + x];
                    if (((p >> 16) & 255) > 60 || ((p >> 8) & 255) > 60 || (p & 255) > 60)
                        x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
                }
            if (x1 >= 0)
                gCrop = {std::max(0, x0 - 10), std::max(0, y0 - 10), std::min(gPicW, x1 + 11),
                         std::min(gPicH, y1 + 11)};
        }
        ReleaseDC(nullptr, dc);
    }
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
        std::wstring what = gGraphOnly ? L"graph" : L"graph or outline";
        std::wstring t = gZoneName.empty() ? L"No " + what + L" here" : L"No " + what + L" for " + gZoneName;
        DrawTextW(dc, t.c_str(), -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }
    int cw = gCrop.right - gCrop.left, ch = gCrop.bottom - gCrop.top;
    if (area.right > area.left && area.bottom > area.top && cw > 0 && ch > 0) {  // fit, keep the shape
        double s = std::min((double)(area.right - area.left) / cw, (double)(area.bottom - area.top) / ch);
        int w = std::max(1, (int)(cw * s)), h = std::max(1, (int)(ch * s));
        int x = (area.left + area.right - w) / 2, y = (area.top + area.bottom - h) / 2;
        HDC pdc = CreateCompatibleDC(dc);
        HGDIOBJ o = SelectObject(pdc, gPic);
        SetStretchBltMode(dc, HALFTONE);
        StretchBlt(dc, x, y, w, h, pdc, gCrop.left, gCrop.top, cw, ch, SRCCOPY);
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
    std::wstring n = std::to_wstring(gIdx + 1) + L"/" + std::to_wstring(gGraphs.size()) + L"  \u00b7  " +
                     (isGraph(gGraphs[gIdx]) ? L"graph" : L"outline");
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
    std::vector<std::wstring> now = gGraphOn ? currentZonePictures(gGraphOnly) : std::vector<std::wstring>{};
    std::wstring zone = W(g.locationDisplay());
    if (Tracker::isHideout(g.location)) zone.clear();
    if (zone != gZoneName && hGraphOv) InvalidateRect(hGraphOv, nullptr, FALSE);
    gZoneName = zone;
    int want = -1;  // the outline of a screenshot just taken (its picture changed even if its name did not)
    for (int i = 0; i < (int)now.size() && !gWant.empty(); ++i)
        if (now[i] == gWant) want = i;
    if (want >= 0) gWant.clear();
    if (now == gGraphs && want < 0) {
        updateOverlayVisibility();
        return;
    }
    std::wstring shown = gIdx < (int)gGraphs.size() ? gGraphs[gIdx] : L"";
    bool newer = !now.empty() && isGraph(now[0]) && (gGraphs.empty() || now[0] != gGraphs[0]);  // a graph just made
    gGraphs = now;
    gIdx = 0;
    if (want >= 0) gIdx = want;
    else if (!newer)
        for (int i = 0; i < (int)gGraphs.size(); ++i)
            if (gGraphs[i] == shown) gIdx = i;
    loadPicture();
    updateOverlayVisibility();
}

void graphOverlayShowPicture(const std::wstring& path) {
    gWant = path;
    graphOverlayRefresh();
}

void setGraphOnly(bool on) {
    gGraphOnly = on;
    WritePrivateProfileStringW(kSec, L"OnlyGraphs", on ? L"1" : L"0", iniPath().c_str());
    if (hGraphOnlyChk) InvalidateRect(hGraphOnlyChk, nullptr, FALSE);
    graphOverlayRefresh();
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
        hGraphOv = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
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
