// List views: rows, sorting, column menu (sort / hide), header search box, dark custom drawing.
#include "app.h"

static bool numKey(const std::wstring& s, double& v);
static void relayoutLists();
static int searchCol(HWND lv);
static LRESULT CALLBACK HeaderProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR lvp);
static LRESULT CALLBACK SearchEditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR);
static void placeSearchBox(HWND lv, HWND hd, HDC dc);
static LRESULT CALLBACK ListProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR);

// search box in the header of the name column (Maps, Summary, Campaign): filters rows by name or Area ID
static std::map<HWND, HWND> gSearchEdit;  // list -> edit

static std::map<HWND, std::wstring> gSearch;  // list -> lower-case text

bool searchMatch(HWND lv, const std::wstring& name, const std::wstring& id) {
    const std::wstring& q = gSearch[lv];
    return q.empty() || lowerW(name).find(q) != std::wstring::npos || lowerW(id).find(q) != std::wstring::npos;
}

std::map<HWND, SortState> gSort;

// Sort key: times (MM:SS, H:MM:SS) and numbers ("5.0s", "80-81", "1208479102") compare numerically.
static bool numKey(const std::wstring& s, double& v) {
    if (s.empty() || s == L"-") return false;
    bool colon = s.find(L':') != std::wstring::npos;
    if (colon) {
        double t = 0, part = 0;
        bool any = false;
        for (wchar_t ch : s) {
            if (ch >= L'0' && ch <= L'9') {
                part = part * 10 + (ch - L'0');
                any = true;
            } else if (ch == L':') {
                t = t * 60 + part;
                part = 0;
            } else return false;
        }
        v = t * 60 + part;
        return any;
    }
    wchar_t* end = nullptr;
    v = wcstod(s.c_str(), &end);
    if (end == s.c_str()) return false;
    return *end == 0 || *end == L's' || *end == L'-';
}

void sortRows(HWND lv, std::vector<LvRow>& rows) {
    SortState st = gSort[lv];
    if (st.col < 0) return;
    std::stable_sort(rows.begin(), rows.end(), [&](const LvRow& a, const LvRow& b) {
        const std::wstring& x = a.c[st.col];
        const std::wstring& y = b.c[st.col];
        bool ex = x.empty() || x == L"-", ey = y.empty() || y == L"-";
        if (ex != ey) return ey;  // empty values always last
        double vx, vy;
        bool nx = numKey(x, vx), ny = numKey(y, vy);
        int c;
        if (nx && ny) c = vx < vy ? -1 : vx > vy ? 1 : 0;
        else if (nx != ny) c = nx ? -1 : 1;
        else c = _wcsicmp(x.c_str(), y.c_str());
        return st.desc ? c > 0 : c < 0;
    });
}

// Writes rows into the list: keeps the item count in sync and only touches cells that changed.
void writeRows(HWND lv, const std::vector<LvRow>& rows) {
    int have = ListView_GetItemCount(lv);
    if (have != (int)rows.size()) SendMessageW(lv, WM_SETREDRAW, FALSE, 0);
    for (; have < (int)rows.size(); ++have) {
        LVITEMW it = {};
        it.mask = LVIF_TEXT;
        it.iItem = have;
        it.pszText = (LPWSTR)L"";
        ListView_InsertItem(lv, &it);
    }
    for (; have > (int)rows.size(); --have) ListView_DeleteItem(lv, have - 1);
    for (size_t r = 0; r < rows.size(); ++r)
        for (size_t c = 0; c < rows[r].c.size(); ++c) {
            wchar_t buf[256] = {0};
            ListView_GetItemText(lv, (int)r, (int)c, buf, 255);
            if (rows[r].c[c] != buf) ListView_SetItemText(lv, (int)r, (int)c, (LPWSTR)rows[r].c[c].c_str());
        }
    SendMessageW(lv, WM_SETREDRAW, TRUE, 0);
}

void fitColumns(HWND lv, const int* weights, int n) {
    RECT rc;
    GetClientRect(lv, &rc);
    int avail = rc.right - rc.left;
    if (!(GetWindowLongW(lv, GWL_STYLE) & WS_VSCROLL)) avail -= GetSystemMetrics(SM_CXVSCROLL);
    if (avail <= 0) return;
    unsigned hidden = gHiddenCols[lv];
    int sum = 0, last = -1;
    for (int i = 0; i < n; ++i)
        if (!((hidden >> i) & 1)) {
            sum += weights[i];
            last = i;
        }
    int used = 0;
    for (int i = 0; i < n; ++i) {
        if ((hidden >> i) & 1) {
            ListView_SetColumnWidth(lv, i, 0);
            continue;
        }
        int w = i == last ? avail - used : weights[i] * avail / std::max(sum, 1);
        ListView_SetColumnWidth(lv, i, w);
        used += w;
    }
}

// ---- column order (drag a header left / right), saved per list in [order]
static const UINT WM_SAVE_ORDER = WM_APP + 20;
void saveColumnOrder(HWND lv) {
    int n = Header_GetItemCount(ListView_GetHeader(lv));
    std::vector<int> o(n);
    if (!ListView_GetColumnOrderArray(lv, n, o.data())) return;
    std::wstring v;
    for (int i = 0; i < n; ++i) v += (i ? L"," : L"") + std::to_wstring(o[i]);
    WritePrivateProfileStringW(L"order", listKey(lv), v.c_str(), iniPath().c_str());
    InvalidateRect(ListView_GetHeader(lv), nullptr, TRUE);  // search box follows its column
}
void loadColumnOrder(HWND lv) {
    int n = Header_GetItemCount(ListView_GetHeader(lv));
    wchar_t buf[256];
    GetPrivateProfileStringW(L"order", listKey(lv), L"", buf, 256, iniPath().c_str());
    std::vector<int> o;
    std::vector<bool> seen(n, false);
    for (wchar_t* p = buf; *p;) {  // known columns in the saved order ...
        int c = (int)wcstol(p, &p, 10);
        if (c >= 0 && c < n && !seen[c]) o.push_back(c), seen[c] = true;
        while (*p == L',' || *p == L' ') ++p;
        if (!iswdigit(*p) && *p) break;
    }
    for (int c = 0; c < n; ++c)  // ... columns added in a newer version go to the end
        if (!seen[c]) o.push_back(c);
    ListView_SetColumnOrderArray(lv, n, o.data());
}
const wchar_t* listKey(HWND lv) {
    return lv == hHist         ? L"General"
           : lv == hSumList    ? L"Summary"
           : lv == hExpList    ? L"LevelUps"
           : lv == hCmpActList ? L"CampaignActs"
                               : L"Campaign";
}

static void relayoutLists() {
    fitColumns(hHist, kHistW, 9);
    fitColumns(hSumList, kSumW, 7);
    fitColumns(hCmpList, kCmpW, 11);
    fitColumns(hCmpActList, kCmpActW, 7);
    fitColumns(hExpList, kLvlW, 8);
}

// ---- search box inside the list header
static int searchCol(HWND lv) { return lv == hSumList ? 0 : 1; }  // Map / Zone column

static LRESULT CALLBACK HeaderProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR lvp) {
    HWND lv = (HWND)lvp;
    if (msg == WM_CTLCOLOREDIT) {
        SetTextColor((HDC)wp, C::Fg);
        SetBkColor((HDC)wp, C::Darkest);
        return (LRESULT)brDarkest;
    }
    if (msg == WM_COMMAND && HIWORD(wp) == EN_CHANGE) {
        wchar_t buf[128];
        GetWindowTextW((HWND)lp, buf, 128);
        gSearch[lv] = lowerW(buf);
        if (lv == hHist) gHistDirty = true;
        refreshUi();
        if (lv == hSumList) refreshSummary();
        if (lv == hCmpList) refreshCampaign(true);
        return 0;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

static LRESULT CALLBACK SearchEditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {  // Esc clears the search
        SetWindowTextW(h, L"");
        return 0;
    }
    if (msg == WM_GETDLGCODE) return DLGC_WANTALLKEYS;
    return DefSubclassProc(h, msg, wp, lp);
}

void addSearchBox(HWND lv) {
    HWND hd = ListView_GetHeader(lv);
    SetWindowLongPtrW(hd, GWL_STYLE, GetWindowLongPtrW(hd, GWL_STYLE) | WS_CLIPCHILDREN);
    HWND e = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 10, 10, hd, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    SendMessageW(e, WM_SETFONT, (WPARAM)fNormal, FALSE);
    SendMessageW(e, EM_SETCUEBANNER, TRUE, (LPARAM)L"search");
    SetWindowSubclass(hd, HeaderProc, 3, (DWORD_PTR)lv);
    SetWindowSubclass(e, SearchEditProc, 4, 0);
    gSearchEdit[lv] = e;
}

// keep the box right of the column title, inside the column (called after the header paints)
static void placeSearchBox(HWND lv, HWND hd, HDC dc) {
    auto it = gSearchEdit.find(lv);
    if (it == gSearchEdit.end()) return;
    int col = searchCol(lv);
    RECT r;
    Header_GetItemRect(hd, col, &r);
    wchar_t t[64] = L"";
    HDITEMW hi = {};
    hi.mask = HDI_TEXT;
    hi.pszText = t;
    hi.cchTextMax = 63;
    Header_GetItem(hd, col, &hi);
    SelectObject(dc, fBold);
    SIZE ts;
    GetTextExtentPoint32W(dc, t, (int)wcslen(t), &ts);
    int x = r.left + 6 + ts.cx + 10, w = r.right - 8 - x, hgt = r.bottom - r.top - 4;
    RECT cur;
    GetWindowRect(it->second, &cur);
    MapWindowPoints(nullptr, hd, (POINT*)&cur, 2);
    bool show = w >= 40;
    if (show && (cur.left != x || cur.right - cur.left != w || cur.top != r.top + 2))
        MoveWindow(it->second, x, r.top + 2, w, hgt, TRUE);
    if (show != (IsWindowVisible(it->second) != 0)) ShowWindow(it->second, show ? SW_SHOWNA : SW_HIDE);
}

static LRESULT CALLBACK ListProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_SAVE_ORDER) {
        if (h == hCmpRunList || h == hCmpCmpList) campaignRunsSaveOrder(h);  // by column name (acts come and go)
        else saveColumnOrder(h);
        return 0;
    }
    if (h == hSumList || h == hCmpRunList || h == hCmpCmpList) {
        if (msg == WM_MOUSEMOVE) h == hSumList ? summaryHover(h, lp) : campaignRunsHover(h, lp);
        else if (msg == WM_MOUSELEAVE || msg == WM_MOUSEWHEEL || msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN)
            hideTip();
    }
    if (msg == WM_NOTIFY) {
        NMHDR* nh = (NMHDR*)lp;
        bool runs = h == hCmpRunList || h == hCmpCmpList;  // columns per act: moved by dragging only
        if (nh->hwndFrom == ListView_GetHeader(h) && nh->code == NM_RCLICK && runs) return TRUE;
        if (nh->hwndFrom == ListView_GetHeader(h) && nh->code == NM_RCLICK) {
            HDHITTESTINFO ht = {};
            GetCursorPos(&ht.pt);
            ScreenToClient(nh->hwndFrom, &ht.pt);
            int col = (int)SendMessageW(nh->hwndFrom, HDM_HITTEST, 0, (LPARAM)&ht);
            if (col >= 0 && ((gHiddenCols[h] >> col) & 1)) col = -1;
            showColumnMenu(h, col);
            return TRUE;
        }
        if (nh->hwndFrom == ListView_GetHeader(h) && nh->code == HDN_ENDDRAG)  // columns moved: save once applied
            PostMessageW(h, WM_SAVE_ORDER, 0, 0);
        if (nh->hwndFrom == ListView_GetHeader(h) && (nh->code == HDN_BEGINTRACKW || nh->code == HDN_BEGINTRACKA)) {
            int col = ((NMHEADERW*)lp)->iItem;
            if ((gHiddenCols[h] >> col) & 1) return TRUE;  // hidden columns can't be dragged open
        }
        if (nh->hwndFrom == ListView_GetHeader(h) && nh->code == NM_CUSTOMDRAW) {
            NMCUSTOMDRAW* cd = (NMCUSTOMDRAW*)lp;
            if (cd->dwDrawStage == CDDS_PREPAINT) {
                RECT rc;
                GetClientRect(nh->hwndFrom, &rc);
                FillRect(cd->hdc, &rc, brCard);
                return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
            }
            if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
                RECT r = cd->rc;
                FillRect(cd->hdc, &r, brCard);
                wchar_t buf[64] = {0};
                HDITEMW hi = {};
                hi.mask = HDI_TEXT | HDI_FORMAT;
                hi.pszText = buf;
                hi.cchTextMax = 63;
                Header_GetItem(nh->hwndFrom, (int)cd->dwItemSpec, &hi);
                SortState st = gSort[h];
                bool sorted = st.col == (int)cd->dwItemSpec;
                SetBkMode(cd->hdc, TRANSPARENT);
                SetTextColor(cd->hdc, st.col == (int)cd->dwItemSpec ? C::Pink : C::Purple);
                SelectObject(cd->hdc, fBold);
                r.left += 6;
                r.right -= 6;
                DrawTextW(cd->hdc, buf, -1, &r,
                          DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | ((hi.fmt & HDF_RIGHT) ? DT_RIGHT : DT_LEFT));
                if (sorted)  // arrow on the side opposite to the text, so the name is never cut
                    DrawTextW(cd->hdc, st.desc ? L"\u25bc" : L"\u25b2", -1, &r,
                              DT_VCENTER | DT_SINGLELINE | ((hi.fmt & HDF_RIGHT) ? DT_LEFT : DT_RIGHT));
                RECT ln = {cd->rc.left, cd->rc.bottom - 1, cd->rc.right, cd->rc.bottom};
                HBRUSH b = CreateSolidBrush(C::Line);
                FillRect(cd->hdc, &ln, b);
                DeleteObject(b);
                return CDRF_SKIPDEFAULT;
            }
            if (cd->dwDrawStage == CDDS_POSTPAINT) {  // filler area right of the last column
                placeSearchBox(h, nh->hwndFrom, cd->hdc);
                RECT rc, last = {0, 0, 0, 0};
                GetClientRect(nh->hwndFrom, &rc);
                int n = Header_GetItemCount(nh->hwndFrom);
                if (n > 0) Header_GetItemRect(nh->hwndFrom, n - 1, &last);
                rc.left = last.right;
                if (rc.left < rc.right) {
                    FillRect(cd->hdc, &rc, brCard);
                    RECT ln = {rc.left, rc.bottom - 1, rc.right, rc.bottom};
                    HBRUSH b = CreateSolidBrush(C::Line);
                    FillRect(cd->hdc, &ln, b);
                    DeleteObject(b);
                }
                return CDRF_DODEFAULT;
            }
        }
    }
    return DefSubclassProc(h, msg, wp, lp);
}

std::vector<MenuItem> gMenuItems;  // owner-draw item data (kept alive while the menu is open)

void showColumnMenu(HWND lv, int col) {
    int n = Header_GetItemCount(ListView_GetHeader(lv));
    unsigned hidden = gHiddenCols[lv];
    int visible = 0;
    for (int i = 0; i < n; ++i) visible += !((hidden >> i) & 1);
    wchar_t name[64] = {0};
    if (col >= 0) {
        HDITEMW hi = {};
        hi.mask = HDI_TEXT;
        hi.pszText = name;
        hi.cchTextMax = 63;
        Header_GetItem(ListView_GetHeader(lv), col, &hi);
    }
    SortState st = gSort[lv];
    bool filterMenu = lv == hHist && col == 1;  // Map column on Maps: category filter
    gMenuItems.clear();
    gMenuItems.reserve(16);  // item data pointers must stay valid while the menu is open
    if (filterMenu) {
        for (int i = 0; i < 4; ++i) gMenuItems.push_back({std::wstring(L"Show: ") + kCatName[i], false});
        gMenuItems.push_back({L"", true});
    }
    size_t base = gMenuItems.size();
    gMenuItems.push_back({L"Sort ascending  \u25b2", false});
    gMenuItems.push_back({L"Sort descending  \u25bc", false});
    gMenuItems.push_back({L"Default order", false});
    gMenuItems.push_back({L"", true});
    gMenuItems.push_back({L"Hide \u201c" + std::wstring(name) + L"\u201d", false});
    gMenuItems.push_back({L"", true});
    gMenuItems.push_back({L"Unhide all columns", false});
    HMENU m = CreatePopupMenu();
    MENUINFO mi = {};
    mi.cbSize = sizeof mi;
    mi.fMask = MIM_BACKGROUND;
    mi.hbrBack = brCard;
    SetMenuInfo(m, &mi);
    if (filterMenu) {
        for (int i = 0; i < 4; ++i)
            AppendMenuW(m, MF_OWNERDRAW | (gMapFilter == i ? MF_CHECKED : 0), 20 + i, (LPCWSTR)&gMenuItems[i]);
        AppendMenuW(m, MF_OWNERDRAW | MF_DISABLED, 0, (LPCWSTR)&gMenuItems[4]);
    }
    UINT noCol = col < 0 ? MF_GRAYED : 0;
    AppendMenuW(m, MF_OWNERDRAW | noCol | ((st.col == col && !st.desc) ? MF_CHECKED : 0), 10,
                (LPCWSTR)&gMenuItems[base + 0]);
    AppendMenuW(m, MF_OWNERDRAW | noCol | ((st.col == col && st.desc) ? MF_CHECKED : 0), 11,
                (LPCWSTR)&gMenuItems[base + 1]);
    AppendMenuW(m, MF_OWNERDRAW | (st.col >= 0 ? 0 : MF_GRAYED), 12, (LPCWSTR)&gMenuItems[base + 2]);
    AppendMenuW(m, MF_OWNERDRAW | MF_DISABLED, 0, (LPCWSTR)&gMenuItems[base + 3]);
    AppendMenuW(m, MF_OWNERDRAW | ((col < 0 || visible <= 1) ? MF_GRAYED : 0), 1, (LPCWSTR)&gMenuItems[base + 4]);
    AppendMenuW(m, MF_OWNERDRAW | MF_DISABLED, 0, (LPCWSTR)&gMenuItems[base + 5]);
    AppendMenuW(m, MF_OWNERDRAW | (hidden ? 0 : MF_GRAYED), 2, (LPCWSTR)&gMenuItems[base + 6]);
    POINT pt;
    GetCursorPos(&pt);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hMain, nullptr);
    DestroyMenu(m);
    if (cmd >= 20 && cmd <= 23) {
        gMapFilter = cmd - 20;
        WritePrivateProfileStringW(L"filter", L"General", std::to_wstring(gMapFilter).c_str(), iniPath().c_str());
        refreshUi();
        if (ListView_GetItemCount(lv)) ListView_EnsureVisible(lv, ListView_GetItemCount(lv) - 1, FALSE);
        return;
    }
    if (cmd == 10 || cmd == 11 || cmd == 12) {
        gSort[lv] = cmd == 12 ? SortState{} : SortState{col, cmd == 11};
        WritePrivateProfileStringW(L"sort", listKey(lv),
                                   (std::to_wstring(gSort[lv].col) + L"," + (gSort[lv].desc ? L"1" : L"0")).c_str(),
                                   iniPath().c_str());
        InvalidateRect(ListView_GetHeader(lv), nullptr, TRUE);
        if (lv == hSumList) refreshSummary();
        else if (lv == hCmpList || lv == hCmpActList) refreshCampaign(true);
        else if (lv == hExpList) expListChanged();
        else refreshUi();
        if (ListView_GetItemCount(lv)) ListView_EnsureVisible(lv, 0, FALSE);
        return;
    }
    if (cmd == 1) gHiddenCols[lv] |= 1u << col;
    if (cmd == 2) gHiddenCols[lv] = 0;
    if (cmd) {
        WritePrivateProfileStringW(L"columns", listKey(lv), std::to_wstring(gHiddenCols[lv]).c_str(),
                                   iniPath().c_str());
        relayoutLists();
    }
}

// list rows: stripes, TOTAL/AVERAGE rows, current campaign zone
LRESULT listCustomDraw(NMLVCUSTOMDRAW* cd) {
    if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (cd->nmcd.dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
    HWND lv = cd->nmcd.hdr.hwndFrom;
    int row = (int)cd->nmcd.dwItemSpec;
    cd->nmcd.uItemState &= ~(CDIS_SELECTED | CDIS_FOCUS);  // no system selection colors
    cd->clrText = C::Fg;
    cd->clrTextBk = row % 2 ? C::Stripe : C::Card;
    wchar_t c1[32] = {0};
    if (lv != hSumList) ListView_GetItemText(lv, row, 1, c1, 31);
    bool total = wcscmp(c1, L"TOTAL") == 0, avg = wcscmp(c1, L"AVERAGE") == 0;
    if (total || avg) {
        cd->clrTextBk = C::Darkest;
        cd->clrText = total ? C::Pink : C::Cyan;
        SelectObject(cd->nmcd.hdc, fBold);
        return CDRF_NEWFONT;
    }
    if (lv == hHist && row == gHistCurRow) {  // the map being played now
        cd->clrTextBk = C::Line;
        cd->clrText = g.cur.state == RunState::Paused ? C::Orange : C::Green;
    }
    if (lv == hCmpList && row < (int)gCmpRows.size() && gCmpRows[row] == g.curZone) {
        cd->clrTextBk = C::Line;
        cd->clrText = C::Green;
    }
    return CDRF_DODEFAULT;
}

HWND list(int id, int page, const wchar_t* const* cols, const int* widths, int n, unsigned rightMask) {
    HWND lv = mk(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL, id, fNormal,
                 page);  // header buttons on: needed for dragging columns
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP);
    ListView_SetBkColor(lv, C::Card);
    ListView_SetTextBkColor(lv, C::Card);
    ListView_SetTextColor(lv, C::Fg);
    for (int i = 0; i < n; ++i) {
        LVCOLUMNW c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        c.fmt = (rightMask >> i) & 1 ? LVCFMT_RIGHT : LVCFMT_LEFT;
        c.pszText = (LPWSTR)cols[i];
        c.cx = widths[i];
        ListView_InsertColumn(lv, i, &c);
    }
    darkControl(lv);
    SetWindowTheme(ListView_GetHeader(lv), gTheme.dark ? L"DarkMode_ItemsView" : L"ItemsView", nullptr);
    HWND hd = ListView_GetHeader(lv);  // columns can be dragged left / right (set on the header too, for older comctl)
    SetWindowLongPtrW(hd, GWL_STYLE, GetWindowLongPtrW(hd, GWL_STYLE) | HDS_DRAGDROP);
    SetWindowSubclass(lv, ListProc, 2, 0);
    // row height a bit taller: tiny invisible image list
    HIMAGELIST il = ImageList_Create(1, 24, ILC_COLOR, 1, 0);
    ListView_SetImageList(lv, il, LVSIL_SMALL);
    return lv;
}
