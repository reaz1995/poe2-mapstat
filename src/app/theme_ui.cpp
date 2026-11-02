// Colours, brushes, fonts, dark/light mode and the theme selector (themes are built into the exe).
#include "app.h"
#include "themes_gen.h"  // themes/*.yaml baked in at build time

static COLORREF cr(uint32_t v);
static bool embeddedTheme(const std::string& key, Theme& t);
static void applyTheme();

void initDarkMode() {
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    if (!ux) return;
    using SetPreferredAppMode_t = int(WINAPI*)(int);
    auto setMode = (SetPreferredAppMode_t)(void*)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    if (setMode) setMode(gTheme.dark ? 2 : 3);  // ForceDark / ForceLight
}

void darkControl(HWND h) {
    static auto allow = (BOOL(WINAPI*)(HWND, BOOL))(void*)GetProcAddress(LoadLibraryW(L"uxtheme.dll"), MAKEINTRESOURCEA(133));
    if (allow) allow(h, gTheme.dark);
    SetWindowTheme(h, gTheme.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);  // scrollbars
}

void darkTitleBar(HWND h) {
    BOOL on = gTheme.dark;
    if (FAILED(DwmSetWindowAttribute(h, 20, &on, sizeof on))) DwmSetWindowAttribute(h, 19, &on, sizeof on);
}

HFONT font(int px, int weight, const wchar_t* face) {
    return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
}

void makeFonts() {
    fNormal = font(15, FW_NORMAL, gFontUi.c_str());
    fBold = font(15, FW_SEMIBOLD, gFontUi.c_str());
    fTab = font(16, FW_SEMIBOLD, gFontUi.c_str());
    fSection = font(13, FW_BOLD, gFontUi.c_str());
    fTitle = font(30, FW_SEMIBOLD, gFontUi.c_str());
    fTimer = font(60, FW_BOLD, gFontMono.c_str());
}

static COLORREF cr(uint32_t v) { return RGB((v >> 16) & 255, (v >> 8) & 255, v & 255); }

// Built-in themes (themes/*.yaml baked into the exe). Default = Dracula (the Theme struct defaults).
static bool embeddedTheme(const std::string& key, Theme& t) {
    for (int i = 0; i < kEmbeddedThemeCount; ++i)
        if (key == kEmbeddedThemes[i].file) {
            Theme r;
            std::string e, w;
            if (!parseTheme(kEmbeddedThemes[i].yaml, r, e, w)) return false;
            t = r;
            return true;
        }
    return false;
}

void loadStartupTheme() {
    wchar_t buf[MAX_PATH];
    GetPrivateProfileStringW(L"main", L"Theme", L"", buf, MAX_PATH, iniPath().c_str());
    gTheme = Theme{};
    gThemeKey.clear();
    std::string k = U8(buf);
    size_t sl = k.find_last_of("\\/");  // older versions stored "themes\\x.yaml"
    if (sl != std::string::npos) k = k.substr(sl + 1);
    if (k.size() > 5 && k.compare(k.size() - 5, 5, ".yaml") == 0) k.resize(k.size() - 5);
    Theme t;
    if (!k.empty() && k != "dracula" && embeddedTheme(k, t)) {
        gTheme = t;
        gThemeKey = k;
    }
}

void updateThemeLabel() {
    if (!hThemeSel) return;
    std::wstring s = gThemeKey.empty() ? L"Default (Dracula)" : W(gTheme.name);
    SetWindowTextW(hThemeSel, (s + L"   \u25be").c_str());
}

void setThemeColors() {
    const Theme& t = gTheme;
    C::Bg = cr(t.bg), C::Card = cr(t.card), C::Darkest = cr(t.darkest), C::Line = cr(t.line);
    C::Stripe = cr(t.stripe), C::Fg = cr(t.fg), C::Comment = cr(t.comment), C::Cyan = cr(t.cyan);
    C::Green = cr(t.green), C::Orange = cr(t.orange), C::Pink = cr(t.pink), C::Purple = cr(t.purple);
    C::Red = cr(t.red), C::Yellow = cr(t.yellow);
    gFontUi = W(t.fontUi);
    gFontMono = W(t.fontMono);
    HBRUSH o[3] = {brBg, brCard, brDarkest};
    brBg = CreateSolidBrush(C::Bg);
    brCard = CreateSolidBrush(C::Card);
    brDarkest = CreateSolidBrush(C::Darkest);
    if (hMain) SetClassLongPtrW(hMain, GCLP_HBRBACKGROUND, (LONG_PTR)brBg);
    for (HBRUSH b : o)
        if (b) DeleteObject(b);
}

// switch theme while running: colours, fonts, light/dark controls, repaint everything
static void applyTheme() {
    struct Swap { HFONT from[6], to[6]; } sw;
    HFONT* fs[6] = {&fNormal, &fBold, &fTab, &fSection, &fTitle, &fTimer};
    for (int i = 0; i < 6; ++i) sw.from[i] = *fs[i];
    setThemeColors();
    makeFonts();
    for (int i = 0; i < 6; ++i) sw.to[i] = *fs[i];
    EnumChildWindows(hMain, [](HWND c, LPARAM lp) -> BOOL {
        auto* s = (Swap*)lp;
        HFONT f = (HFONT)SendMessageW(c, WM_GETFONT, 0, 0);
        for (int i = 0; i < 6; ++i)
            if (f == s->from[i]) SendMessageW(c, WM_SETFONT, (WPARAM)s->to[i], FALSE);
        return TRUE;
    }, (LPARAM)&sw);
    for (HFONT f : sw.from) DeleteObject(f);
    for (HWND lv : {hHist, hSumList, hCmpList}) {
        ListView_SetBkColor(lv, C::Card);
        ListView_SetTextBkColor(lv, C::Card);
        ListView_SetTextColor(lv, C::Fg);
        darkControl(lv);
        SetWindowTheme(ListView_GetHeader(lv), gTheme.dark ? L"DarkMode_ItemsView" : L"ItemsView", nullptr);
    }
    darkControl(hNpc);
    initDarkMode();
    darkTitleBar(hMain);
    if (fOvHead) ovFonts(gOvScale);
    SetWindowPos(hMain, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    RedrawWindow(hMain, nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
    if (hOverlay) InvalidateRect(hOverlay, nullptr, TRUE);
    if (hCmdBar) InvalidateRect(hCmdBar, nullptr, TRUE);
    updateThemeLabel();
}

// theme selector: built-in themes, Default at the bottom
void themeMenu() {
    struct Item { std::string key, name; };
    std::vector<Item> items;
    for (int i = 0; i < kEmbeddedThemeCount; ++i) {
        std::string key = kEmbeddedThemes[i].file;
        if (key == "dracula") continue;  // that is the default
        Theme t;
        if (embeddedTheme(key, t)) items.push_back({key, t.name});
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.name < b.name; });
    gMenuItems.clear();
    gMenuItems.reserve(items.size() + 3);
    const wchar_t* tick = L"\u2713  ";
    const wchar_t* none = L"     ";
    for (auto& it : items) gMenuItems.push_back({std::wstring(it.key == gThemeKey ? tick : none) + W(it.name), false});
    gMenuItems.push_back({L"", true});
    gMenuItems.push_back({std::wstring(gThemeKey.empty() ? tick : none) + L"Default (Dracula)", false});
    HMENU m = CreatePopupMenu();
    MENUINFO mi = {};
    mi.cbSize = sizeof mi;
    mi.fMask = MIM_BACKGROUND;
    mi.hbrBack = brCard;
    SetMenuInfo(m, &mi);
    for (size_t i = 0; i < gMenuItems.size(); ++i)
        AppendMenuW(m, MF_OWNERDRAW | (gMenuItems[i].separator ? MF_DISABLED : 0), gMenuItems[i].separator ? 0 : 100 + i,
                    (LPCWSTR)&gMenuItems[i]);
    RECT br;
    GetWindowRect(hThemeSel, &br);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTBUTTON, br.left, br.bottom + 2, 0, hMain, nullptr);
    DestroyMenu(m);
    if (!cmd) return;
    size_t idx = (size_t)cmd - 100;
    if (idx < items.size()) {
        Theme t;
        if (!embeddedTheme(items[idx].key, t)) return;
        gTheme = t;
        gThemeKey = items[idx].key;
    } else {
        gTheme = Theme{};
        gThemeKey.clear();
    }
    WritePrivateProfileStringW(L"main", L"Theme", W(gThemeKey).c_str(), iniPath().c_str());
    applyTheme();
}
