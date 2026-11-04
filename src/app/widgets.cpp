// Owner-drawn buttons, check boxes, tab bar, popup menus and small painting helpers.
#include "app.h"

static void drawGear(HDC dc, RECT r, COLORREF col);

void place(HWND h, int x, int y, int w, int hgt) { MoveWindow(h, x, y, w, hgt, TRUE); }

RECT card(int page, int x, int y, int w, int h) {
    RECT r = {x, y, x + w, y + h};
    gCards[page].push_back(r);
    return r;
}

void fillRound(HDC dc, const RECT& r, int rad, COLORREF fill, COLORREF border) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad, rad);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

void paintMain(HDC dc, const RECT& client) {
    FillRect(dc, &client, brBg);
    RECT bar = {0, 0, client.right, kTabBarH};
    FillRect(dc, &bar, brDarkest);
    for (auto& r : gCards[gPage]) fillRound(dc, r, 12, C::Card, C::Line);
}

static void drawGear(HDC dc, RECT r, COLORREF col) {
    int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    HPEN pen = CreatePen(PS_SOLID, 3, col);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    for (int i = 0; i < 8; ++i) {  // teeth
        double a = i * 3.14159265 / 4;
        MoveToEx(dc, cx + (int)(6 * cos(a)), cy + (int)(6 * sin(a)), nullptr);
        LineTo(dc, cx + (int)(10 * cos(a)), cy + (int)(10 * sin(a)));
    }
    Ellipse(dc, cx - 7, cy - 7, cx + 8, cy + 8);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(pen);
    HBRUSH hole = CreateSolidBrush(C::Darkest);
    HGDIOBJ ob2 = SelectObject(dc, hole);
    HGDIOBJ op2 = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, cx - 3, cy - 3, cx + 4, cy + 4);
    SelectObject(dc, ob2);
    SelectObject(dc, op2);
    DeleteObject(hole);
}

void drawButton(const DRAWITEMSTRUCT* d) {
    HDC dc = d->hDC;
    RECT r = d->rcItem;
    Btn kind = gBtnKind.count(d->hwndItem) ? gBtnKind[d->hwndItem] : Btn::Normal;
    bool hover = gHover == d->hwndItem, down = d->itemState & ODS_SELECTED, off = d->itemState & ODS_DISABLED;
    wchar_t text[128];
    GetWindowTextW(d->hwndItem, text, 128);
    SetBkMode(dc, TRANSPARENT);

    if (kind == Btn::Tab || kind == Btn::Gear) {
        FillRect(dc, &r, brDarkest);
        int page = d->hwndItem == hTabGen   ? (gPage == PG_SUMMARY ? PG_SUMMARY : PG_GENERAL)
                   : d->hwndItem == hTabCmp ? PG_CAMPAIGN
                   : d->hwndItem == hTabExp ? PG_EXPERIENCE
                   : d->hwndItem == hTabLay ? PG_LAYOUTS
                   : d->hwndItem == hTabSteps ? PG_STEPS
                   : d->hwndItem == hGear   ? PG_OPTIONS
                                            : -1;
        bool active = page >= 0 && gPage == page;
        COLORREF col = active ? C::Fg : hover ? C::Purple : C::Comment;
        if (kind == Btn::Gear) {
            drawGear(dc, r, active ? C::Purple : col);
        } else {
            SelectObject(dc, fTab);
            SetTextColor(dc, col);
            DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        if (active) {
            RECT u = {r.left + 14, r.bottom - 3, r.right - 14, r.bottom};
            HBRUSH b = CreateSolidBrush(C::Purple);
            FillRect(dc, &u, b);
            DeleteObject(b);
        }
        return;
    }

    FillRect(dc, &r, brBg);
    if (kind == Btn::Pill && (d->hwndItem == hOptPill[0] || d->hwndItem == hOptPill[1] || d->hwndItem == hOptPill[2])) {
        bool sel = d->hwndItem == hOptPill[gOptPill];
        fillRound(dc, r, 16, sel ? C::Purple : C::Card, sel ? C::Purple : hover ? C::Purple : C::Line);
        SelectObject(dc, fBold);
        SetTextColor(dc, sel ? C::Darkest : C::Fg);
        DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    if (kind == Btn::Pill) {
        bool lvl = d->hwndItem == hLvlCmp || d->hwndItem == hLvlMaps;  // level ups list mode
        bool sub = lvl || d->hwndItem == hSubSession || d->hwndItem == hSubSummary;  // Maps sub-tabs, else act pills
        bool sel = lvl   ? gLvlMaps == (d->hwndItem == hLvlMaps)
                   : sub ? gPage == (d->hwndItem == hSubSession ? PG_GENERAL : PG_SUMMARY)
                         : W(gSelAct) == text;
        bool here = !sub && g.curZone >= 0 && W(Tracker::actOf(g.zones[g.curZone].areaId)) == text;
        fillRound(dc, r, 16, sel ? C::Purple : C::Card, sel ? C::Purple : hover ? C::Purple : C::Line);
        SelectObject(dc, fBold);
        SetTextColor(dc, sel ? C::Darkest : here ? C::Green : C::Fg);
        DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    if (kind == Btn::Check) {
        RECT box = {r.left, r.top + (r.bottom - r.top - 18) / 2, r.left + 18, 0};
        box.bottom = box.top + 18;
        bool on = d->hwndItem == hOverlayChk   ? gOverlayOn
                  : d->hwndItem == hCloseChk   ? gStartInTray
                  : d->hwndItem == hUpdChk     ? gAutoUpdate
                  : d->hwndItem == hCmdChk     ? gCmdOn
                  : d->hwndItem == hGraphChk   ? gGraphOn
                  : d->hwndItem == hGraphOnlyChk ? gGraphOnly
                  : d->hwndItem == hStepsChk   ? gStepsOn
                  : d->hwndItem == hClickChk   ? gClickThru
                  : d->hwndItem == hCaptureChk ? gHideInCapture
                  : d->hwndItem == hNotesChk   ? gStepsNotes
                  : d->hwndItem == hExpOvChk   ? gExpOnOverlay
                  : d->hwndItem == hLayCropChk ? gLayoutCrop
                                               : gOnTopOn;
        fillRound(dc, box, 6, on ? C::Purple : C::Card, on ? C::Purple : (hover ? C::Purple : C::Comment));
        if (on) {
            HPEN pen = CreatePen(PS_SOLID, 2, C::Darkest);
            HGDIOBJ op = SelectObject(dc, pen);
            MoveToEx(dc, box.left + 4, box.top + 9, nullptr);
            LineTo(dc, box.left + 8, box.top + 13);
            LineTo(dc, box.left + 14, box.top + 5);
            SelectObject(dc, op);
            DeleteObject(pen);
        }
        RECT t = r;
        t.left += 28;
        SelectObject(dc, fNormal);
        SetTextColor(dc, C::Fg);
        DrawTextW(dc, text, -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    COLORREF fill = off ? C::Card : down ? C::Darkest : hover ? C::Line : C::Card;
    COLORREF border = off ? C::Line : hover ? C::Purple : C::Line;
    COLORREF txt = C::Fg;
    if (kind == Btn::Danger) txt = C::Red;
    if (kind == Btn::Accent) txt = C::Green;
    if (off) txt = C::Comment;
    fillRound(dc, r, 10, fill, border);
    SelectObject(dc, fBold);
    SetTextColor(dc, txt);
    DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// hover tracking for owner-drawn buttons
LRESULT CALLBACK BtnProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_MOUSEMOVE && gHover != h) {
        HWND old = gHover;
        gHover = h;
        TRACKMOUSEEVENT t = {};
        t.cbSize = sizeof t;
        t.dwFlags = TME_LEAVE;
        t.hwndTrack = h;
        TrackMouseEvent(&t);
        InvalidateRect(h, nullptr, FALSE);
        if (old) InvalidateRect(old, nullptr, FALSE);
    } else if (msg == WM_MOUSELEAVE) {
        if (gHover == h) gHover = nullptr;
        InvalidateRect(h, nullptr, FALSE);
    } else if (msg == WM_ERASEBKGND) {
        return 1;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

void measureMenu(MEASUREITEMSTRUCT* m) {
    auto* it = (MenuItem*)m->itemData;
    if (it->separator) {
        m->itemHeight = 9;
        m->itemWidth = 10;
        return;
    }
    HDC dc = GetDC(hMain);
    HGDIOBJ of = SelectObject(dc, fNormal);
    SIZE sz;
    GetTextExtentPoint32W(dc, it->text.c_str(), (int)it->text.size(), &sz);
    SelectObject(dc, of);
    ReleaseDC(hMain, dc);
    m->itemWidth = sz.cx + 36;
    m->itemHeight = 30;
}

void drawMenu(const DRAWITEMSTRUCT* d) {
    auto* it = (MenuItem*)d->itemData;
    RECT r = d->rcItem;
    FillRect(d->hDC, &r, brCard);
    if (it->separator) {
        RECT ln = {r.left + 8, (r.top + r.bottom) / 2, r.right - 8, (r.top + r.bottom) / 2 + 1};
        HBRUSH b = CreateSolidBrush(C::Line);
        FillRect(d->hDC, &ln, b);
        DeleteObject(b);
        return;
    }
    bool off = d->itemState & ODS_GRAYED, sel = (d->itemState & ODS_SELECTED) && !off;
    if (sel) {
        RECT hr = {r.left + 3, r.top + 2, r.right - 3, r.bottom - 2};
        fillRound(d->hDC, hr, 8, C::Line, C::Purple);
    }
    SetBkMode(d->hDC, TRANSPARENT);
    SelectObject(d->hDC, fNormal);
    COLORREF col = d->itemID == 1 || d->itemID == 31 ? C::Pink
                   : d->itemID == 2                  ? C::Green
                   : d->itemID == 12                 ? C::Orange
                   : d->itemID >= 20                 ? C::Purple
                                                     : C::Cyan;
    SetTextColor(d->hDC, off ? C::Comment : sel ? C::Fg : col);
    if (d->itemState & ODS_CHECKED) {  // active sort direction
        RECT dot = {r.left + 6, (r.top + r.bottom) / 2 - 3, r.left + 12, (r.top + r.bottom) / 2 + 3};
        fillRound(d->hDC, dot, 6, C::Purple, C::Purple);
    }
    r.left += 18;
    DrawTextW(d->hDC, it->text.c_str(), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

HWND mk(const wchar_t* cls, const wchar_t* txt, DWORD style, int id, HFONT f, int page, DWORD ex) {
    HWND h = CreateWindowExW(ex, cls, txt, WS_CHILD | style, 0, 0, 10, 10, hMain, (HMENU)(INT_PTR)id,
                             GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE);
    if (page >= 0) gPages[page].push_back(h);
    return h;
}

HWND btn(const wchar_t* txt, int id, int page, Btn kind) {
    HWND h = mk(L"BUTTON", txt, BS_OWNERDRAW | WS_TABSTOP | (page < 0 ? WS_VISIBLE : 0), id, fBold, page);
    gBtnKind[h] = kind;
    SetWindowSubclass(h, BtnProc, 1, 0);
    return h;
}

HWND label(const wchar_t* txt, int id, HFONT f, int page, bool onCard, DWORD extra) {
    HWND h = mk(L"STATIC", txt, SS_LEFT | SS_NOPREFIX | extra, id, f, page);
    if (onCard) gOnCard.push_back(h);
    return h;
}

COLORREF staticColor(HWND c) {
    if (c == hTimer) {
        if (!g.hasCurrent) return C::Comment;
        return g.cur.state == RunState::Paused ? C::Orange : C::Purple;
    }
    if (c == hCmpTimer) {
        if (g.cmpClock == Tracker::Clock::Stopped) return C::Red;
        if (g.cmpClock == Tracker::Clock::Paused) return C::Yellow;
        return g.curZone >= 0 ? C::Purple : C::Comment;
    }
    if (c == hCmpTimerLoad || c == hExpHint) return C::Comment;
    if (c == hStatus) {
        if (!g.hasCurrent) return C::Comment;
        if (g.cur.state == RunState::Active) return C::Green;
        if (g.cur.state == RunState::Paused) return C::Orange;
        return C::Cyan;
    }
    if (c == hCmpStatus) return g.curZone >= 0 ? C::Green : C::Comment;
    if (c == hLblHist || c == hLblSum || c == hLblCmp || c == hLblPath || c == hLblOverlay || c == hLblTheme ||
        c == hLblUpd || c == hLblKeys)
        return C::Pink;
    if (c == hDetLbl || c == hOptLbl || c == hUpdInfo) return C::Comment;
    if (c == hRecVal || c == hAlphaVal || c == hStepsVal) return C::Purple;
    if (c == hOvGeneralLbl || c == hOvEachLbl) return C::Comment;
    if (c == hSumHdr || c == hSummary || c == hCmpSum) return C::Cyan;
    return C::Fg;
}

// ---------------------------------------------------------------- slider

namespace {
struct SliderState {
    int lo, hi, v;
    bool hover;
};
SliderState* sliderOf(HWND h) { return (SliderState*)GetWindowLongPtrW(h, GWLP_USERDATA); }

const int kThumb = 8;  // thumb radius

void sliderPick(HWND h, int x) {
    SliderState* s = sliderOf(h);
    RECT rc;
    GetClientRect(h, &rc);
    int w = std::max(1, (int)rc.right - 2 * kThumb);
    int v = s->lo + (int)std::lround((double)(x - kThumb) * (s->hi - s->lo) / w);
    v = std::clamp(v, s->lo, s->hi);
    if (v == s->v) return;
    s->v = v;
    InvalidateRect(h, nullptr, FALSE);
    SendMessageW(hMain, WM_HSCROLL, SB_THUMBTRACK, (LPARAM)h);
}

LRESULT CALLBACK SliderProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    SliderState* s = sliderOf(h);
    switch (msg) {
        case WM_NCDESTROY:
            delete s;
            break;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS;
        case WM_LBUTTONDOWN:
            SetFocus(h);
            SetCapture(h);
            sliderPick(h, (short)LOWORD(lp));
            return 0;
        case WM_MOUSEMOVE:
            if (GetCapture() == h) sliderPick(h, (short)LOWORD(lp));
            if (!s->hover) {
                s->hover = true;
                InvalidateRect(h, nullptr, FALSE);
                TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, h, 0};
                TrackMouseEvent(&t);
            }
            return 0;
        case WM_MOUSELEAVE:
            s->hover = false;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
            if (GetCapture() == h) ReleaseCapture();
            return 0;
        case WM_MOUSEWHEEL:
        case WM_KEYDOWN: {
            int d = msg == WM_MOUSEWHEEL ? ((short)HIWORD(wp) > 0 ? 1 : -1)
                    : wp == VK_RIGHT || wp == VK_UP ? 1
                    : wp == VK_LEFT || wp == VK_DOWN ? -1
                                                     : 0;
            if (!d) break;
            int v = std::clamp(s->v + d, s->lo, s->hi);
            if (v != s->v) {
                s->v = v;
                InvalidateRect(h, nullptr, FALSE);
                SendMessageW(hMain, WM_HSCROLL, SB_THUMBPOSITION, (LPARAM)h);
            }
            return 0;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            InvalidateRect(h, nullptr, FALSE);
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            FillRect(dc, &rc, brBg);
            int cy = rc.bottom / 2, w = std::max(1, (int)rc.right - 2 * kThumb);
            int x = kThumb + (int)std::lround((double)(s->v - s->lo) * w / std::max(1, s->hi - s->lo));
            RECT track = {kThumb, cy - 2, rc.right - kThumb, cy + 2};
            fillRound(dc, track, 4, C::Line, C::Line);
            RECT done = {kThumb, cy - 2, x, cy + 2};
            if (x > kThumb) fillRound(dc, done, 4, C::Purple, C::Purple);
            RECT th = {x - kThumb, cy - kThumb, x + kThumb, cy + kThumb};
            bool hot = s->hover || GetCapture() == h || GetFocus() == h;
            fillRound(dc, th, kThumb * 2, hot ? C::Fg : C::Purple, C::Purple);
            EndPaint(h, &ps);
            return 0;
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}
}  // namespace

HWND slider(int id, int page, int lo, int hi) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = SliderProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_HAND);
        wc.lpszClassName = L"Poe2MapStatSlider";
        RegisterClassExW(&wc);
        reg = true;
    }
    HWND h = mk(L"Poe2MapStatSlider", L"", WS_TABSTOP, id, fNormal, page);
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR) new SliderState{lo, hi, lo, false});
    return h;
}

int sliderValue(HWND h) { return sliderOf(h) ? sliderOf(h)->v : 0; }

void setSliderValue(HWND h, int v) {
    if (SliderState* s = sliderOf(h)) {
        s->v = std::clamp(v, s->lo, s->hi);
        InvalidateRect(h, nullptr, FALSE);
    }
}
