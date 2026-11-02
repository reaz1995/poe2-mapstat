// Creates all controls and lays out the pages.
#include "app.h"

static HWND hExpCap[6];  // captions above the Experience inputs
static HWND hExpModsLbl;  // "TABLETS / MAP MODS" section title

// Maps tab has two sub-pages (Session, Summary); the tab opens the one that was shown last.
static int gMapsPage = PG_GENERAL;

void showMapsPage() { showPage(gMapsPage); }

void showPage(int p) {
    gPage = p;
    bool maps = p == PG_GENERAL || p == PG_SUMMARY;
    if (maps) gMapsPage = p;
    for (int i = 0; i < PG_COUNT; ++i)
        for (HWND h : gPages[i]) ShowWindow(h, i == p ? SW_SHOW : SW_HIDE);
    for (HWND h : {hSubSession, hSubSummary}) {
        ShowWindow(h, maps ? SW_SHOW : SW_HIDE);
        InvalidateRect(h, nullptr, FALSE);
    }
    for (HWND h : {hTabGen, hTabCmp, hTabExp, hGear}) InvalidateRect(h, nullptr, FALSE);
    if (p == PG_EXPERIENCE) refreshExperience();
    InvalidateRect(hMain, nullptr, TRUE);
    if (p == PG_SUMMARY) refreshSummary();
    if (p == PG_CAMPAIGN) refreshCampaign(true);
}

void layout(int w, int h) {
    for (auto& c : gCards) c.clear();
    const int P = 16, rh = 32, pad = 10;
    const int L = P, R = w - P, T = kTabBarH + 14, B = h - P, CW = R - L;

    // tab bar
    place(hTabGen, 8, 0, 100, kTabBarH);
    place(hTabCmp, 108, 0, 116, kTabBarH);
    place(hTabExp, 224, 0, 122, kTabBarH);
    place(hGear, w - kTabBarH - 6, 0, kTabBarH, kTabBarH);
    if (hUpdTab) place(hUpdTab, w - kTabBarH - 6 - 170, 0, 166, kTabBarH);  // just left of the gear

    // ---- Maps: sub-tabs Session / Summary on top of both pages
    place(hSubSession, L, T, 100, 30);
    place(hSubSummary, L + 108, T, 106, 30);
    const int MT = T + 44;  // content of the Maps pages starts under the sub-tabs
    int y = MT;
    place(hTitle, L, y, CW - 260, 40);
    place(hTimer, R - 250, y - 6, 250, 72);
    place(hStatus, L, y + 42, CW - 260, 22);
    y += 76;
    card(PG_GENERAL, L, y, CW, 5 * 20 + 2 * pad);
    place(hDetLbl, L + pad + 4, y + pad, 90, 5 * 20);
    place(hDetails, L + pad + 100, y + pad, CW - 2 * pad - 104, 5 * 20);
    y += 5 * 20 + 2 * pad + 12;
    place(hGenReset, L, y, 130, rh);
    place(hExpGen, L + 138, y, 130, rh);
    place(hRestoreGen, L + 276, y, 200, rh);
    y += rh + 8;
    place(hLoadHist, L, y, 476, rh);  // under the three buttons, same width as the row
    y += rh + 14;
    place(hLblHist, L, y + 2, 70, 18);
    place(hSummary, L + 75, y, CW - 75, 22);
    y += 26;
    int hh = std::max(80, B - y);
    card(PG_GENERAL, L, y, CW, hh);
    place(hHist, L + 6, y + 6, CW - 12, hh - 12);

    // ---- Summary
    place(hSumReset, R - 286, T - 1, 140, rh);  // on the sub-tab row
    place(hExpSum, R - 138, T - 1, 138, rh);
    place(hLblSum, L, MT + 2, 130, 18);
    place(hSumHdr, L + 135, MT, CW - 135, 22);
    {
        int sy = MT + 28;
        card(PG_SUMMARY, L, sy, CW, B - sy);
        place(hSumList, L + 6, sy + 6, CW - 12, B - sy - 12);
    }

    // ---- Campaign
    place(hCmpTitle, L, T, CW - 260, 40);
    place(hCmpTimer, R - 250, T - 6, 250, 72);
    place(hCmpTimerLoad, R - 250, T + 62, 250, 20);  // under the timer
    place(hCmpStatus, L, T + 42, CW - 260, 22);
    place(hCmpReset, L, T + 80, 180, rh);
    place(hExpCmp, L + 188, T + 80, 130, rh);
    place(hRestoreCmp, L + 326, T + 80, 230, rh);
    place(hCmpPause, R - 206, T + 92, 100, rh);
    place(hCmpStop, R - 100, T + 92, 100, rh);
    gActRowX = L;
    gActRowY = T + 80 + rh + 14;
    placeActs();
    place(hLblCmp, L, gActRowY + 44, 70, 18);
    place(hCmpSum, L + 75, gActRowY + 42, CW - 75, 22);
    {
        int cy = gActRowY + 70;
        card(PG_CAMPAIGN, L, cy, CW, B - cy);
        place(hCmpList, L + 6, cy + 6, CW - 12, B - cy - 12);
    }

    // ---- Experience
    y = T;
    place(hExpHint, L, y, CW, 20);  // one line of help above the inputs
    y += 30;
    {
        int x = L;
        struct {
            HWND e;
            int w;
        } f[] = {{hExpName, 260},   {hExpLevel, 80},   {hExpArea, 80},
                 {hExpTabEff, 200}, {hExpMapEff, 200}, {hExpGain, 200}};
        for (int i = 0; i < 6; ++i) {
            if (i == 3) {  // second row: tablets and map mods
                x = L;
                y += 24 + rh + 16;
                place(hExpModsLbl, L, y, CW, 18);
                y += 24;
            }
            auto& e = f[i];
            place(hExpCap[i], x, y, e.w + 10, 18);
            card(PG_EXPERIENCE, x, y + 24, e.w, rh);
            place(e.e, x + 10, y + 31, e.w - 20, rh - 12);
            x += e.w + 16;
        }
        y += 24;
        y += rh + 18;
        card(PG_EXPERIENCE, L, y, CW, 8 * 20 + 2 * pad);
        place(hExpRowLbl, L + pad + 4, y + pad, 170, 8 * 20);
        place(hExpInfo, L + pad + 180, y + pad, CW - 2 * pad - 184, 8 * 20);
        y += 8 * 20 + 2 * pad + 16;
        place(hExpOvChk, L, y, 420, 26);
    }

    // ---- Options
    y = T;
    place(hLblPath, L, y, CW, 18);
    y += 24;
    card(PG_OPTIONS, L, y, CW - 300, rh);
    place(hPath, L + 10, y + 7, CW - 320, rh - 12);
    place(hBrowse, R - 290, y, 130, rh);
    place(hReload, R - 152, y, 152, rh);
    y += rh + 18;
    place(hOnTop, L, y, 200, 26);
    place(hCloseChk, L + 220, y, 300, 26);
    place(hExitBtn, R - 140, y - 3, 140, rh);
    y += 44;
    place(hLblOverlay, L, y, CW, 18);
    y += 26;
    place(hOverlayChk, L, y, 200, 26);
    place(hRecLbl, L + 220, y + 4, 110, 20);
    place(hRecMinus, L + 334, y - 3, 34, rh);
    place(hRecVal, L + 372, y + 4, 34, 20);
    place(hRecPlus, L + 410, y - 3, 34, rh);
    y += 40;
    place(hCmdChk, L, y, 210, 26);
    y += 44;
    place(hLblKeys, L, y, CW, 18);
    y += 26;
    place(hKeysChk, L, y, 260, 26);
    y += 36;
    for (int i = 0; i < HK_COUNT; ++i, y += rh + 6) {  // one column: name, key
        place(hKeyLbl[i], L, y + 6, 130, 20);
        place(hKeyBtn[i], L + 140, y, 140, rh);
    }
    y += 14;
    place(hLblTheme, L, y, CW, 18);
    y += 26;
    place(hThemeSel, L, y, 240, rh);
    y += rh + 24;
    if (hLblUpd) {
        place(hLblUpd, L, y, CW, 18);
        y += 26;
        if (hUpdChk) place(hUpdChk, L, y, 260, 26);
        place(hUpdInfo, hUpdChk ? L + 280 : L, y + 3, hUpdChk ? CW - 280 : CW, 20);
        y += 44;
    }
    card(PG_OPTIONS, L, y, CW, 7 * 20 + 2 * pad);
    place(hOptLbl, L + pad + 4, y + pad, 130, 7 * 20);
    place(hOptInfo, L + pad + 140, y + pad, CW - 2 * pad - 144, 7 * 20);

    fitColumns(hHist, kHistW, 9);
    fitColumns(hSumList, kSumW, 7);
    fitColumns(hCmpList, kCmpW, 11);
    InvalidateRect(hMain, nullptr, TRUE);
}

void createControls() {
    makeFonts();

    // tab bar
    hTabGen = btn(L"Maps", ID_TAB_GEN, -1, Btn::Tab);
    hSubSession = btn(L"Session", ID_TAB_SES, -1, Btn::Pill);
    hSubSummary = btn(L"Summary", ID_TAB_SUM, -1, Btn::Pill);
    hTabCmp = btn(L"Campaign", ID_TAB_CMP, -1, Btn::Tab);
    hTabExp = btn(L"Experience", ID_TAB_EXP, -1, Btn::Tab);
    hGear = btn(L"", ID_GEAR, -1, Btn::Gear);
#ifdef UPDATE_REPO
    hUpdTab = btn(L"Check for updates", ID_UPD_TAB, -1, Btn::Tab);
#endif

    // Maps
    hTitle = label(L"", ID_TITLE, fTitle, PG_GENERAL, false, SS_ENDELLIPSIS);
    hTimer = mk(L"STATIC", L"00:00", SS_RIGHT, ID_TIMER, fTimer, PG_GENERAL);
    hStatus = label(L"", ID_STATUS, fBold, PG_GENERAL);
    hDetLbl = label(L"Area ID\r\nSeed\r\nGenerated\r\nHideout\r\nSub-areas", ID_DETLBL, fNormal, PG_GENERAL, true);
    hDetails = label(L"", ID_DETAILS, fNormal, PG_GENERAL, true);
    hGenReset = btn(L"Reset Session", ID_GEN_RESET, PG_GENERAL, Btn::Danger);
    hExpGen = btn(L"Export to CSV", ID_EXP_GEN, PG_GENERAL, Btn::Accent);
    hSummary = label(L"", ID_SUMMARY, fBold, PG_GENERAL, false, SS_ENDELLIPSIS);
    hLblHist = label(L"MAPS", ID_LBL_HIST, fSection, PG_GENERAL);
    const wchar_t* hc[] = {L"#", L"Map", L"Area Lvl", L"Area ID", L"Seed", L"Entered", L"Load", L"Map time", L"Entries"};
    hHist = list(ID_HIST, PG_GENERAL, hc, kHistW, 9, 0b111110100);

    // Summary
    hSumHdr = label(L"", ID_SUMHDR, fBold, PG_SUMMARY, false, SS_ENDELLIPSIS);
    hSumReset = btn(L"Reset Summary", ID_SUM_RESET, PG_SUMMARY, Btn::Danger);
    hExpSum = btn(L"Export to CSV", ID_EXP_SUM, PG_SUMMARY, Btn::Accent);
    hLblSum = label(L"MAPS BY TYPE", ID_LBL_SUM, fSection, PG_SUMMARY);
    const wchar_t* sc[] = {L"Map", L"Area Lvl", L"Area ID", L"Played", L"Total time", L"Average", L"Best"};
    hSumList = list(ID_SUMLIST, PG_SUMMARY, sc, kSumW, 7, 0b1111010);

    // Campaign
    hCmpTitle = label(L"", ID_CMP_TITLE, fTitle, PG_CAMPAIGN, false, SS_ENDELLIPSIS);
    hCmpTimer = mk(L"STATIC", L"00:00", SS_RIGHT, ID_CMP_TIMER, fTimer, PG_CAMPAIGN);
    hCmpStatus = label(L"", ID_CMP_STATUS, fBold, PG_CAMPAIGN);
    hCmpReset = btn(L"Reset Campaign Run", ID_CMP_RESET, PG_CAMPAIGN, Btn::Danger);
    hCmpTimerLoad = mk(L"STATIC", L"", SS_RIGHT | SS_NOPREFIX, ID_CMP_TIMER_LOAD, fNormal, PG_CAMPAIGN);
    hCmpPause = btn(L"Pause", ID_CMP_PAUSE, PG_CAMPAIGN, Btn::Normal);
    hCmpStop = btn(L"Stop", ID_CMP_STOP, PG_CAMPAIGN, Btn::Danger);
    hExpCmp = btn(L"Export to CSV", ID_EXP_CMP, PG_CAMPAIGN, Btn::Accent);
    hLblCmp = label(L"ZONES", ID_LBL_CMP, fSection, PG_CAMPAIGN);
    hCmpSum = label(L"", ID_CMP_SUM, fBold, PG_CAMPAIGN, false, SS_ENDELLIPSIS);
    const wchar_t* cc[] = {L"#", L"Zone", L"Area Lvl", L"Area ID", L"Seed", L"Start", L"Last entered", L"Entries",
                           L"Total time", L"Load", L"Avg load"};
    hCmpList = list(ID_CMP_LIST, PG_CAMPAIGN, cc, kCmpW, 11, 0b11110010100);
    for (HWND lv : {hHist, hSumList, hCmpList}) addSearchBox(lv);

    // Options
    hLblPath = label(L"LOG FILE  (LatestClient.txt)", ID_LBL_PATH, fSection, PG_OPTIONS);
    hPath = mk(L"EDIT", gLogPath.c_str(), ES_AUTOHSCROLL | WS_TABSTOP, ID_PATH, fNormal, PG_OPTIONS);
    hBrowse = btn(L"Browse...", ID_BROWSE, PG_OPTIONS, Btn::Normal);
    hReload = btn(L"Load / Reload", ID_RELOAD, PG_OPTIONS, Btn::Accent);
    hOnTop = btn(L"Always on top", ID_ONTOP, PG_OPTIONS, Btn::Check);
    hCloseChk = btn(L"Start hidden in tray", ID_CLOSE_CHK, PG_OPTIONS, Btn::Check);
    hExitBtn = btn(L"Exit app", ID_EXIT, PG_OPTIONS, Btn::Danger);
    hLblOverlay = label(L"OVERLAYS   \u00b7   shown only while Path of Exile 2 is open", ID_LBL_OVERLAY, fSection, PG_OPTIONS);
    hOverlayChk = btn(L"MapTimerOverlay", ID_OVERLAY, PG_OPTIONS, Btn::Check);
    hRecLbl = label(L"Recent records", ID_REC_LBL, fNormal, PG_OPTIONS);
    hRecMinus = btn(L"\u2212", ID_REC_MINUS, PG_OPTIONS, Btn::Normal);
    hRecVal = label(L"3", ID_REC_VAL, fBold, PG_OPTIONS, false, SS_CENTER);
    hRecPlus = btn(L"+", ID_REC_PLUS, PG_OPTIONS, Btn::Normal);
    hCmdChk = btn(L"CommandBarOverlay", ID_CMDBAR, PG_OPTIONS, Btn::Check);
    // Experience
    {
        const wchar_t* t[6] = {L"CHARACTER NAME",      L"LEVEL",
                               L"AREA LEVEL",          L"TABLET EFFECTIVENESS %",
                               L"MAP EFFECTIVENESS %", L"TABLET EXP GAIN %"};
        for (int i = 0; i < 6; ++i) hExpCap[i] = label(t[i], ID_EXP_LBLS, fSection, PG_EXPERIENCE);
        hExpModsLbl = label(L"TABLETS / MAP MODS", ID_EXP_LBLS, fSection, PG_EXPERIENCE);
    }
    hExpName = mk(L"EDIT", gExpName.c_str(), ES_AUTOHSCROLL | WS_TABSTOP, ID_EXP_NAME, fNormal, PG_EXPERIENCE);
    hExpLevel = mk(L"EDIT", gExpLevel ? std::to_wstring(gExpLevel).c_str() : L"", ES_NUMBER | WS_TABSTOP, ID_EXP_LEVEL, fNormal, PG_EXPERIENCE);
    hExpArea = mk(L"EDIT", L"", ES_NUMBER | WS_TABSTOP, ID_EXP_AREA, fNormal, PG_EXPERIENCE);
    auto pctEdit = [](int v, int id) {
        HWND e = mk(L"EDIT", v ? std::to_wstring(v).c_str() : L"", ES_NUMBER | WS_TABSTOP, id, fNormal, PG_EXPERIENCE);
        SendMessageW(e, EM_LIMITTEXT, 4, 0);
        return e;
    };
    hExpTabEff = pctEdit(gTabEff, ID_EXP_TAB_EFF);
    hExpMapEff = pctEdit(gMapEff, ID_EXP_MAP_EFF);
    hExpGain = pctEdit(gExpGain, ID_EXP_GAIN);
    for (HWND e : {hExpLevel, hExpArea}) SendMessageW(e, EM_LIMITTEXT, 3, 0);  // levels 1-100
    hExpRowLbl = label(
        L"Exp to next level\r\nSafe range\r\nMonster base exp\r\nEffectiveness\r\nPenalty multiplier\r\n"
        L"Experience gain\r\nExpected exp\r\nBest area",
        ID_EXP_ROWLBL, fNormal, PG_EXPERIENCE, true);
    hExpInfo = label(L"", ID_EXP_INFO, fNormal, PG_EXPERIENCE, true);
    hExpOvChk = btn(L"Show safe area level and exp bonus on MapTimerOverlay", ID_EXP_OVCHK, PG_EXPERIENCE, Btn::Check);
    hExpHint = label(L"Enter your character name and the level updates by itself on every level up.", ID_EXP_HINT,
                     fNormal, PG_EXPERIENCE);

    hLblKeys = label(L"HOTKEYS   \u00b7   campaign timer, work while the game or this app is in front", ID_LBL_KEYS,
                     fSection, PG_OPTIONS);
    hKeysChk = btn(L"Enable hotkeys", ID_KEYS_CHK, PG_OPTIONS, Btn::Check);
    {
        const wchar_t* names[HK_COUNT] = {L"Stop", L"Pause / resume", L"Start new run"};
        for (int i = 0; i < HK_COUNT; ++i) {
            hKeyLbl[i] = label(names[i], ID_KEY_LBL0 + i, fNormal, PG_OPTIONS);
            hKeyBtn[i] = btn(hotkeyName(i).c_str(), ID_KEY_BTN0 + i, PG_OPTIONS, Btn::Normal);
        }
    }
    hLblTheme = label(L"THEME", ID_LBL_THEME, fSection, PG_OPTIONS);
    hThemeSel = btn(L"Default (Dracula)", ID_THEME_SEL, PG_OPTIONS, Btn::Normal);
    hLblUpd = label(L"UPDATES", ID_LBL_UPD, fSection, PG_OPTIONS);
#ifdef UPDATE_REPO
    hUpdChk = btn(L"Check for updates on start", ID_UPD_CHK, PG_OPTIONS, Btn::Check);
    hUpdInfo = label((L"Version v" + W(APP_VERSION) + L"  \u00b7  github.com/" + W(UPDATE_REPO)).c_str(), ID_UPD_INFO,
                     fNormal, PG_OPTIONS);
#else  // tell why there is nothing to click
    hUpdInfo = label((L"Version v" + W(APP_VERSION) + L"  \u00b7  self-update is off in this build: set GITHUB_REPO=owner/repo "
                      L"in .env and build Release").c_str(),
                     ID_UPD_INFO, fNormal, PG_OPTIONS);
#endif
    hLoadHist = btn(L"Load past Map session events...", ID_LOAD_HIST, PG_GENERAL, Btn::Accent);
    hRestoreGen = btn(L"Restore map session...", ID_RESTORE_GEN, PG_GENERAL, Btn::Normal);
    hRestoreCmp = btn(L"Restore campaign session...", ID_RESTORE_CMP, PG_CAMPAIGN, Btn::Normal);
    hOptLbl = label(L"Status\r\nRead\r\nLog session\r\nCurrent area\r\nMap session\r\nCampaign session\r\nSummary session", ID_OPT_LBL, fNormal, PG_OPTIONS, true);
    hOptInfo = label(L"", ID_OPT_INFO, fNormal, PG_OPTIONS, true);

    for (HWND lv : {hHist, hSumList, hCmpList}) {
        gHiddenCols[lv] = (unsigned)GetPrivateProfileIntW(L"columns", listKey(lv), 0, iniPath().c_str());
        wchar_t sb[32];
        GetPrivateProfileStringW(L"sort", listKey(lv), L"-1,0", sb, 32, iniPath().c_str());
        int c = -1, d = 0;
        if (swscanf(sb, L"%d,%d", &c, &d) == 2) gSort[lv] = {c, d != 0};
        loadColumnOrder(lv);
    }
    gMapFilter = std::clamp((int)GetPrivateProfileIntW(L"filter", L"General", 0, iniPath().c_str()), 0, 3);

    showPage(PG_GENERAL);
}
