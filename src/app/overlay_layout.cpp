// LayoutOverlay: the outline of the instance the player is in, as it stands after the last layout screenshot. It
// appears with the first screenshot of an instance and goes away when the player leaves for another instance (or
// closes it from its menu). It is left out of screenshots, so it can stay open while taking more.
#include "app.h"

HWND hLayoutOv = nullptr;

namespace {

const wchar_t* kSec = L"LayoutOverlay";
const int kPad = 6;

bool gShown = false;  // wanted on screen (an outline of the current instance)
uint32_t gSeed = 0;  // the instance it belongs to
std::wstring gPath;  // the outline picture
HBITMAP gPic = nullptr;  // scaled down once (scaled to the window when painted)
int gPicW = 0, gPicH = 0;
RECT gCrop = {};  // the part of the picture with the map (outline pictures have wide empty margins)

void loadPicture() {
    if (gPic) DeleteObject(gPic);
    gPic = gPath.empty() ? nullptr : layoutPicture(gPath, 1400, 900, gPicW, gPicH);
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
                gCrop = {std::max(0, x0 - 10), std::max(0, y0 - 10), std::min(gPicW, x1 + 11), std::min(gPicH, y1 + 11)};
        }
        ReleaseDC(nullptr, dc);
    }
    if (hLayoutOv) InvalidateRect(hLayoutOv, nullptr, FALSE);
}

void paint(HDC dc, RECT rc) {
    FillRect(dc, &rc, brDarkest);
    fillRound(dc, rc, 12, C::Darkest, C::Line);
    RECT area = {rc.left + kPad, rc.top + kPad, rc.right - kPad, rc.bottom - kPad};
    if (!gPic) {
        SetBkMode(dc, TRANSPARENT);
        SelectObject(dc, fOvSmall ? fOvSmall : fNormal);
        SetTextColor(dc, C::Comment);
        DrawTextW(dc, L"LayoutOverlay: no outline", -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    int cw = gCrop.right - gCrop.left, ch = gCrop.bottom - gCrop.top;
    if (area.right <= area.left || area.bottom <= area.top || cw <= 0 || ch <= 0) return;
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

void savePos() {
    RECT wr;
    if (!hLayoutOv || !GetWindowRect(hLayoutOv, &wr)) return;
    WritePrivateProfileStringW(kSec, L"X", std::to_wstring(wr.left).c_str(), iniPath().c_str());
    WritePrivateProfileStringW(kSec, L"Y", std::to_wstring(wr.top).c_str(), iniPath().c_str());
    WritePrivateProfileStringW(kSec, L"W", std::to_wstring(wr.right - wr.left).c_str(), iniPath().c_str());
    WritePrivateProfileStringW(kSec, L"H", std::to_wstring(wr.bottom - wr.top).c_str(), iniPath().c_str());
    anchorSave(hLayoutOv, kSec);
}

LRESULT CALLBACK LayoutProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCHITTEST: {  // edges resize, the rest drags
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
            return HTCAPTION;
        }
        case WM_NCCALCSIZE:
            if (wp) return 0;
            break;
        case WM_NCLBUTTONDBLCLK:  // double-click: the outline in the Windows viewer
            if (!gPath.empty()) ShellExecuteW(nullptr, L"open", gPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
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

void create() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = LayoutProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"LayoutOverlay";
    wc.style = CS_DBLCLKS;
    RegisterClassExW(&wc);
    auto ini = [](const wchar_t* k, int d) { return (int)GetPrivateProfileIntW(kSec, k, d, iniPath().c_str()); };
    // first time: a wide window in the upper middle of the game
    RECT pos = {300, 100, 1100, 500}, gr;
    if (gameClientRect(gr)) {
        int gw = gr.right - gr.left, gh = gr.bottom - gr.top;
        pos = {gr.left + gw / 4, gr.top + gh / 10, gr.right - gw / 4, gr.top + gh / 10 + gh * 2 / 5};
    }
    bool saved = ini(L"X", -100000) != -100000;
    int x = saved ? ini(L"X", pos.left) : pos.left, y = saved ? ini(L"Y", pos.top) : pos.top;
    int w = saved ? ini(L"W", 800) : pos.right - pos.left, h = saved ? ini(L"H", 400) : pos.bottom - pos.top;
    hLayoutOv = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE, wc.lpszClassName,
                                L"LayoutOverlay", WS_POPUP | WS_THICKFRAME, x, y, w, h, nullptr, nullptr, wc.hInstance,
                                nullptr);
    SetLayeredWindowAttributes(hLayoutOv, 0, overlayAlpha(), LWA_ALPHA);
    hideFromCapture(hLayoutOv);
    if (!saved) savePos();
}

}  // namespace

bool layoutOverlayShown() { return gShown; }

void layoutOverlayShow(const std::wstring& outlinePath, uint32_t seed) {
    if (outlinePath.empty() || !seed) return;
    if (!hLayoutOv) create();
    gSeed = seed;
    gPath = outlinePath;
    gShown = true;
    loadPicture();
    updateOverlayVisibility();
}

void layoutOverlayHide() {
    gShown = false;
    gPath.clear();
    loadPicture();
    updateOverlayVisibility();
}

void layoutOverlayTick() {  // bound to its instance: gone once the player is in another one
    if (gShown && currentSeed() != gSeed && !Tracker::isHideout(g.location) && !g.location.empty()) layoutOverlayHide();
}
