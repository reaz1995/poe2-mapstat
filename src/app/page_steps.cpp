// Steps tab: leveling steps as YAML-like text (src/core/steps), zone names typed at the start of a line are completed
// from the known zones. Presets: the built-in Guide, then one file each in %APPDATA%\Poe2MapStat\steps\<name>.yaml
// (what is done in steps\progress\<name>.txt), listed on the left; the chosen one is in the editor and on the
// StepsOverlay (overlay_steps.cpp), whose hotkeys mark steps done. The editor folds "+ Header" sections, has its own
// thin scroll bar and saves by itself a second after the last change.
#include "app.h"

#include <algorithm>
#include <functional>
#include <set>

HWND hTabSteps = nullptr, hNotesChk = nullptr;
bool gStepsNotes = true;

namespace {

HWND hEdit, hAc, hReset, hInfo, hList, hImport, hSave, hSaveAs;
HFONT fMono = nullptr;

std::vector<steps::Step> gSteps;
steps::Doc gDoc;
steps::Progress gProg;
bool gSwallowChar = false;  // the Enter / Tab that accepted a suggestion
size_t gAcFrom = 0, gAcTo = 0;  // text replaced by the suggestion

std::wstring stepsDir() { return gDataDir + L"\\steps"; }
std::wstring presetPath(const std::wstring& n) { return stepsDir() + L"\\" + n + L".yaml"; }
std::wstring progressPath(const std::wstring& n) { return stepsDir() + L"\\progress\\" + n + L".txt"; }

std::vector<std::wstring> gPresets;  // names (file names without .yaml), sorted
std::wstring gCur;  // the preset in the editor
bool gDirty = false, gLoading = false;
const int kSep = -1, kNew = -2;  // list items after the presets: a line, "New..."

std::wstring donePath() { return progressPath(gCur); }

bool writeUtf8(const std::wstring& path, const std::string& s) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    bool ok = WriteFile(f, s.data(), (DWORD)s.size(), &w, nullptr) && w == s.size();
    CloseHandle(f);
    return ok;
}

// zone names in campaign order (then other areas), with their lowercase form
struct ZoneName {
    std::wstring low, name;
};
const std::vector<ZoneName>& zoneList() {
    static std::vector<ZoneName> v;
    static size_t known = (size_t)-1;
    if (known != g.areaNames.size()) {
        known = g.areaNames.size();
        v.clear();
        for (auto& z : knownZoneNames()) v.push_back({lowerW(z), z});
    }
    return v;
}

// ---- folding: a "+ Header" line and the next "+" line make a section; folded, the editor shows the whole pair
// as one line "----- Header" (double-click either to fold / unfold). The file always has the full text.
const std::wstring kFold = L"----- ";
std::set<std::wstring> gFolded;  // headers folded in this preset (settings: [StepsFold] <preset>=a|b)
std::vector<std::vector<std::wstring>> gHidden;  // the lines of each folded section, in editor order

std::vector<std::wstring> splitLines(const std::wstring& t) {
    std::vector<std::wstring> v(1);
    for (wchar_t c : t) {
        if (c == L'\r') continue;
        if (c == L'\n') v.emplace_back();
        else v.back() += c;
    }
    return v;
}
std::wstring joinLines(const std::vector<std::wstring>& v, const wchar_t* nl) {
    std::wstring r;
    for (size_t i = 0; i < v.size(); ++i) r += (i ? nl : L"") + v[i];
    return r;
}
bool isPlus(const std::wstring& l) { return !l.empty() && l[0] == L'+'; }
std::wstring plusHeader(const std::wstring& l) {
    size_t a = l.find_first_not_of(L" \t", 1), b = l.find_last_not_of(L" \t\r");
    return a == std::wstring::npos ? L"" : l.substr(a, b - a + 1);
}
std::wstring placeholder(const std::vector<std::wstring>& block) { return kFold + plusHeader(block[0]); }

// the full text: the editor's, each placeholder replaced by its section (\n line ends)
std::wstring fullText() {
    std::vector<std::wstring> out;
    size_t k = 0;
    for (const std::wstring& l : splitLines(getText(hEdit))) {
        size_t j = k;
        while (j < gHidden.size() && l != placeholder(gHidden[j])) ++j;  // a placeholder deleted: its section too
        if (j < gHidden.size()) {
            out.insert(out.end(), gHidden[j].begin(), gHidden[j].end());
            k = j + 1;
        } else {
            out.push_back(l);
        }
    }
    return joinLines(out, L"\n");
}

void saveFolds() {
    std::wstring v;
    for (auto& f : gFolded) v += (v.empty() ? L"" : L"|") + f;
    if (!gCur.empty())
        WritePrivateProfileStringW(L"StepsFold", gCur.c_str(), v.empty() ? nullptr : v.c_str(), iniPath().c_str());
}
void loadFolds() {
    gFolded.clear();
    wchar_t b[4096] = L"";
    GetPrivateProfileStringW(L"StepsFold", gCur.c_str(), L"", b, 4096, iniPath().c_str());
    std::wstring v = b;
    for (size_t a = 0; a <= v.size();) {
        size_t e = v.find(L'|', a);
        if (e == std::wstring::npos) e = v.size();
        if (e > a) gFolded.insert(v.substr(a, e - a));
        a = e + 1;
    }
}

// shows the full text with the folded sections as placeholders; keeps the scroll position and puts the caret on line
void showFolded(const std::wstring& full, int caretLine = -1) {
    gHidden.clear();
    std::vector<std::wstring> lines = splitLines(full), view;
    bool open = false;  // the "+" lines pair up: opener, closer
    for (size_t i = 0; i < lines.size(); ++i) {
        if (!isPlus(lines[i])) {
            view.push_back(lines[i]);
            continue;
        }
        if (!open && gFolded.count(plusHeader(lines[i]))) {
            size_t c = i + 1;
            while (c < lines.size() && !isPlus(lines[c])) ++c;
            if (c < lines.size()) {  // a closing "+": fold
                gHidden.emplace_back(lines.begin() + i, lines.begin() + c + 1);
                view.push_back(placeholder(gHidden.back()));
                i = c;
                continue;
            }
        }
        open = !open;
        view.push_back(lines[i]);
    }
    int top = (int)SendMessageW(hEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
    bool was = gLoading;
    gLoading = true;
    SetWindowTextW(hEdit, joinLines(view, L"\r\n").c_str());
    gLoading = was;
    SendMessageW(hEdit, EM_LINESCROLL, 0, top);
    if (caretLine >= 0) {
        int c = (int)SendMessageW(hEdit, EM_LINEINDEX, caretLine, 0);
        if (c >= 0) SendMessageW(hEdit, EM_SETSEL, c, c);
    }
}

std::wstring editLine(int line) {
    int start = (int)SendMessageW(hEdit, EM_LINEINDEX, line, 0);
    if (start < 0) return L"";
    int len = (int)SendMessageW(hEdit, EM_LINELENGTH, start, 0);
    std::wstring t(len + 2, L'\0');
    *(WORD*)t.data() = (WORD)(len + 1);
    t.resize((size_t)SendMessageW(hEdit, EM_GETLINE, line, (LPARAM)t.data()));
    return t;
}
bool isPlaceholderLine(int line) {
    if (line < 0 || line >= (int)SendMessageW(hEdit, EM_GETLINECOUNT, 0, 0)) return false;
    std::wstring t = editLine(line);
    for (auto& b : gHidden)
        if (t == placeholder(b)) return true;
    return false;
}

// double-click on a "+ Header" opener or a placeholder: fold / unfold; true when it was one
bool toggleFold(int line) {
    std::wstring t = editLine(line);
    if (isPlaceholderLine(line)) {
        gFolded.erase(t.substr(kFold.size()));
    } else if (isPlus(t)) {
        int plus = 0;  // "+" lines above: an even count makes this one an opener
        for (int i = 0; i < line; ++i) plus += isPlus(editLine(i));
        if (plus % 2) return false;
        gFolded.insert(plusHeader(t));
    } else {
        return false;
    }
    saveFolds();
    showFolded(fullText(), line);
    return true;
}

// a key that would change a placeholder line (it can only be deleted whole, or unfolded first)
bool editsPlaceholder(UINT msg, WPARAM wp) {
    DWORD a = 0, b = 0;
    SendMessageW(hEdit, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
    int la = (int)SendMessageW(hEdit, EM_LINEFROMCHAR, a, 0), lb = (int)SendMessageW(hEdit, EM_LINEFROMCHAR, b, 0);
    auto start = [](int l) { return (DWORD)SendMessageW(hEdit, EM_LINEINDEX, l, 0); };
    auto end = [&](int l) { return start(l) + (DWORD)editLine(l).size(); };
    if (a != b) {  // a selection: placeholders in it only whole
        for (int l = la; l <= lb; ++l)
            if (isPlaceholderLine(l) && (a > start(l) || b < end(l))) return true;
        return false;
    }
    bool atStart = a == start(la), atEnd = a == end(la);
    if (msg == WM_KEYDOWN && wp == VK_DELETE)
        return (isPlaceholderLine(la) && !(atStart && atEnd)) || (atEnd && isPlaceholderLine(la + 1));
    if (wp == L'\b') return (isPlaceholderLine(la)) || (atStart && isPlaceholderLine(la - 1));
    if (wp == L'\r') return isPlaceholderLine(la) && !atStart && !atEnd;
    return isPlaceholderLine(la);
}

void reparse() {
    gDoc = steps::parseDoc(U8(fullText()));
    gSteps = gDoc.steps;
    stepsChanged();
}

void saveDone() {
    CreateDirectoryW((stepsDir() + L"\\progress").c_str(), nullptr);
    writeUtf8(donePath(), gProg.save());
}

// ---- suggestions

void hideAc() {
    if (hAc && IsWindowVisible(hAc)) ShowWindow(hAc, SW_HIDE);
}

// The line the caret is at the end of, when it is a zone line being typed: suggestions for it.
void updateAc() {
    DWORD a = 0, b = 0;
    SendMessageW(hEdit, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
    int line = (int)SendMessageW(hEdit, EM_LINEFROMCHAR, b, 0);
    int start = (int)SendMessageW(hEdit, EM_LINEINDEX, line, 0);
    int len = (int)SendMessageW(hEdit, EM_LINELENGTH, start, 0);
    std::wstring t(len + 2, L'\0');
    *(WORD*)t.data() = (WORD)(len + 1);
    t.resize((size_t)SendMessageW(hEdit, EM_GETLINE, line, (LPARAM)t.data()));
    if (a != b || (int)b != start + len || t.size() < 2 || t[0] == L' ' || t[0] == L'\t' || t[0] == L'#') {
        hideAc();
        return;
    }
    std::wstring typed = lowerW(t);
    std::vector<std::wstring> first, other;  // names starting with it (also after "The "), then containing it
    for (auto& z : zoneList()) {
        if (z.low == typed) {  // already complete
            hideAc();
            return;
        }
        size_t at = z.low.find(typed);
        if (at == 0 || (at == 4 && z.low.rfind(L"the ", 0) == 0)) first.push_back(z.name);
        else if (at != std::wstring::npos) other.push_back(z.name);
    }
    first.insert(first.end(), other.begin(), other.end());
    if (first.empty()) {
        hideAc();
        return;
    }
    if (first.size() > 8) first.resize(8);
    gAcFrom = (size_t)start, gAcTo = b;
    SendMessageW(hAc, WM_SETREDRAW, FALSE, 0);
    SendMessageW(hAc, LB_RESETCONTENT, 0, 0);
    for (auto& n : first) SendMessageW(hAc, LB_ADDSTRING, 0, (LPARAM)n.c_str());
    SendMessageW(hAc, LB_SETCURSEL, 0, 0);
    SendMessageW(hAc, WM_SETREDRAW, TRUE, 0);
    // a popup under the line (screen coordinates), above everything; the editor keeps the focus
    LRESULT pos = SendMessageW(hEdit, EM_POSFROMCHAR, start, 0);
    POINT pt = {(short)LOWORD(pos), (short)HIWORD(pos)};
    ClientToScreen(hEdit, &pt);
    int ih = (int)SendMessageW(hAc, LB_GETITEMHEIGHT, 0, 0);
    SetWindowPos(hAc, HWND_TOPMOST, pt.x, pt.y + ih + 6, 340, (int)first.size() * ih + 4,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

void acceptAc() {
    int i = (int)SendMessageW(hAc, LB_GETCURSEL, 0, 0);
    if (i < 0) return;
    wchar_t b[256] = L"";
    SendMessageW(hAc, LB_GETTEXT, i, (LPARAM)b);
    hideAc();
    SendMessageW(hEdit, EM_SETSEL, gAcFrom, gAcTo);
    SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)b);
    SetFocus(hEdit);
}

// current line (to the caret) and whether it is a zone line
std::wstring lineBeforeCaret() {
    DWORD a = 0, b = 0;
    SendMessageW(hEdit, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
    int line = (int)SendMessageW(hEdit, EM_LINEFROMCHAR, a, 0);
    int start = (int)SendMessageW(hEdit, EM_LINEINDEX, line, 0);
    int len = (int)SendMessageW(hEdit, EM_LINELENGTH, start, 0);
    std::wstring t(len + 2, L'\0');
    *(WORD*)t.data() = (WORD)(len + 1);
    t.resize((size_t)SendMessageW(hEdit, EM_GETLINE, line, (LPARAM)t.data()));
    return t.substr(0, std::min(t.size(), (size_t)(a - start)));
}

// The suggestion popup: a click takes the name; it never takes the focus from the editor.
LRESULT CALLBACK AcProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_LBUTTONDOWN: {
            LRESULT r = SendMessageW(h, LB_ITEMFROMPOINT, 0, lp);
            if (!HIWORD(r)) {
                SendMessageW(h, LB_SETCURSEL, LOWORD(r), 0);
                acceptAc();
            }
            return 0;
        }
        case WM_MOUSEMOVE: {  // the item under the mouse is the one Enter takes
            LRESULT r = SendMessageW(h, LB_ITEMFROMPOINT, 0, lp);
            if (!HIWORD(r) && (int)SendMessageW(h, LB_GETCURSEL, 0, 0) != LOWORD(r))
                SendMessageW(h, LB_SETCURSEL, LOWORD(r), 0);
            return 0;
        }
    }
    return DefSubclassProc(h, msg, wp, lp);
}

bool savePreset();
const UINT_PTR kAutoSave = 42;  // the preset is saved 1 s after the last change

// ---- the editor's scroll bar: thin, in the theme's colours (the edit control has none of its own)
HWND hScroll = nullptr;
bool gSbDrag = false, gSbHover = false;
int gSbGrabY = 0, gSbGrabFirst = 0, gSbShown = -1;

void scrollMetrics(int& first, int& count, int& page) {
    first = (int)SendMessageW(hEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
    count = (int)SendMessageW(hEdit, EM_GETLINECOUNT, 0, 0);
    static int lineH = 0;
    if (!lineH) {
        HDC dc = GetDC(hEdit);
        HGDIOBJ o = SelectObject(dc, fMono);
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, o);
        ReleaseDC(hEdit, dc);
        lineH = std::max(1, (int)tm.tmHeight);
    }
    RECT rc;
    GetClientRect(hEdit, &rc);
    page = std::max(1, (int)rc.bottom / lineH);
}

RECT thumbRect() {
    RECT rc;
    GetClientRect(hScroll, &rc);
    int first, count, page;
    scrollMetrics(first, count, page);
    if (count <= page) return {0, 0, 0, 0};  // all of it fits: no thumb
    int th = std::max(28, (int)(rc.bottom * (int64_t)page / count)), maxFirst = count - page;
    int y = (int)((rc.bottom - th) * (int64_t)std::min(first, maxFirst) / maxFirst);
    return {1, y, rc.right - 1, y + th};
}

void syncScroll() {  // after anything that may have scrolled the editor
    if (!hScroll) return;
    int first, count, page;
    scrollMetrics(first, count, page);
    int key = first * 7919 + count * 31 + page;
    if (key != gSbShown) {
        gSbShown = key;
        InvalidateRect(hScroll, nullptr, FALSE);
        UpdateWindow(hScroll);  // now: while dragging, paints would wait until the mouse is let go
    }
}

LRESULT CALLBACK ScrollProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            FillRect(dc, &rc, brCard);
            RECT t = thumbRect();
            if (t.bottom > t.top) fillRound(dc, t, 6, gSbDrag || gSbHover ? C::Comment : C::Line, C::Card);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int y = (short)HIWORD(lp);
            RECT t = thumbRect();
            if (t.bottom <= t.top) return 0;
            if (y >= t.top && y < t.bottom) {  // drag the thumb
                int count, page;
                scrollMetrics(gSbGrabFirst, count, page);
                gSbGrabY = y;
                gSbDrag = true;
                SetCapture(h);
            } else {  // a page up / down
                SendMessageW(hEdit, EM_SCROLL, y < t.top ? SB_PAGEUP : SB_PAGEDOWN, 0);
            }
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!gSbHover) {
                gSbHover = true;
                TRACKMOUSEEVENT tme = {sizeof tme, TME_LEAVE, h, 0};
                TrackMouseEvent(&tme);
                InvalidateRect(h, nullptr, FALSE);
            }
            if (!gSbDrag) return 0;
            RECT rc, t = thumbRect();
            GetClientRect(h, &rc);
            int first, count, page;
            scrollMetrics(first, count, page);
            int track = rc.bottom - (t.bottom - t.top), maxFirst = count - page;
            if (track <= 0 || maxFirst <= 0) return 0;
            int want = gSbGrabFirst + (int)(((short)HIWORD(lp) - gSbGrabY) * (int64_t)maxFirst / track);
            want = std::clamp(want, 0, maxFirst);
            if (want != first) {
                SendMessageW(hEdit, EM_LINESCROLL, 0, want - first);
                UpdateWindow(hEdit);
                syncScroll();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            gSbHover = false;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
            if (gSbDrag) {
                gSbDrag = false;
                if (msg == WM_LBUTTONUP) ReleaseCapture();
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEWHEEL:
            return SendMessageW(hEdit, msg, wp, lp);
    }
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT CALLBACK EditProcInner(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR);

// Every message of the editor goes by here: the scroll bar follows; the wheel scrolls (no native scroll bar).
LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    if (msg == WM_MOUSEWHEEL) {
        static int acc = 0;
        acc += GET_WHEEL_DELTA_WPARAM(wp);
        int lines = acc / WHEEL_DELTA * 3;
        acc %= WHEEL_DELTA;
        if (lines) SendMessageW(h, EM_LINESCROLL, 0, -lines);
        hideAc();
        return 0;
    }
    LRESULT r = EditProcInner(h, msg, wp, lp, id, data);
    static bool syncing = false;  // syncScroll asks the editor itself (EM_GET...): those come back here
    if (!syncing && msg != WM_PAINT && msg != WM_NCPAINT && msg != WM_ERASEBKGND && msg != WM_GETDLGCODE &&
        msg != WM_GETTEXT && msg != WM_GETTEXTLENGTH && (msg < EM_GETSEL || msg > EM_GETIMESTATUS)) {
        syncing = true;
        syncScroll();
        syncing = false;
    }
    return r;
}

bool guideOpen();

LRESULT CALLBACK EditProcInner(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (guideOpen()) {  // the Guide is read only: moving, selecting, copying and folding still work
        bool typing = (msg == WM_CHAR && wp != 3 && wp != 1) || msg == WM_PASTE || msg == WM_CUT || msg == WM_CLEAR ||
                      msg == WM_UNDO || msg == EM_UNDO || (msg == WM_KEYDOWN && wp == VK_DELETE);
        if (typing) {
            MessageBeep(0);
            return 0;
        }
    }
    bool ac = hAc && IsWindowVisible(hAc);
    switch (msg) {
        case WM_TIMER:
            if (wp == kAutoSave) {
                KillTimer(h, kAutoSave);
                if (gDirty) savePreset();
                return 0;
            }
            break;
        case WM_GETDLGCODE:  // Tab and Enter belong to the editor, not to the dialog navigation
            return DefSubclassProc(h, msg, wp, lp) | DLGC_WANTALLKEYS;
        case WM_LBUTTONDBLCLK: {
            LRESULT r = SendMessageW(h, EM_CHARFROMPOS, 0, lp);
            if (toggleFold(HIWORD(r))) return 0;
            break;
        }
        case WM_CUT:
        case WM_CLEAR:
            if (editsPlaceholder(WM_CHAR, L'x')) {
                MessageBeep(0);
                return 0;
            }
            break;
        case WM_KEYDOWN:
            if (wp == VK_DELETE && editsPlaceholder(msg, wp)) {
                MessageBeep(0);
                return 0;
            }
            if (ac && (wp == VK_DOWN || wp == VK_UP)) {
                int n = (int)SendMessageW(hAc, LB_GETCOUNT, 0, 0), i = (int)SendMessageW(hAc, LB_GETCURSEL, 0, 0);
                SendMessageW(hAc, LB_SETCURSEL, ((i + (wp == VK_DOWN ? 1 : -1)) % n + n) % n, 0);
                return 0;
            }
            if (ac && (wp == VK_RETURN || wp == VK_TAB)) {
                acceptAc();
                gSwallowChar = true;
                return 0;
            }
            if (ac && wp == VK_ESCAPE) {
                hideAc();
                gSwallowChar = true;
                return 0;
            }
            break;
        case WM_CHAR:
            if (gSwallowChar) {
                gSwallowChar = false;
                return 0;
            }
            if ((wp >= L' ' || wp == L'\b' || wp == L'\r' || wp == L'\t') && editsPlaceholder(msg, wp)) {
                MessageBeep(0);  // a folded section: double-click it to unfold first
                return 0;
            }
            if (wp == L'\t') {  // a tab is 2 spaces
                SendMessageW(h, EM_REPLACESEL, TRUE, (LPARAM)L"  ");
                return 0;
            }
            if (wp == L'\r') {  // new line: steps under a zone stay indented
                std::wstring line = lineBeforeCaret(), indent;
                size_t n = line.find_first_not_of(L" \t");
                if (n == std::wstring::npos) n = line.size();
                indent = line.substr(0, n);
                std::wstring t = line.substr(n);
                while (!t.empty() && t.back() == L' ') t.pop_back();
                if (!t.empty() && t.back() == L':') t.pop_back();
                if (n == 0 && !t.empty() && t[0] != L'+' && t.rfind(kFold, 0) != 0) indent = L"  ";  // a zone, or Notes
                else if (lowerW(t) == L"notes") indent += L"  ";  // a zone's notes
                SendMessageW(h, EM_REPLACESEL, TRUE, (LPARAM)(L"\r\n" + indent).c_str());
                return 0;
            }
            break;
        case WM_PASTE: {  // text with Unix line ends (\n) would show as one line: paste it with \r\n
            if (!OpenClipboard(h)) break;
            std::wstring t;
            if (HANDLE d = GetClipboardData(CF_UNICODETEXT))
                if (auto* p = (const wchar_t*)GlobalLock(d)) {
                    for (; *p; ++p) {
                        if (*p == L'\r') continue;
                        if (*p == L'\n') t += L'\r';
                        t += *p == L'\t' ? std::wstring(L"  ") : std::wstring(1, *p);
                    }
                    GlobalUnlock(d);
                }
            CloseClipboard();
            hideAc();
            if (editsPlaceholder(WM_CHAR, L'x')) {
                MessageBeep(0);
                return 0;
            }
            SendMessageW(h, EM_REPLACESEL, TRUE, (LPARAM)t.c_str());
            return 0;
        }
        case WM_KILLFOCUS:
            hideAc();
            break;
        case WM_VSCROLL:
        case WM_MOUSEWHEEL:
            hideAc();
            break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

}  // namespace

// ---------------------------------------------------------------- data

const std::vector<steps::Step>& stepsAll() { return gSteps; }
steps::Progress& stepsProgress() { return gProg; }

std::wstring stepsZoneName() {
    if (g.location.empty() || Tracker::isHideout(g.location)) return L"";
    return W(g.locationDisplay());
}

// The zone before the one the player is in (any area: a town, a hideout), for "From -> Zone" steps.
static std::string gPrevZone, gZoneNow;  // name of the zone before; area id of the one now

std::vector<int> stepsCurrent(bool* zoneSteps) {
    return steps::forZone(gSteps, U8(stepsZoneName()), zoneSteps, gPrevZone);
}
std::vector<steps::Note> stepsNotes() { return steps::notesFor(gDoc, U8(stepsZoneName())); }

void setStepsNotes(bool on) {
    gStepsNotes = on;
    WritePrivateProfileStringW(L"StepsOverlay", L"Notes", on ? L"1" : L"0", iniPath().c_str());
    if (hNotesChk) InvalidateRect(hNotesChk, nullptr, FALSE);
    stepsOverlayRefresh();
}

void stepsAction(int hk) {
    auto list = stepsCurrent(nullptr);
    bool changed = hk == HK_STEP_DONE ? gProg.completeNext(gSteps, list) : gProg.undoLast(gSteps, list);
    if (!changed) return;
    saveDone();
    stepsChanged();
}

void stepsChanged() {
    stepsOverlayRefresh();
    if (!hInfo) return;
    int done = 0;
    for (auto& s : gSteps) done += gProg.isDone(s);
    bool zoneSteps = false;
    auto list = stepsCurrent(&zoneSteps);
    int here = 0;
    for (int i : list) here += gProg.isDone(gSteps[i]);
    std::wstring z = stepsZoneName();
    std::wstring t = gCur + L"  ·  " + std::to_wstring(gSteps.size()) + L" steps  ·  " + std::to_wstring(done) + L" done";
    if (!list.empty()) t += L"  ·  " + z + L": " + std::to_wstring(here) + L"/" + std::to_wstring(list.size());
    setTextIfChanged(hInfo, t);
}

// ---------------------------------------------------------------- page

// ---------------------------------------------------------------- presets

namespace {


std::wstring editorText() { return fullText(); }  // \n line ends, as saved, folded sections included

void setEditorText(const std::string& text) {
    std::wstring w;  // the edit control wants \r\n
    for (wchar_t c : W(text)) {
        if (c == L'\r') continue;
        if (c == L'\n') w += L'\r';
        w += c;
    }
    loadFolds();
    gHidden.clear();
    SendMessageW(hEdit, EM_LINESCROLL, 0, -(int)SendMessageW(hEdit, EM_GETFIRSTVISIBLELINE, 0, 0));
    gLoading = true;
    showFolded(w, 0);
    gLoading = false;
}

// The Guide preset: every rule of the steps text, with a small example. Made again when it is missing.
const wchar_t* kGuide = L"Guide";
bool isGuide(const std::wstring& name) { return lowerW(name) == lowerW(kGuide); }
const char* kGuideText = R"(# Guide - how steps are written. Built into the app: read only, Save As makes an editable copy.
#
# ZONES AND STEPS
#   A zone at the start of a line (a ':' after it is allowed), its steps under it by 2 spaces (Tab).
#   Zone names are suggested as you type: Up / Down picks one, Enter / Tab takes it, Esc closes the list.
#   "- " before a step is allowed. A zone can be listed more than once: its steps add up.
#   # starts a comment.
#
# DETAILS
#   4 spaces (2 Tabs) under a step: a detail of it (a boss, a reward, a hint), smaller on the overlay.
#   Details starting with "Reward" are shown in gold.
#
# OPTIONAL
#   (op) before a step: optional, shown a shade darker.
#
# COMING FROM
#   "From -> Zone": steps for the zone only when you came from that zone;
#   coming from anywhere else the plain zone steps are shown (none if there are none).
#
# NOTES
#   "Notes" under a zone: notes for that zone.
#   "Notes" at the start of a line: notes for every zone that has none of its own.
#   Notes are the overlay's footer (Options -> Overlays -> StepsOverlay -> Show notes).
#   A note indented deeper than the one above shows smaller.
#
# SECTIONS
#   "+ Header" ... "+": a section (an act, say). Double-click the "+ Header" line to fold it into
#   "----- Header", double-click that to unfold. Folded lines can't be typed on.
#
# ON THE OVERLAY
#   The next steps of the zone you are in, and the last one done (green).
#   Hotkeys (Options -> Hotkeys): Complete step marks the next one done, Undo step the last one.
#   Click a line (Alt + click with click-through on) to copy it, e.g. a regex for the game's search.
#
# Saved by itself 1 s after you stop typing.

+ Example
The Riverbank
  Kill the Bloated Miller
    Reward: Uncut Skill Gem

The Riverbank -> Clearfell Encampment
  First time in town: talk to Renly

Clearfell Encampment
  Back in town: sell, then go on

Clearfell
  Find the Mud Burrow
  (op) Kill Beira of the Rotten Pack
    Boss: Beira
    Reward: cold resistance
  Take the waypoint
  Notes
    Beira hits hard, bring a flask
      only the second phase is dangerous
+

Notes
  Check the vendors for movement speed boots
    in every town
)";

// The Guide is built in (no file): always listed first, its text from the app. A Guide.yaml an earlier version wrote
// is removed.
std::string presetText(const std::wstring& name) {
    return isGuide(name) ? std::string(kGuideText) : readFileUtf8(presetPath(name));
}

void scanPresets() {
    static bool cleaned = false;
    if (!cleaned) {
        cleaned = true;
        if (readFileUtf8(presetPath(kGuide)).rfind("# Guide - how steps are written", 0) == 0)
            DeleteFileW(presetPath(kGuide).c_str());
    }
    gPresets.clear();
    gPresets.push_back(kGuide);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((stepsDir() + L"\\*.yaml").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName;
            n = n.substr(0, n.size() - 5);
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !isGuide(n)) gPresets.push_back(n);
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    std::sort(gPresets.begin(), gPresets.end(), [](auto& a, auto& b) {
        bool ga = lowerW(a) == lowerW(kGuide), gb = lowerW(b) == lowerW(kGuide);  // the Guide always first
        return ga != gb ? ga : lowerW(a) < lowerW(b);
    });
}

void fillList() {
    SendMessageW(hList, WM_SETREDRAW, FALSE, 0);
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < gPresets.size(); ++i) {
        int at = (int)SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)gPresets[i].c_str());
        SendMessageW(hList, LB_SETITEMDATA, at, (LPARAM)i);
        if (i == 0 && isGuide(gPresets[0]) && gPresets.size() > 1) {  // a line under the Guide
            at = (int)SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)L"");
            SendMessageW(hList, LB_SETITEMDATA, at, (LPARAM)kSep);
        }
    }
    for (int extra : {kSep, kNew}) {
        int at = (int)SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)(extra == kNew ? L"New..." : L""));
        SendMessageW(hList, LB_SETITEMDATA, at, (LPARAM)extra);
    }
    for (int at = 0, n = (int)SendMessageW(hList, LB_GETCOUNT, 0, 0); at < n; ++at) {
        int d = (int)SendMessageW(hList, LB_GETITEMDATA, at, 0);
        if (d >= 0 && d < (int)gPresets.size() && gPresets[d] == gCur) SendMessageW(hList, LB_SETCURSEL, at, 0);
    }
    SendMessageW(hList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hList, nullptr, TRUE);
}

std::wstring uniqueName(const std::wstring& want) {
    std::wstring n = want;
    for (int i = 2; fileExists(presetPath(n)) || isGuide(n); ++i) n = want + L" (" + std::to_wstring(i) + L")";
    return n;
}

void loadPreset(const std::wstring& name) {
    gCur = name;
    WritePrivateProfileStringW(L"StepsOverlay", L"Preset", name.c_str(), iniPath().c_str());
    setEditorText(presetText(name));
    gProg.load(readFileUtf8(donePath()));
    gDirty = false;
    reparse();
    fillList();
}

bool guideOpen() { return isGuide(gCur); }

bool savePreset() {
    if (gCur.empty()) return false;
    if (isGuide(gCur)) return true;  // never written over
    CreateDirectoryW(stepsDir().c_str(), nullptr);
    if (!writeUtf8(presetPath(gCur), U8(editorText()))) {
        MessageBoxW(hMain, (L"Cannot save " + presetPath(gCur)).c_str(), L"Steps", MB_ICONERROR);
        return false;
    }
    gDirty = false;
    InvalidateRect(hList, nullptr, FALSE);
    return true;
}

// Unsaved changes: save them, drop them or stay. false = stay.
bool confirmLeave() {
    if (!gDirty) return true;
    int r = MessageBoxW(hMain, (L"Save the changes to \"" + gCur + L"\"?").c_str(), L"Steps",
                        MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return false;
    if (r == IDYES) return savePreset();
    gDirty = false;
    return true;
}

void newPreset() {
    if (!confirmLeave()) return;
    std::wstring n = uniqueName(L"New preset");
    CreateDirectoryW(stepsDir().c_str(), nullptr);
    writeUtf8(presetPath(n), "");
    scanPresets();
    loadPreset(n);
    SetFocus(hEdit);
}

// Save As: into the app's presets (it becomes the preset in the editor, progress goes along), or anywhere else to
// share it (the preset in the editor stays).
void saveAs() {
    std::wstring start = gCur.empty() ? L"My steps" : isGuide(gCur) ? uniqueName(L"My steps") : gCur;
    std::wstring to = pickSaveFile(hMain, L"Save the steps as", stepsDir(), start, L"yaml");
    if (to.empty()) return;
    std::wstring folder = to.substr(0, to.find_last_of(L"\\/")), file = to.substr(to.find_last_of(L"\\/") + 1);
    if (lowerW(folder) == lowerW(stepsDir()) && isGuide(file.substr(0, file.find_last_of(L'.')))) {
        MessageBoxW(hMain, L"The Guide can't be saved over: pick another name.", L"Steps", MB_ICONINFORMATION);
        return;
    }
    if (!writeUtf8(to, U8(editorText()))) {
        MessageBoxW(hMain, (L"Cannot save " + to).c_str(), L"Steps", MB_ICONERROR);
        return;
    }
    if (lowerW(folder) != lowerW(stepsDir())) return;  // exported
    std::wstring name = file.substr(0, file.find_last_of(L'.'));
    if (lowerW(file.substr(file.find_last_of(L'.') + 1)) != L"yaml") {  // the list only shows .yaml
        DeleteFileW(to.c_str());
        name = uniqueName(name);
        writeUtf8(presetPath(name), U8(editorText()));
    }
    if (name != gCur) {
        CreateDirectoryW((stepsDir() + L"\\progress").c_str(), nullptr);
        CopyFileW(donePath().c_str(), progressPath(name).c_str(), FALSE);
        gDirty = false;  // the old preset keeps its last saved text
    }
    scanPresets();
    loadPreset(name);
}

// Import: preset files, or every .yaml / .yml / .txt in a folder (and its subfolders), copied into the presets.
void importPresets() {
    enum { CMD_FILES = 1, CMD_FOLDER };
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, CMD_FILES, L"Preset files...");
    AppendMenuW(m, MF_STRING, CMD_FOLDER, L"A folder (every preset in it)...");
    RECT r;
    GetWindowRect(hImport, &r);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, r.left, r.bottom, 0, hMain, nullptr);
    DestroyMenu(m);
    std::vector<std::wstring> files;
    auto isPreset = [](const std::wstring& f) {
        std::wstring l = lowerW(f);
        for (const wchar_t* e : {L".yaml", L".yml", L".txt"})
            if (l.size() > wcslen(e) && l.compare(l.size() - wcslen(e), wcslen(e), e) == 0) return true;
        return false;
    };
    if (cmd == CMD_FILES) {
        files = pickFiles(hMain, L"Steps presets", L"Presets", L"*.yaml;*.yml;*.txt");
    } else if (cmd == CMD_FOLDER) {
        std::wstring root = pickFolder(hMain, L"Folder with steps presets");
        if (root.empty()) return;
        std::function<void(const std::wstring&)> walk = [&](const std::wstring& dir) {
            WIN32_FIND_DATAW fd;
            HANDLE f = FindFirstFileW((dir + L"\\*").c_str(), &fd);
            if (f == INVALID_HANDLE_VALUE) return;
            do {
                std::wstring n = fd.cFileName;
                if (n == L"." || n == L"..") continue;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) walk(dir + L"\\" + n);
                else if (isPreset(n)) files.push_back(dir + L"\\" + n);
            } while (FindNextFileW(f, &fd));
            FindClose(f);
        };
        walk(root);
    }
    if (files.empty() || !confirmLeave()) return;
    CreateDirectoryW(stepsDir().c_str(), nullptr);
    int added = 0, same = 0;
    std::wstring last;
    for (auto& f : files) {
        std::wstring file = f.substr(f.find_last_of(L"\\/") + 1), name = file.substr(0, file.find_last_of(L'.'));
        std::string text = readFileUtf8(f);
        if (fileExists(presetPath(name)) && readFileUtf8(presetPath(name)) == text) {
            ++same, last = name;
            continue;
        }
        name = uniqueName(name);
        if (writeUtf8(presetPath(name), text)) ++added, last = name;
    }
    scanPresets();
    if (!last.empty()) loadPreset(last);
    std::wstring msg = std::to_wstring(added) + L" preset(s) imported.";
    if (same) msg += L"\n" + std::to_wstring(same) + L" were there already.";
    MessageBoxW(hMain, msg.c_str(), L"Steps", MB_ICONINFORMATION);
}

void presetMenu(int idx, POINT pt) {  // right-click on a preset
    if (idx < 0 || idx >= (int)gPresets.size() || isGuide(gPresets[idx])) return;  // built in: nothing to do
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"Delete");
    AppendMenuW(m, MF_STRING, 2, L"Show in folder");
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hMain, nullptr);
    DestroyMenu(m);
    std::wstring n = gPresets[idx];
    if (cmd == 2) {
        ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + presetPath(n) + L"\"").c_str(), nullptr,
                      SW_SHOWNORMAL);
    } else if (cmd == 1) {
        if (MessageBoxW(hMain, (L"Delete the preset \"" + n + L"\"?").c_str(), L"Steps", MB_OKCANCEL | MB_ICONWARNING) !=
            IDOK)
            return;
        DeleteFileW(presetPath(n).c_str());
        DeleteFileW(progressPath(n).c_str());
        scanPresets();
        if (n == gCur) {
            gDirty = false;
            if (gPresets.empty()) newPreset();
            else loadPreset(gPresets[0]);
        } else
            fillList();
    }
}

LRESULT CALLBACK ListProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_CONTEXTMENU) {
        POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)}, c = pt;
        ScreenToClient(h, &c);
        LRESULT r = SendMessageW(h, LB_ITEMFROMPOINT, 0, MAKELPARAM(c.x, c.y));
        if (!HIWORD(r)) presetMenu((int)SendMessageW(h, LB_GETITEMDATA, LOWORD(r), 0), pt);
        return 0;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

}  // namespace

bool stepsDrawItem(DRAWITEMSTRUCT* d) {
    if (d->hwndItem != hList || d->itemID == (UINT)-1) return false;
    HDC dc = d->hDC;
    RECT r = d->rcItem;
    FillRect(dc, &r, brCard);
    int data = (int)d->itemData;
    SetBkMode(dc, TRANSPARENT);
    if (data == kSep) {
        RECT line = {r.left + 8, (r.top + r.bottom) / 2, r.right - 8, (r.top + r.bottom) / 2 + 1};
        HBRUSH b = CreateSolidBrush(C::Line);
        FillRect(dc, &line, b);
        DeleteObject(b);
        return true;
    }
    RECT t = {r.left + 12, r.top, r.right - 8, r.bottom};
    if (data == kNew) {
        SelectObject(dc, fBold);
        SetTextColor(dc, C::Green);
        DrawTextW(dc, L"+  New...", -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        return true;
    }
    bool cur = data >= 0 && data < (int)gPresets.size() && gPresets[data] == gCur;
    if (cur) {
        RECT p = {r.left + 4, r.top + 2, r.right - 4, r.bottom - 2};
        fillRound(dc, p, 10, C::Purple, C::Purple);
    }
    std::wstring n = data >= 0 && data < (int)gPresets.size() ? gPresets[data] : L"";
    if (cur && gDirty) n += L"  •";  // unsaved changes
    SelectObject(dc, cur ? fBold : fNormal);
    SetTextColor(dc, cur ? C::Darkest : C::Fg);
    DrawTextW(dc, n.c_str(), -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    return true;
}

void stepsSaveOnExit() {
    if (gDirty) savePreset();
}

void createStepsPage() {
    fMono = font(15, FW_NORMAL, L"Consolas");
    hList = mk(L"LISTBOX", L"", LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_VSCROLL |
                                    WS_TABSTOP,
               ID_STEPS_LIST, fNormal, PG_STEPS);
    SendMessageW(hList, LB_SETITEMHEIGHT, 0, 28);
    SetWindowSubclass(hList, ListProc, 9, 0);
    hEdit = mk(L"EDIT", L"",
               ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN | WS_TABSTOP,
               ID_STEPS_EDIT, fMono, PG_STEPS);
    SendMessageW(hEdit, EM_SETLIMITTEXT, 4 * 1024 * 1024, 0);
    SendMessageW(hEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));
    SetWindowSubclass(hEdit, EditProc, 7, 0);
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = ScrollProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"Poe2MapStatThinScroll";
        RegisterClassW(&wc);
        hScroll = mk(wc.lpszClassName, L"", 0, 3999, fNormal, PG_STEPS);  // sends no commands
    }
    hAc = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, L"LISTBOX", L"",
                          WS_POPUP | WS_BORDER | LBS_NOINTEGRALHEIGHT, 0, 0, 10, 10, hMain, nullptr,
                          GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hAc, WM_SETFONT, (WPARAM)fNormal, FALSE);
    SetWindowSubclass(hAc, AcProc, 8, 0);
    hImport = btn(L"Import...", ID_STEPS_IMPORT, PG_STEPS, Btn::Normal);
    hSave = btn(L"Save", ID_STEPS_SAVE, PG_STEPS, Btn::Accent);
    hSaveAs = btn(L"Save As...", ID_STEPS_SAVEAS, PG_STEPS, Btn::Normal);
    hReset = btn(L"Reset progress", ID_STEPS_RESET, PG_STEPS, Btn::Danger);
    gStepsNotes = GetPrivateProfileIntW(L"StepsOverlay", L"Notes", 1, iniPath().c_str()) != 0;
    hInfo = label(L"", ID_STEPS_INFO, fBold, PG_STEPS, false, SS_ENDELLIPSIS);

    // older versions: one list in steps.txt (+ steps_done.txt) -> the preset "My steps"
    CreateDirectoryW(stepsDir().c_str(), nullptr);
    scanPresets();
    std::wstring oldText = gDataDir + L"\\steps.txt", oldDone = gDataDir + L"\\steps_done.txt";
    if (gPresets.size() <= 1 && fileExists(oldText)) {  // only the Guide
        CreateDirectoryW((stepsDir() + L"\\progress").c_str(), nullptr);
        writeUtf8(presetPath(L"My steps"), readFileUtf8(oldText));
        if (fileExists(oldDone)) MoveFileW(oldDone.c_str(), progressPath(L"My steps").c_str());
        if (fileExists(oldText)) DeleteFileW(oldText.c_str());
    }
    scanPresets();
    wchar_t b[260] = L"";
    GetPrivateProfileStringW(L"StepsOverlay", L"Preset", L"", b, 260, iniPath().c_str());
    std::wstring want = b;
    if (std::find(gPresets.begin(), gPresets.end(), want) == gPresets.end()) want = gPresets.empty() ? L"" : gPresets[0];
    loadPreset(want);
}

void stepsHideSuggestions() { hideAc(); }

void layoutStepsPage(int L, int T, int R, int B) {
    const int CW = R - L, rh = 32, lw = 230;
    int y = T;
    int bottom = B - rh - 12;
    card(PG_STEPS, L, y, lw, bottom - y);
    place(hList, L + 6, y + 8, lw - 12, bottom - y - 16);
    card(PG_STEPS, L + lw + 10, y, CW - lw - 10, bottom - y);
    place(hEdit, L + lw + 18, y + 8, CW - lw - 26 - 14, bottom - y - 16);
    place(hScroll, R - 8 - 8, y + 8, 8, bottom - y - 16);  // thin scroll bar at the card's right edge
    gSbShown = -1;
    syncScroll();
    int x = L;
    for (auto [h, w] : {std::pair{hImport, 110}, {hSave, 90}, {hSaveAs, 110}, {hReset, 140}}) {
        place(h, x, B - rh, w, rh);
        x += w + 8;
    }
    place(hInfo, x + 8, B - rh + 7, R - x - 8, 20);
    hideAc();
}

void stepsCommand(int id, int code) {
    if (id == ID_STEPS_EDIT && code == EN_CHANGE) {
        reparse();
        if (gLoading) return;
        updateAc();
        if (!gDirty) {
            gDirty = true;
            InvalidateRect(hList, nullptr, FALSE);
        }
        SetTimer(hEdit, kAutoSave, 1000, nullptr);  // typing again restarts it
    } else if (id == ID_STEPS_LIST && code == LBN_SELCHANGE) {
        int i = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
        int data = i < 0 ? kSep : (int)SendMessageW(hList, LB_GETITEMDATA, i, 0);
        if (data == kNew) newPreset();
        else if (data >= 0 && data < (int)gPresets.size() && gPresets[data] != gCur && confirmLeave())
            loadPreset(gPresets[data]);
        else
            fillList();  // the line, or staying: the current preset stays selected
    } else if (id == ID_STEPS_IMPORT) {
        importPresets();
    } else if (id == ID_STEPS_SAVE) {
        savePreset();
    } else if (id == ID_STEPS_SAVEAS) {
        saveAs();
    } else if (id == ID_STEPS_RESET) {
        if (MessageBoxW(hMain, L"Mark all steps as not done?", L"Steps", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
        gProg = steps::Progress();
        saveDone();
        stepsChanged();
    }
}

void stepsTick() {  // the zone changed, or its loading screen gave its name
    static std::string last;
    std::string now = g.location + '\x1f' + g.locationDisplay();
    if (now != last) {
        last = now;
        if (!g.location.empty() && g.location != gZoneNow) {  // another area: the one left is "from"
            if (!gZoneNow.empty()) gPrevZone = g.nameOf(gZoneNow);
            gZoneNow = g.location;  // area id
        }
        stepsChanged();
    }
}
