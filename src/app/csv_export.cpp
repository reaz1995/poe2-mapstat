// Export of a list to CSV.
#include "app.h"

void exportListView(HWND lv, const wchar_t* defName) {
    wchar_t file[MAX_PATH];
    wcsncpy(file, defName, MAX_PATH - 1);
    file[MAX_PATH - 1] = 0;
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = hMain;
    ofn.lpstrFilter = L"CSV (*.csv)\0*.csv\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return;
    FILE* f = _wfopen(file, L"wb");
    if (!f) { MessageBoxW(hMain, L"Cannot write file.", L"Export", MB_ICONERROR); return; }
    fputs("\xEF\xBB\xBF", f);  // BOM so Excel reads UTF-8
    HWND hdr = ListView_GetHeader(lv);
    int cols = Header_GetItemCount(hdr), rows = ListView_GetItemCount(lv);
    unsigned hidden = gHiddenCols[lv];
    std::vector<int> order(cols);  // columns in the order they are shown (they can be dragged)
    if (!ListView_GetColumnOrderArray(lv, cols, order.data()))
        for (int i = 0; i < cols; ++i) order[i] = i;
    int lastVisible = -1;
    for (int c : order)
        if (!((hidden >> c) & 1)) lastVisible = c;
    auto put = [&](const std::wstring& t, int c) {
        if ((hidden >> c) & 1) return;
        std::string u = U8(t), q = "\"";
        for (char ch : u) q += ch == '"' ? std::string("\"\"") : std::string(1, ch);
        fprintf(f, "%s%s", (q + "\"").c_str(), c == lastVisible ? "\r\n" : ",");
    };
    for (int c : order) {
        wchar_t buf[256] = {0};
        HDITEMW hi = {};
        hi.mask = HDI_TEXT;
        hi.pszText = buf;
        hi.cchTextMax = 255;
        Header_GetItem(hdr, c, &hi);
        put(buf, c);
    }
    for (int r = 0; r < rows; ++r)
        for (int c : order) {
            wchar_t buf[512] = {0};
            ListView_GetItemText(lv, r, c, buf, 511);
            put(buf, c);
        }
    fclose(f);
}

std::wstring exportName(const wchar_t* what) {
    std::string st = g.sessionStart;
    for (char& c : st) if (c == '/' || c == ':') c = '-'; else if (c == ' ') c = '_';
    return std::wstring(what) + (st.empty() ? L"" : L"_" + W(st)) + L".csv";
}
