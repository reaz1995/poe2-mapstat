// App-wide state: window and control handles, fonts, the tracker and flags shared by the modules.
#include "app.h"

namespace C {
COLORREF Bg, Card, Darkest, Line, Stripe, Fg, Comment, Cyan, Green, Orange, Pink, Purple, Red, Yellow;
}  // namespace C

Theme gTheme;              // active theme

std::string gThemeKey;     // embedded theme file name ("" = default Dracula)

std::wstring gFontUi = L"Segoe UI", gFontMono = L"Consolas";

HBRUSH brBg, brCard, brDarkest;

HWND hMain;

HWND hTabGen, hTabCmp, hGear;
HWND hSubSession, hSubSummary;

HWND hTitle, hStatus, hTimer, hDetLbl, hDetails, hSummary, hHist, hLblHist;

HWND hGenReset, hExpGen;

HWND hSumHdr, hSumList, hSumReset, hExpSum, hLblSum;

HWND hCmpTitle, hCmpTimer, hCmpStatus, hCmpList, hCmpReset, hExpCmp, hLblCmp, hCmpSum;

HWND hLblPath, hPath, hBrowse, hReload, hOnTop, hOptInfo, hOptLbl, hLoadHist, hRestoreGen, hRestoreCmp;

HFONT fNormal, fBold, fTitle, fTimer, fSection, fTab;

std::vector<HWND> gPages[PG_COUNT];

std::vector<RECT> gCards[PG_COUNT];   // rounded panels painted behind controls

std::vector<HWND> gOnCard;            // statics drawn on a card (card background)

int gPage = PG_GENERAL;

HWND gHover = nullptr;

bool gOnTopOn = false;

bool gOverlayOn = false;

bool gStartInTray = true, gExiting = false;

UINT gTaskbarCreated = 0;  // explorer restarted -> add the tray icon again

HWND hLblOverlay, hRecLbl, hRecMinus, hRecVal, hRecPlus, hCloseChk, hExitBtn;
HWND hAlphaLbl, hAlphaMinus, hAlphaVal, hAlphaPlus;

HWND hCmdChk, hCmdBar = nullptr;

HWND hLblTheme, hThemeSel;

HWND hLblUpd = nullptr, hUpdChk = nullptr, hUpdInfo = nullptr, hUpdTab = nullptr;

bool gAutoUpdate = true;
HWND hCmpTimerLoad = nullptr;
HWND hCmpPause = nullptr, hCmpStop = nullptr;
HWND hLblKeys = nullptr, hKeyLbl[3] = {}, hKeyBtn[3] = {}, hKeysChk = nullptr;
HWND hTabExp, hExpName, hExpLevel, hExpArea, hExpTabEff, hExpMapEff, hExpGain, hExpInfo, hExpRowLbl, hExpOvChk,
    hExpHint;

HANDLE gMutex = nullptr;

bool gGameVisible = false;  // PoE2 window exists and is not minimized

bool gCmdOn = false;

HWND hOverlay = nullptr, hOverlayChk = nullptr;

HFONT fOvHead = nullptr, fOvSmall = nullptr, fOvTiny = nullptr;

std::map<HWND, Btn> gBtnKind;

Tracker g;

std::wstring gLogPath, gExeDir;

uint64_t gOffset = 0;

std::string gHead;  // first bytes of the log: changes when the game starts a new session/file

std::string gPartial;

bool gHistDirty = true, gFileOk = false;

std::vector<std::string> gActs;  // act sub-tabs on the Campaign page

std::vector<HWND> gActBtns;

std::string gSelAct, gCmpShownAct;

std::vector<int> gCmpRows;        // list row -> zone index

int gLastCurZone = -2;

int gHistCurRow = -1;  // Maps list: row of the map being played

int gActRowX = 0, gActRowY = 0;

std::map<HWND, unsigned> gHiddenCols;

int gMapFilter = CAT_ALL;

uint64_t gLinesRead = 0;

std::atomic<int> gImportPct{-1};  // -1 = idle (Load past events)

bool gSkipExisting = true;   // track only new log lines (no replay of what is already in the log)

bool gHistoryLoaded = false;

bool gShrunk = false;

std::wstring gDataDir;  // %APPDATA%\Poe2MapStat - settings, area names, stored runs
