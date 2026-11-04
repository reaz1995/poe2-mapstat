// StepsOverlay: the steps of the zone the player is in (Steps tab), up to the number set in Options: the last one
// done (green) and the next ones to do, each with its details. The zone and done/total on top, the notes as a footer.
// The Complete step / Undo step hotkeys mark the next / last one; a click on a line copies it; a double-click on the
// rest opens the app on the Steps tab. Its height follows the steps unless a height was dragged.
#include "app.h"

#include <map>

HWND hStepsOv = nullptr, hStepsChk = nullptr, hStepsLbl = nullptr, hStepsMinus = nullptr, hStepsVal = nullptr,
     hStepsPlus = nullptr;
bool gStepsOn = false;
int gStepsShown = 5;

namespace {

const wchar_t* kSec = L"StepsOverlay";
const int kPad = 8;

struct Row {
    int step;
    RECT text;
    std::vector<RECT> sub;  // its details, smaller and further in
};
std::vector<Row> gRows;  // from the last layout
std::vector<steps::Note> gNotes;
std::vector<RECT> gNoteRects;
int gSepY = -1;  // the line between steps and notes
// Every line (step, detail, note) can be copied: hovered it is underlined, a click copies it (with click-through on:
// Alt + click), e.g. a regex to paste into the game's search.
struct Hit {
    RECT r;
    std::string text;
};
std::vector<Hit> gHits;  // the lines of the last paint
int gHover = -1, gCopied = -1;  // index in gHits
const UINT_PTR kCopiedTimer = 7;

HFONT underlined(HFONT f) {
    static std::map<HFONT, HFONT> made;
    auto it = made.find(f);
    if (it != made.end()) return it->second;
    LOGFONTW lf = {};
    GetObjectW(f, sizeof lf, &lf);
    lf.lfUnderline = TRUE;
    return made[f] = CreateFontIndirectW(&lf);
}

int hitAt(POINT client) {
    for (size_t i = 0; i < gHits.size(); ++i)
        if (PtInRect(&gHits[i].r, client)) return (int)i;
    return -1;
}

void copyLine(HWND h, int i) {
    if (i < 0 || i >= (int)gHits.size()) return;
    std::wstring w = W(gHits[i].text);
    if (!OpenClipboard(h)) return;
    EmptyClipboard();
    if (HGLOBAL m = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t))) {
        memcpy(GlobalLock(m), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(m);
        if (!SetClipboardData(CF_UNICODETEXT, m)) GlobalFree(m);
    }
    CloseClipboard();
    gCopied = i;  // shown green for a moment
    SetTimer(h, kCopiedTimer, 700, nullptr);
    InvalidateRect(h, nullptr, FALSE);
}

int gMinH = 0;   // header + notes footer: the window is never smaller, so both always show in full

HFONT rowFont() { return fOvSmall ? fOvSmall : fNormal; }
HFONT headFont() { return fOvSmall ? fOvSmall : fBold; }
HFONT subFont() { return fOvTiny ? fOvTiny : fNormal; }
const int kSubIndent = 14;

COLORREF mix(COLORREF a, COLORREF b, int pctA) {
    auto ch = [&](int s) { return (((a >> s) & 255) * pctA + ((b >> s) & 255) * (100 - pctA)) / 100; };
    return RGB(ch(0), ch(8), ch(16));
}

// wrapped text at x..right from y: its rect
RECT measure(HDC dc, const std::wstring& t, int x, int y, int right, int minH) {
    RECT r = {x, y, right, y + 1000};
    DrawTextW(dc, t.c_str(), -1, &r, DT_LEFT | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    return {x, y, right, y + std::max((int)(r.bottom - r.top), minH)};
}

// rows for the window width; returns the height the window needs
int layoutRows(HDC dc, int width) {
    gRows.clear();
    gNoteRects.clear();
    gSepY = -1;
    auto list = stepsCurrent(nullptr);
    auto show = steps::shown(stepsAll(), list, stepsProgress().done, gStepsShown);
    gNotes = gStepsNotes ? stepsNotes() : std::vector<steps::Note>();
    TEXTMETRICW tm, sm;
    SelectObject(dc, subFont());
    GetTextMetricsW(dc, &sm);
    SelectObject(dc, headFont());
    GetTextMetricsW(dc, &tm);
    int y = kPad + tm.tmHeight + 6;  // header line
    const int headH = y;
    int footTop = -1;
    SelectObject(dc, rowFont());
    GetTextMetricsW(dc, &tm);
    const int right = width - kPad;
    for (int i : show) {
        const steps::Step& st = stepsAll()[i];
        Row row{i, {}, {}};
        SelectObject(dc, rowFont());
        row.text = measure(dc, W(st.text), kPad, y, right, tm.tmHeight);
        y = row.text.bottom;
        SelectObject(dc, subFont());
        for (const std::string& n : st.sub) {
            row.sub.push_back(measure(dc, W(n), kPad + kSubIndent, y + 1, right, sm.tmHeight));
            y = row.sub.back().bottom;
        }
        gRows.push_back(std::move(row));
        y += 4;
    }
    if (show.empty()) y += tm.tmHeight + 4;  // "no steps" line
    if (!gNotes.empty()) {
        gSepY = y + 1;  // the line above the notes (paint moves both to the bottom of the window)
        footTop = y;
        y += 7;
        for (const steps::Note& n : gNotes) {
            SelectObject(dc, n.sub ? subFont() : rowFont());
            int x = kPad + (n.sub ? kSubIndent : 0);
            gNoteRects.push_back(measure(dc, W(n.text), x, y + (n.sub ? 1 : 0), right, n.sub ? sm.tmHeight : tm.tmHeight));
            y = gNoteRects.back().bottom + (n.sub ? 0 : 2);
        }
        y += 4;
    }
    int h = y + kPad - 4;
    gMinH = headH + (footTop >= 0 ? h - footTop : kPad);
    return h;
}

void paint(HDC dc, RECT rc) {
    FillRect(dc, &rc, brDarkest);
    fillRound(dc, rc, 12, C::Darkest, C::Line);
    SetBkMode(dc, TRANSPARENT);
    layoutRows(dc, rc.right);

    // header: zone (or "Steps") left, done/total right
    auto list = stepsCurrent(nullptr);
    int done = 0;
    for (int i : list) done += stepsProgress().isDone(stepsAll()[i]);
    SelectObject(dc, headFont());
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    std::wstring count = list.empty() ? L"" : std::to_wstring(done) + L"/" + std::to_wstring(list.size());
    SIZE cs = {};
    GetTextExtentPoint32W(dc, count.c_str(), (int)count.size(), &cs);
    RECT cr = {rc.right - kPad - cs.cx, kPad, rc.right - kPad, kPad + tm.tmHeight};
    SetTextColor(dc, done == (int)list.size() && !list.empty() ? C::Green : C::Comment);
    DrawTextW(dc, count.c_str(), -1, &cr, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
    std::wstring title = stepsZoneName();
    if (title.empty()) title = L"Steps";
    RECT hr = {kPad, kPad, cr.left - 8, kPad + tm.tmHeight};
    SetTextColor(dc, C::Purple);
    DrawTextW(dc, title.c_str(), -1, &hr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    gHits.clear();
    auto draw = [&](HFONT f, COLORREF c, const std::string& s, RECT r) {
        int i = (int)gHits.size();
        gHits.push_back({r, s});
        SelectObject(dc, i == gHover ? underlined(f) : f);
        SetTextColor(dc, i == gCopied ? C::Green : c);
        std::wstring w = W(s);
        DrawTextW(dc, w.c_str(), -1, &r, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    };
    // The notes are a footer: the line and the notes sit at the bottom of the window, the steps fill the space above
    // (a fixed height: steps that don't fit are left out).
    const int headBottom = kPad + tm.tmHeight + 6;
    int limit = rc.bottom - kPad + 2;
    if (!gNoteRects.empty() && gSepY >= 0) {
        int dy = (rc.bottom - kPad) - gNoteRects.back().bottom;
        for (RECT& r : gNoteRects) OffsetRect(&r, 0, dy);
        gSepY += dy;
        limit = gSepY - 2;
    }
    if (gRows.empty()) {
        RECT r = {kPad, headBottom, rc.right - kPad, headBottom + tm.tmHeight + 4};
        SelectObject(dc, rowFont());
        SetTextColor(dc, C::Comment);
        if (r.bottom <= limit)
            DrawTextW(dc, stepsAll().empty() ? L"Write steps in the Steps tab" : L"No steps for this zone", -1, &r,
                      DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    for (const Row& r : gRows) {
        if (r.text.bottom > limit) break;
        const steps::Step& st = stepsAll()[r.step];
        bool on = stepsProgress().isDone(st);
        COLORREF c = on ? C::Green : C::Fg;
        if (st.optional) c = mix(c, C::Darkest, 55);  // optional: a shade down
        draw(rowFont(), c, st.text, r.text);
        for (size_t k = 0; k < r.sub.size() && k < st.sub.size() && r.sub[k].bottom <= limit; ++k) {
            // "Reward..." lines in a muted gold, the rest in the comment colour
            bool reward = steps::lower(st.sub[k].substr(0, 6)) == "reward";
            COLORREF c = reward ? mix(mix(C::Yellow, C::Orange, 60), C::Darkest, 72) : C::Comment;
            draw(subFont(), st.optional ? mix(c, C::Darkest, 70) : c, st.sub[k], r.sub[k]);
        }
    }
    if (gSepY >= headBottom) {
        HPEN pen = CreatePen(PS_SOLID, 1, C::Line);
        HGDIOBJ op = SelectObject(dc, pen);
        MoveToEx(dc, kPad, gSepY, nullptr);
        LineTo(dc, rc.right - kPad, gSepY);
        SelectObject(dc, op);
        DeleteObject(pen);
    }
    for (size_t k = 0; k < gNotes.size() && k < gNoteRects.size(); ++k)
        if (gNoteRects[k].top >= gSepY + 2 && gSepY >= headBottom)
            draw(gNotes[k].sub ? subFont() : rowFont(), gNotes[k].sub ? C::Comment : C::Fg, gNotes[k].text, gNoteRects[k]);
}

// The window is as tall as its rows, or as tall as dragged (top / bottom edge; "H" in its settings, 0 = fit the
// rows): rows that don't fit are left out.
int gFixedH = 0;

void fitHeight() {
    if (!hStepsOv) return;
    RECT rc;
    GetClientRect(hStepsOv, &rc);
    HDC dc = GetDC(hStepsOv);
    int h = layoutRows(dc, rc.right);
    ReleaseDC(hStepsOv, dc);
    if (gFixedH > 0) h = std::max(gFixedH, gMinH);
    if (h != rc.bottom) SetWindowPos(hStepsOv, nullptr, 0, 0, rc.right, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Double-click: the app on the Steps tab. With click-through on, Alt is held for the click; the app opens once it
// is let go (an Alt let go over the app would open its window menu).
void openSteps() {
    for (ULONGLONG t0 = GetTickCount64(); (GetAsyncKeyState(VK_MENU) & 0x8000) && GetTickCount64() - t0 < 5000;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_SYSKEYUP || msg.message == WM_KEYUP) continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(15);
    }
    showMain();
    showPage(PG_STEPS);
}

void savePos() {
    RECT wr;
    if (!hStepsOv || !GetWindowRect(hStepsOv, &wr)) return;
    WritePrivateProfileStringW(kSec, L"X", std::to_wstring(wr.left).c_str(), iniPath().c_str());
    WritePrivateProfileStringW(kSec, L"Y", std::to_wstring(wr.top).c_str(), iniPath().c_str());
    WritePrivateProfileStringW(kSec, L"W", std::to_wstring(wr.right - wr.left).c_str(), iniPath().c_str());
    WritePrivateProfileStringW(kSec, L"H", std::to_wstring(gFixedH).c_str(), iniPath().c_str());
    anchorSave(hStepsOv, kSec);
}

LRESULT CALLBACK StepsProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCHITTEST: {  // edges resize, the rest drags
            POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
            RECT wr;
            GetWindowRect(h, &wr);
            bool L = pt.x < wr.left + 6, R = pt.x >= wr.right - 6, T = pt.y < wr.top + 6, B = pt.y >= wr.bottom - 6;
            if (T && L) return HTTOPLEFT;
            if (T && R) return HTTOPRIGHT;
            if (B && L) return HTBOTTOMLEFT;
            if (B && R) return HTBOTTOMRIGHT;
            if (L) return HTLEFT;
            if (R) return HTRIGHT;
            if (T) return HTTOP;
            if (B) return HTBOTTOM;
            POINT c = pt;
            ScreenToClient(h, &c);
            return hitAt(c) >= 0 ? HTCLIENT : HTCAPTION;  // a line: hover / click copies; the rest drags
        }
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        case WM_MOUSEMOVE: {
            int i = hitAt({(short)LOWORD(lp), (short)HIWORD(lp)});
            if (i != gHover) {
                gHover = i;
                InvalidateRect(h, nullptr, FALSE);
            }
            TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, h, 0};
            TrackMouseEvent(&t);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (gHover >= 0) {
                gHover = -1;
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            copyLine(h, hitAt({(short)LOWORD(lp), (short)HIWORD(lp)}));
            return 0;
        case WM_TIMER:
            if (wp == kCopiedTimer) {
                KillTimer(h, kCopiedTimer);
                gCopied = -1;
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_SIZING:  // a top / bottom edge dragged: that height is kept
            if (wp != WMSZ_LEFT && wp != WMSZ_RIGHT) {
                RECT* r = (RECT*)lp;
                if (r->bottom - r->top < gMinH) {  // not over the header or the notes
                    if (wp == WMSZ_TOP || wp == WMSZ_TOPLEFT || wp == WMSZ_TOPRIGHT) r->top = r->bottom - gMinH;
                    else r->bottom = r->top + gMinH;
                }
                gFixedH = r->bottom - r->top;
            }
            return TRUE;
        case WM_NCCALCSIZE:
            if (wp) return 0;
            break;
        case WM_NCLBUTTONDBLCLK:
            openSteps();
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
            fitHeight();
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
            ((MINMAXINFO*)lp)->ptMinTrackSize = {180, 40};
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

bool stepsOverlayFixedHeight() { return gFixedH > 0; }
void stepsOverlayFitHeight() {
    gFixedH = 0;
    fitHeight();
    savePos();
}

void stepsOverlayRefresh() {
    if (!hStepsOv) return;
    fitHeight();
    InvalidateRect(hStepsOv, nullptr, FALSE);
}

void setStepsShown(int n) {
    gStepsShown = std::clamp(n, 1, 10);
    WritePrivateProfileStringW(kSec, L"Shown", std::to_wstring(gStepsShown).c_str(), iniPath().c_str());
    if (hStepsVal) SetWindowTextW(hStepsVal, std::to_wstring(gStepsShown).c_str());
    stepsOverlayRefresh();
}

void showStepsOverlay(bool on) {
    gStepsOn = on;
    if (on && !hStepsOv) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = StepsProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"StepsOverlay";
        wc.style = CS_DBLCLKS;
        RegisterClassExW(&wc);
        auto ini = [](const wchar_t* k, int d) { return (int)GetPrivateProfileIntW(kSec, k, d, iniPath().c_str()); };
        // first start: right of the timer overlay, else inside the game window on the left
        RECT pos = {40, 200, 400, 300}, o, gr;
        if (hOverlay && IsWindowVisible(hOverlay) && GetWindowRect(hOverlay, &o)) {
            pos = {o.right + 8, o.top, o.right + 368, o.top + 100};
        } else if (gameClientRect(gr)) {
            pos = {gr.left + 40, gr.top + 200, gr.left + 400, gr.top + 300};
        }
        bool saved = ini(L"X", -100000) != -100000;
        int x = saved ? ini(L"X", 40) : pos.left, y = saved ? ini(L"Y", 200) : pos.top;
        int w = saved ? ini(L"W", 360) : pos.right - pos.left;
        gFixedH = std::max(0, ini(L"H", 0));
        hStepsOv = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
                                   wc.lpszClassName, L"StepsOverlay", WS_POPUP | WS_THICKFRAME, x, y, w, 100, nullptr,
                                   nullptr, wc.hInstance, nullptr);
        SetLayeredWindowAttributes(hStepsOv, 0, overlayAlpha(), LWA_ALPHA);
        hideFromCapture(hStepsOv);
        fitHeight();
        if (!saved) savePos();
    }
    stepsOverlayRefresh();
    updateOverlayVisibility();
    if (hStepsChk) InvalidateRect(hStepsChk, nullptr, FALSE);
}
