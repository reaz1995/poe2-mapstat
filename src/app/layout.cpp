// Creates all controls and lays out the pages.
#include "app.h"

#include <map>

static HWND hExpCap[6];  // captions above the Experience inputs
static HWND hExpModsLbl;  // "TABLETS / MAP MODS" section title

// Maps tab has two sub-pages (Session, Summary); the tab opens the one that was shown last.
static int gMapsPage = PG_GENERAL, gCmpPage = PG_CAMPAIGN;
HWND hRunsLoadChk = nullptr;
bool gRunsLoad = false;
HWND hCmpSubSession = nullptr, hCmpSubRuns = nullptr, hCmpRunList = nullptr, hLblRuns = nullptr, hCmpCmpList = nullptr,
     hLblCmpCmp = nullptr, hExpRuns = nullptr, hExpRunCmp = nullptr, hRunsImport = nullptr;

// Campaign -> Summary: hovering a list's title shows how to use the list.
static WNDPROC gHdrTipOld;
static LRESULT CALLBACK HdrTipProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT tme = {sizeof tme, TME_LEAVE, h, 0};
        TrackMouseEvent(&tme);
        if (h == hLblRuns)
            showListTip(L"hdr-runs", L"Saved runs",
                        {{L"Ctrl + click: add to the comparison", L""},
                         {L"Double-click: load the run", L""},
                         {L"Right-click: pin, rename or remove", L""},
                         {L"Hover an act: its zones", L""}});
        else
            showListTip(L"hdr-cmp", L"Comparison",
                        {{L"The first selected run is the base", L""},
                         {L"The others: faster (-) or slower (+)", L""},
                         {L"Hover an act: each zone's difference", L""}});
    } else if (msg == WM_MOUSELEAVE) hideTip();
    else if (msg == WM_SETCURSOR) {  // the help cursor hints there is something to read
        SetCursor(LoadCursor(nullptr, IDC_HELP));
        return TRUE;
    }
    return CallWindowProcW(gHdrTipOld, h, msg, wp, lp);
}
void showCampaignPage() { showPage(gCmpPage); }

void showMapsPage() { showPage(gMapsPage); }

// ---- Options pills: each control of the Options page belongs to one pill (or to all when not listed)
HWND hOptPill[3] = {};
HWND hZonesImport = nullptr;
int gOptPill = 0;
static std::map<HWND, int> gOptGroup;

static void applyOptPill() {
    for (auto& [h, grp] : gOptGroup)
        if (h) ShowWindow(h, gPage == PG_OPTIONS && grp == gOptPill ? SW_SHOW : SW_HIDE);
    for (HWND h : hOptPill)
        if (h) InvalidateRect(h, nullptr, FALSE);
}

void setOptPill(int i) {
    gOptPill = i;
    WritePrivateProfileStringW(L"main", L"OptionsPill", std::to_wstring(i).c_str(), iniPath().c_str());
    applyOptPill();
    InvalidateRect(hMain, nullptr, TRUE);
}

void showPage(int p) {
    gPage = p;
    bool maps = p == PG_GENERAL || p == PG_SUMMARY, cmp = p == PG_CAMPAIGN || p == PG_CMP_RUNS;
    if (maps) gMapsPage = p;
    if (cmp) gCmpPage = p;
    for (HWND h : {hCmpSubSession, hCmpSubRuns}) {
        ShowWindow(h, cmp ? SW_SHOW : SW_HIDE);
        InvalidateRect(h, nullptr, FALSE);
    }
    for (int i = 0; i < PG_COUNT; ++i)
        for (HWND h : gPages[i]) ShowWindow(h, i == p ? SW_SHOW : SW_HIDE);
    for (HWND h : {hSubSession, hSubSummary}) {
        ShowWindow(h, maps ? SW_SHOW : SW_HIDE);
        InvalidateRect(h, nullptr, FALSE);
    }
    for (HWND h : {hTabGen, hTabCmp, hTabExp, hTabLay, hTabSteps, hGear}) InvalidateRect(h, nullptr, FALSE);
    if (p == PG_EXPERIENCE) refreshExperience();
    if (p == PG_LAYOUTS) refreshLayouts();
    if (p == PG_STEPS) stepsChanged();
    InvalidateRect(hMain, nullptr, TRUE);
    if (p == PG_SUMMARY) refreshSummary();
    if (p == PG_CAMPAIGN) refreshCampaign(true);
    if (p == PG_CMP_RUNS) refreshCampaignRuns();
    campaignShowLists();
    applyOptPill();
}

void layout(int w, int h) {
    for (auto& c : gCards) c.clear();
    const int P = 16, rh = 32, pad = 10;
    const int L = P, R = w - P, T = kTabBarH + 14, B = h - P, CW = R - L;

    // tab bar
    place(hTabGen, 8, 0, 100, kTabBarH);
    place(hTabCmp, 108, 0, 116, kTabBarH);
    place(hTabExp, 224, 0, 122, kTabBarH);
    place(hTabLay, 346, 0, 106, kTabBarH);
    place(hTabSteps, 452, 0, 90, kTabBarH);
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

    // ---- Campaign: Session / Summary pills on top, the Session page under them
    place(hCmpSubSession, L, T, 100, 30);
    place(hCmpSubRuns, L + 108, T, 106, 30);
    place(hRunsLoadChk, L, T + 40, 260, 26);
    place(hLblRuns, L, T + 76, 110, 18);
    place(hRunsImport, R - 290, T + 68, 152, 30);
    place(hExpRuns, R - 130, T + 68, 130, 30);  // right of the label, over the runs list
    {  // the saved runs on top, the comparison of the selected ones under them
        int ry = T + 102, rh2 = (B - ry - 34) * 55 / 100, cy = ry + rh2 + 34;
        card(PG_CMP_RUNS, L, ry, CW, rh2);
        place(hCmpRunList, L + 6, ry + 6, CW - 12, rh2 - 12);
        place(hLblCmpCmp, L, ry + rh2 + 10, 110, 18);
        place(hExpRunCmp, R - 130, ry + rh2 + 2, 130, 30);
        card(PG_CMP_RUNS, L, cy, CW, B - cy);
        place(hCmpCmpList, L + 6, cy + 6, CW - 12, B - cy - 12);
    }
    {
    const int T = kTabBarH + 14 + 44;  // the Session page starts under the pills
    place(hCmpTitle, L, T, CW - 260, 40);
    place(hCmpTimer, R - 250, T - 6, 250, 72);
    place(hCmpTimerLoad, R - 250, T + 62, 250, 20);  // under the timer
    place(hCmpStatus, L, T + 42, CW - 260, 22);
    // "ZONES" (or "ACTS" on the Act Summary pill) over the pills, the totals under them; pause / stop are in the
    // timer's right-click menu
    place(hLblCmp, L, T + 84, 200, 18);
    gActRowX = L;
    gActRowY = T + 108;
    placeActs();
    place(hCmpSum, L, gActRowY + 42, CW - 140, 22);
    place(hExpCmp, R - 130, gActRowY + 37, 130, rh);  // under the pills: exports the pill chosen
    {
        int cy = gActRowY + 70;
        card(PG_CAMPAIGN, L, cy, CW, B - cy);
        place(hCmpList, L + 6, cy + 6, CW - 12, B - cy - 12);
        place(hCmpActList, L + 6, cy + 6, CW - 12, B - cy - 12);
    }
    }

    // ---- Layouts
    place(hLayImport, R - 436, T, 112, rh);
    place(hLayFolder, R - 150, T, 150, rh);
    place(hLayGraph, R - 316, T, 158, rh);
    place(hGallery, L, T + rh + 12, CW, B - (T + rh + 12));

    // ---- Steps
    layoutStepsPage(L, T, R, B);

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
        const int rows = 11;
        card(PG_EXPERIENCE, L, y, CW, rows * 20 + 2 * pad);
        place(hExpRowLbl, L + pad + 4, y + pad, 190, rows * 20);
        place(hExpInfo, L + pad + 200, y + pad, CW - 2 * pad - 204, rows * 20);
        y += rows * 20 + 2 * pad + 16;
        place(hLvlCmp, L, y, 110, 30);
        place(hLvlMaps, L + 118, y, 90, 30);
        place(hLvlExport, R - 130, y - 1, 130, 32);
        y += 38;
        place(hExpList, L, y, CW, std::max(80, B - y));
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
    place(hCloseChk, L + 220, y, 240, 26);
    if (hUpdChk) place(hUpdChk, L + 480, y, 260, 26);
    place(hExitBtn, R - 140, y - 3, 140, rh);
    y += 44;
    {  // the pills, then the chosen one's options (all pills start at the same y)
        int x = L;
        for (int i = 0; i < 3; ++i) {
            int w = i == 0 ? 110 : i == 1 ? 104 : 90;
            place(hOptPill[i], x, y, w, 30);
            x += w + 8;
        }
        place(hZonesImport, R - 190, y - 1, 190, 32);
    }
    y += 46;
    const int top = y;
    // Overlays: General (all overlays), then one row per overlay with its options on the right
    place(hLblOverlay, L, y, CW, 18);
    y += 28;
    place(hOvGeneralLbl, L, y, 200, 18);
    y += 24;
    place(hAlphaLbl, L, y + 4, 110, 20);
    place(hAlphaSlider, L + 110, y, 260, 26);
    place(hAlphaVal, L + 380, y + 4, 48, 20);
    place(hClickChk, L + 450, y, 260, 26);
    place(hCaptureChk, L + 720, y, 300, 26);
    y += 42;
    place(hOvEachLbl, L, y, 200, 18);
    y += 24;
    const int ox = L + 260;  // where an overlay's options start
    place(hOverlayChk, L, y, 240, 26);
    place(hRecLbl, ox, y + 4, 110, 20);
    place(hRecMinus, ox + 114, y - 3, 34, rh);
    place(hRecVal, ox + 152, y + 4, 34, 20);
    place(hRecPlus, ox + 190, y - 3, 34, rh);
    place(hExpOvChk, ox + 250, y, 190, 26);     // the timer overlay's add-ons: experience lines,
    place(hCmpBestChk, ox + 450, y, 230, 26);   // comparison with the pinned / best run
    y += 40;
    place(hCmdChk, L, y, 240, 26);
    y += 40;
    place(hGraphChk, L, y, 240, 26);
    place(hGraphOnlyChk, ox, y, 200, 26);
    y += 40;
    place(hStepsChk, L, y, 240, 26);
    place(hStepsLbl, ox, y + 4, 110, 20);
    place(hStepsMinus, ox + 114, y - 3, 34, rh);
    place(hStepsVal, ox + 152, y + 4, 34, 20);
    place(hStepsPlus, ox + 190, y - 3, 34, rh);
    place(hNotesChk, ox + 250, y, 160, 26);
    // Hotkeys
    y = top;
    place(hLblKeys, L, y, CW, 18);
    y += 30;
    for (int i = 0; i < HK_COUNT; ++i) {  // campaign + layout keys in one column, steps keys next to the first two
        bool steps = i >= HK_STEP_DONE;
        int x = steps ? L + 320 : L, ky = y + (steps ? i - HK_STEP_DONE : i) * (rh + 6);
        place(hKeyLbl[i], x, ky + 6, 130, 20);
        place(hKeyBtn[i], x + 140, ky, 140, rh);
        if (i == HK_LAYOUT) place(hLayCropChk, L + 300, ky + 3, 460, 26);
    }
    // Theme
    y = top;
    place(hLblTheme, L, y, 240, 18);  // theme and updates side by side
    place(hLblUpd, L + 280, y, CW - 280, 18);
    y += 26;
    place(hThemeSel, L, y, 240, rh);
    place(hUpdInfo, L + 280, y + 6, CW - 280, 20);
    // about the app, at the bottom whatever pill
    y = B - (7 * 20 + 2 * pad);
    card(PG_OPTIONS, L, y, CW, 7 * 20 + 2 * pad);
    place(hOptLbl, L + pad + 4, y + pad, 130, 7 * 20);
    place(hOptInfo, L + pad + 140, y + pad, CW - 2 * pad - 144, 7 * 20);

    fitColumns(hHist, kHistW, 9);
    fitColumns(hSumList, kSumW, 7);
    fitColumns(hCmpList, kCmpW, 11);
    fitColumns(hCmpActList, kCmpActW, 7);
    fitColumns(hExpList, kLvlW, 8);
    InvalidateRect(hMain, nullptr, TRUE);
}

void createControls() {
    makeFonts();

    // tab bar
    hTabGen = btn(L"Maps", ID_TAB_GEN, -1, Btn::Tab);
    hRunsLoadChk = btn(L"Include loading time", ID_RUNS_LOAD, PG_CMP_RUNS, Btn::Check);
    hExpRuns = btn(L"Export to CSV", ID_EXP_RUNS, PG_CMP_RUNS, Btn::Accent);
    hRunsImport = btn(L"Import from file...", ID_RUNS_IMPORT, PG_CMP_RUNS, Btn::Normal);
    hExpRunCmp = btn(L"Export to CSV", ID_EXP_RUNCMP, PG_CMP_RUNS, Btn::Accent);
    gRunsLoad = GetPrivateProfileIntW(L"CampaignRuns", L"WithLoading", 0, iniPath().c_str()) != 0;
    hCmpSubSession = btn(L"Session", ID_TAB_CSES, -1, Btn::Pill);
    hCmpSubRuns = btn(L"Summary", ID_TAB_CRUNS, -1, Btn::Pill);
    hLblRuns = label(L"SAVED RUNS", ID_LBL_CMP, fSection, PG_CMP_RUNS, false, SS_NOTIFY);
    hLblCmpCmp = label(L"COMPARISON", ID_LBL_CMP, fSection, PG_CMP_RUNS, false, SS_NOTIFY);
    for (HWND h : {hLblRuns, hLblCmpCmp})  // how to use the list shows when hovering its title
        gHdrTipOld = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)HdrTipProc);
    {
        const wchar_t* cc2[] = {L"Run", L"Campaign"};
        const int cw2[] = {220, 110};
        hCmpCmpList = list(ID_CMP_CMPLIST, PG_CMP_RUNS, cc2, cw2, 2, 0b10);
    }
    {
        const wchar_t* rc[] = {L"Run", L"Campaign"};
        const int rw[] = {220, 110};
        hCmpRunList = list(ID_CMP_RUNLIST, PG_CMP_RUNS, rc, rw, 2, 0b10);
        // names can be edited (Rename), several runs selected (Ctrl + click) for the comparison
        SetWindowLongPtrW(hCmpRunList, GWL_STYLE,
                          (GetWindowLongPtrW(hCmpRunList, GWL_STYLE) | LVS_EDITLABELS) & ~(LONG_PTR)LVS_SINGLESEL);
    }
    hSubSession = btn(L"Session", ID_TAB_SES, -1, Btn::Pill);
    hSubSummary = btn(L"Summary", ID_TAB_SUM, -1, Btn::Pill);
    hTabCmp = btn(L"Campaign", ID_TAB_CMP, -1, Btn::Tab);
    hTabExp = btn(L"Experience", ID_TAB_EXP, -1, Btn::Tab);
    hTabLay = btn(L"Layouts", ID_TAB_LAY, -1, Btn::Tab);
    hTabSteps = btn(L"Steps", ID_TAB_STEPS, -1, Btn::Tab);
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
    const wchar_t* hc[] = {L"#",       L"Map",  L"Area Lvl", L"Area ID", L"Seed",
                           L"Entered", L"Load", L"Map time", L"Entries"};
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
    hCmpTimerLoad = mk(L"STATIC", L"", SS_RIGHT | SS_NOPREFIX, ID_CMP_TIMER_LOAD, fNormal, PG_CAMPAIGN);
    hExpCmp = btn(L"Export to CSV", ID_EXP_CMP, PG_CAMPAIGN, Btn::Accent);
    hLblCmp = label(L"ZONES", ID_LBL_CMP, fSection, PG_CAMPAIGN);
    hCmpSum = label(L"", ID_CMP_SUM, fBold, PG_CAMPAIGN, false, SS_ENDELLIPSIS);
    const wchar_t* cc[] = {L"#",       L"Zone",       L"Area Lvl", L"Area ID", L"Seed", L"Start", L"Last entered",
                           L"Entries", L"Total time", L"Load",     L"Avg load"};
    hCmpList = list(ID_CMP_LIST, PG_CAMPAIGN, cc, kCmpW, 11, 0b11110010100);
    const wchar_t* ca[] = {L"Act", L"Time", L"Start", L"End", L"Load", L"Zones", L"Avg / zone"};
    hCmpActList = list(ID_CMP_ACTLIST, PG_CAMPAIGN, ca, kCmpActW, 7, 0b1110010);
    {  // shown instead of the zone list, not with the page (campaignShowLists)
        auto& pg = gPages[PG_CAMPAIGN];
        pg.erase(std::remove(pg.begin(), pg.end(), hCmpActList), pg.end());
        ShowWindow(hCmpActList, SW_HIDE);
    }
    for (HWND lv : {hHist, hSumList, hCmpList}) addSearchBox(lv);

    // Options
    hLblPath = label(L"LOG FILE  (LatestClient.txt)", ID_LBL_PATH, fSection, PG_OPTIONS);
    hPath = mk(L"EDIT", gLogPath.c_str(), ES_AUTOHSCROLL | WS_TABSTOP, ID_PATH, fNormal, PG_OPTIONS);
    hBrowse = btn(L"Browse...", ID_BROWSE, PG_OPTIONS, Btn::Normal);
    hReload = btn(L"Load / Reload", ID_RELOAD, PG_OPTIONS, Btn::Accent);
    hOnTop = btn(L"Always on top", ID_ONTOP, PG_OPTIONS, Btn::Check);
    hCloseChk = btn(L"Start hidden in tray", ID_CLOSE_CHK, PG_OPTIONS, Btn::Check);
    hExitBtn = btn(L"Exit app", ID_EXIT, PG_OPTIONS, Btn::Danger);
    hLblOverlay =
        label(L"OVERLAYS   \u00b7   shown only while Path of Exile 2 is open", ID_LBL_OVERLAY, fSection, PG_OPTIONS);
    hOverlayChk = btn(L"MapTimerOverlay", ID_OVERLAY, PG_OPTIONS, Btn::Check);
    hRecLbl = label(L"Recent records", ID_REC_LBL, fNormal, PG_OPTIONS);
    hRecMinus = btn(L"\u2212", ID_REC_MINUS, PG_OPTIONS, Btn::Normal);
    hRecVal = label(L"3", ID_REC_VAL, fBold, PG_OPTIONS, false, SS_CENTER);
    hRecPlus = btn(L"+", ID_REC_PLUS, PG_OPTIONS, Btn::Normal);
    hCmdChk = btn(L"CommandBarOverlay", ID_CMDBAR, PG_OPTIONS, Btn::Check);
    hGraphChk = btn(L"Graph / outline overlay", ID_GRAPH_OV, PG_OPTIONS, Btn::Check);
    hGraphOnlyChk = btn(L"Only graphs", ID_GRAPH_ONLY, PG_OPTIONS, Btn::Check);
    hStepsChk = btn(L"StepsOverlay", ID_STEPS_OV, PG_OPTIONS, Btn::Check);
    hStepsLbl = label(L"Steps shown", ID_STEPS_LBL, fNormal, PG_OPTIONS);
    hStepsMinus = btn(L"\u2212", ID_STEPS_MINUS, PG_OPTIONS, Btn::Normal);
    hStepsVal = label(L"5", ID_STEPS_VAL, fBold, PG_OPTIONS, false, SS_CENTER);
    hStepsPlus = btn(L"+", ID_STEPS_PLUS, PG_OPTIONS, Btn::Normal);
    hAlphaLbl = label(L"Transparency", ID_ALPHA_LBL, fNormal, PG_OPTIONS);
    hAlphaSlider = slider(ID_ALPHA_SLIDER, PG_OPTIONS, 0, 100);
    hAlphaVal = label(L"10%", ID_ALPHA_VAL, fBold, PG_OPTIONS, false, SS_LEFT);
    hOvGeneralLbl = label(L"General", ID_OV_GENERAL, fBold, PG_OPTIONS);
    hOvEachLbl = label(L"Overlays", ID_OV_EACH, fBold, PG_OPTIONS);
    hClickChk = btn(L"Click-through  (hold Alt to use)", ID_CLICKTHRU, PG_OPTIONS, Btn::Check);
    hCaptureChk = btn(L"Hide overlays in screenshots", ID_CAPTURE_CHK, PG_OPTIONS, Btn::Check);
    // Experience
    {
        const wchar_t* t[6] = {L"CHARACTER NAME",      L"LEVEL",
                               L"AREA LEVEL",          L"TABLET EFFECTIVENESS %",
                               L"MAP EFFECTIVENESS %", L"TABLET EXP GAIN %"};
        for (int i = 0; i < 6; ++i) hExpCap[i] = label(t[i], ID_EXP_LBLS, fSection, PG_EXPERIENCE);
        hExpModsLbl = label(L"TABLETS / MAP MODS", ID_EXP_LBLS, fSection, PG_EXPERIENCE);
    }
    hExpName = mk(L"EDIT", gExpName.c_str(), ES_AUTOHSCROLL | WS_TABSTOP, ID_EXP_NAME, fNormal, PG_EXPERIENCE);
    hExpLevel = mk(L"EDIT", gExpLevel ? std::to_wstring(gExpLevel).c_str() : L"", ES_NUMBER | WS_TABSTOP, ID_EXP_LEVEL,
                   fNormal, PG_EXPERIENCE);
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
        L"Required exp to level up\r\nSafe range\r\nMonster base exp\r\nEffectiveness\r\nPenalty multiplier\r\n"
        L"Experience gain\r\nExpected exp\r\nBest area\r\nLast level up\r\nTime between level ups\r\nExp per hour",
        ID_EXP_ROWLBL, fNormal, PG_EXPERIENCE, true);
    hExpInfo = label(L"", ID_EXP_INFO, fNormal, PG_EXPERIENCE, true);
    hExpOvChk = btn(L"Experience AddOn", ID_EXP_OVCHK, PG_OPTIONS, Btn::Check);  // Options -> Overlays, MapTimerOverlay row
    {
        const wchar_t* lc[] = {L"#",          L"Zone",        L"Area Lvl",   L"Lvl before",
                               L"Lvl after",  L"Time to lvl", L"Exp/h",      L"Leveled at"};
        hLvlCmp = btn(L"Campaign", ID_LVL_CMP, PG_EXPERIENCE, Btn::Pill);
        hLvlMaps = btn(L"Maps", ID_LVL_MAPS, PG_EXPERIENCE, Btn::Pill);
        hLvlExport = btn(L"Export to CSV", ID_LVL_EXPORT, PG_EXPERIENCE, Btn::Accent);
        hExpList = list(ID_EXP_LIST, PG_EXPERIENCE, lc, kLvlW, 8, 0b01111101);
    }
    hExpHint = label(L"Enter your character name and the level updates by itself on every level up.", ID_EXP_HINT,
                     fNormal, PG_EXPERIENCE);

    hLblKeys =
        label(L"HOTKEYS   \u00b7   campaign timer, layout screenshots and steps, work while the game or this app is in front",
              ID_LBL_KEYS, fSection, PG_OPTIONS);
    hLayCropChk =
        btn(L"Select the area (snipping tool) instead of the whole game window", ID_LAY_CROP, PG_OPTIONS, Btn::Check);
    {
        const wchar_t* names[HK_COUNT] = {L"Stop",         L"Pause / resume", L"Start new run", L"Layout screenshot",
                                          L"Complete step", L"Undo step"};
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
    hUpdInfo = label((L"Version v" + W(APP_VERSION) +
                      L"  \u00b7  self-update is off in this build: set GITHUB_REPO=owner/repo "
                      L"in .env and build Release")
                         .c_str(),
                     ID_UPD_INFO, fNormal, PG_OPTIONS);
#endif
    hLoadHist = btn(L"Load past Map session events...", ID_LOAD_HIST, PG_GENERAL, Btn::Accent);
    hRestoreGen = btn(L"Restore map session...", ID_RESTORE_GEN, PG_GENERAL, Btn::Normal);
    hOptLbl =
        label(L"Status\r\nRead\r\nLog session\r\nCurrent area\r\nMap session\r\nCampaign session\r\nSummary session",
              ID_OPT_LBL, fNormal, PG_OPTIONS, true);
    hOptInfo = label(L"", ID_OPT_INFO, fNormal, PG_OPTIONS, true);
    createLayoutsPage();
    createStepsPage();

    hNotesChk = btn(L"Show notes", ID_STEPS_NOTES, PG_OPTIONS, Btn::Check);  // StepsOverlay row
    hCmpBestChk = btn(L"Time Comparison AddOn", ID_OV_COMPARE, PG_OPTIONS, Btn::Check);
    gOvCompare = GetPrivateProfileIntW(L"MapTimerOverlay", L"CompareBest", 0, iniPath().c_str()) != 0;
    hZonesImport = btn(L"Load zone names...", ID_ZONES_IMPORT, PG_OPTIONS, Btn::Normal);
    {
        const wchar_t* pills[3] = {L"Overlays", L"Hotkeys", L"Theme"};
        for (int i = 0; i < 3; ++i) hOptPill[i] = btn(pills[i], ID_OPT_PILL0 + i, PG_OPTIONS, Btn::Pill);
        for (HWND h : {hLblOverlay, hOvGeneralLbl, hAlphaLbl, hAlphaSlider, hAlphaVal, hClickChk, hCaptureChk, hOvEachLbl,
                       hOverlayChk, hRecLbl, hRecMinus, hRecVal, hRecPlus, hCmdChk, hGraphChk, hGraphOnlyChk, hStepsChk,
                       hStepsLbl, hStepsMinus, hStepsVal, hStepsPlus, hNotesChk, hExpOvChk, hCmpBestChk})
            gOptGroup[h] = 0;
        gOptGroup[hLblKeys] = 1;
        gOptGroup[hLayCropChk] = 1;
        for (int i = 0; i < HK_COUNT; ++i) gOptGroup[hKeyLbl[i]] = gOptGroup[hKeyBtn[i]] = 1;
        for (HWND h : {hLblTheme, hThemeSel, hLblUpd, hUpdInfo}) gOptGroup[h] = 2;
        gOptPill = std::clamp((int)GetPrivateProfileIntW(L"main", L"OptionsPill", 0, iniPath().c_str()), 0, 2);
    }

    for (HWND lv : {hHist, hSumList, hCmpList, hCmpActList, hExpList}) {
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
