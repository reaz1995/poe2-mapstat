#pragma once
// Shared declarations of the Windows UI (src/app). Each module's functions are listed under its file name.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "theme.h"
#include "tracker.h"
#include "updater.h"

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

// ---------------------------------------------------------------- types, ids, constants
namespace C {  // theme colours (theme_ui.cpp)
extern COLORREF Bg, Card, Darkest, Line, Stripe, Fg, Comment, Cyan, Green, Orange, Pink, Purple, Red, Yellow;
}  // namespace C

enum {
    ID_PATH = 101,
    ID_BROWSE,
    ID_RELOAD,
    ID_ONTOP,
    ID_TITLE,
    ID_STATUS,
    ID_TIMER,
    ID_DETLBL,
    ID_DETAILS,
    ID_SUMMARY,
    ID_HIST,
    ID_LBL_HIST,
    ID_TAB_GEN,
    ID_TAB_SUM,
    ID_TAB_SES,
    ID_TAB_CMP,
    ID_GEAR,
    ID_SUMHDR,
    ID_SUMLIST,
    ID_LBL_PATH,
    ID_OPT_INFO,
    ID_EXP_GEN,
    ID_EXP_SUM,
    ID_EXP_CMP,
    ID_GEN_RESET,
    ID_SUM_RESET,
    ID_CMP_TITLE,
    ID_CMP_TIMER,
    ID_CMP_STATUS,
    ID_CMP_LIST,
    ID_CMP_RESET,
    ID_LBL_CMP,
    ID_LBL_SUM,
    ID_CMP_SUM,
    ID_OPT_LBL,
    ID_LOAD_HIST,
    ID_RESTORE_GEN,
    ID_RESTORE_CMP,
    ID_OVERLAY,
    ID_LBL_OVERLAY,
    ID_REC_LBL,
    ID_REC_MINUS,
    ID_REC_VAL,
    ID_REC_PLUS,
    ID_ALPHA_LBL,
    ID_ALPHA_MINUS,
    ID_ALPHA_VAL,
    ID_ALPHA_PLUS,
    ID_CLOSE_CHK,
    ID_EXIT,
    ID_CMDBAR,
    ID_LBL_THEME,
    ID_THEME_SEL,
    ID_LBL_UPD,
    ID_UPD_CHK,
    ID_UPD_INFO,
    ID_UPD_TAB,
    ID_CMP_TIMER_LOAD,
    ID_CMP_PAUSE,
    ID_CMP_STOP,
    ID_LBL_KEYS,
    ID_KEY_LBL0,
    ID_KEY_LBL1,
    ID_KEY_LBL2,
    ID_KEY_LBL3,
    ID_KEY_BTN0,
    ID_KEY_BTN1,
    ID_KEY_BTN2,
    ID_KEY_BTN3,
    ID_KEYS_CHK,
    ID_TAB_EXP,
    ID_EXP_NAME,
    ID_EXP_LEVEL,
    ID_EXP_AREA,
    ID_EXP_TAB_EFF,
    ID_EXP_MAP_EFF,
    ID_EXP_GAIN,
    ID_EXP_LBLS,
    ID_EXP_INFO,
    ID_EXP_ROWLBL,
    ID_EXP_OVCHK,
    ID_EXP_HINT,
    ID_TAB_LAY,
    ID_LAY_HINT,
    ID_LAY_FOLDER,
    ID_ACT_FIRST = 2000,
    ID_TIMER_POLL = 1, ID_TIMER_UPD = 2
};

// Page indices
enum { PG_GENERAL = 0, PG_SUMMARY = 1, PG_CAMPAIGN = 2, PG_OPTIONS = 3, PG_EXPERIENCE = 4, PG_LAYOUTS = 5, PG_COUNT };

static const wchar_t* kDefaultLog =
    L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Path of Exile 2\\logs\\LatestClient.txt";

static const UINT WM_TRAY = WM_APP + 1, WM_SHOWMAIN = WM_APP + 2;

// button looks
enum class Btn { Normal, Danger, Accent, Tab, Gear, Check, Pill };

// Maps tab filter by map category (right-click on the Map column)
enum { CAT_ALL = 0, CAT_MAPS, CAT_EXPEDITION, CAT_SPECIAL };

static const wchar_t* kCatName[] = {L"All", L"Maps", L"Expedition", L"Special"};

static const UINT WM_IMPORTED = WM_APP + 3;
static const UINT WM_HOTKEY_ACT = WM_APP + 21, WM_KEY_CAPTURED = WM_APP + 22;

// Stored data: three kinds of files in %APPDATA%\Poe2MapStat\sessions, named by prefix + start date:
//   "MapSession <date>.tsv"       maps of the Maps tab
//   "CampaignSession <date>.tsv"  campaign zones
//   "SummarySession <date>.tsv"   what Summary counts
// A reset keeps the old file (state before the reset) and continues in a new one, so each part can be restored.
struct SessionFile {
    const wchar_t* prefix;   // file name prefix
    const wchar_t* key;      // settings.ini [main] key
    int parts;               // Tracker::Parts stored in it
    std::wstring path;
};

// ---- sortable list filling. Rows are rebuilt from data every refresh, so sorting is applied here.
struct LvRow { std::vector<std::wstring> c; int tag; };

struct SortState { int col = -1; bool desc = false; };

// Relative column widths; columns are stretched to fill the list width on every resize.
static const int kHistW[] = {35, 140, 70, 190, 100, 75, 60, 75, 60};

static const int kSumW[] = {170, 70, 210, 70, 100, 90, 80};

static const int kCmpW[] = {36, 220, 70, 95, 100, 85, 100, 65, 90, 70, 75};

// ---- column menu: right click on a header or a row -> Hide <column> / Unhide all
struct MenuItem { std::wstring text; bool separator; };

static const int kTabBarH = 42;

static const wchar_t* kCbSec = L"CommandBarOverlay";

// Client.txt is the game's log of all sessions and can be huge: it is streamed in chunks and fed line by
// line on a worker thread (own Tracker, nothing shared), and for "last N days" the start is found by a
// binary search on the line dates, so only the needed tail is read.
struct ImportResult {
    std::string data, firstDate, err, source;
    std::map<std::string, std::string> names;
    std::map<std::string, std::pair<int, int>> levels;  // campaign area level ranges seen
    uint64_t lines = 0;
};

enum { ID_RG_OK = 3001, ID_RG_CANCEL, ID_RG_BROWSE };

static const UINT WM_UPD_FOUND = WM_APP + 4, WM_UPD_DOWNLOADED = WM_APP + 5;

struct UpdResult { bool ok = false, manual = false; ReleaseInfo rel; std::string data, err; };

// ---------------------------------------------------------------- shared state (defined in the module that owns it)
extern Theme gTheme;  // active theme
extern std::string gThemeKey;  // embedded theme file name ("" = default Dracula)
extern std::wstring gFontUi, gFontMono;
extern HBRUSH brBg, brCard, brDarkest;
extern HWND hMain;
extern HWND hTabGen, hTabCmp, hTabLay, hGear;
extern HWND hSubSession, hSubSummary;  // Maps tab: Session / Summary sub-tabs
extern HWND hTitle, hStatus, hTimer, hDetLbl, hDetails, hSummary, hHist, hLblHist;
extern HWND hGenReset, hExpGen;
extern HWND hSumHdr, hSumList, hSumReset, hExpSum, hLblSum;
extern HWND hCmpTitle, hCmpTimer, hCmpStatus, hCmpList, hCmpReset, hExpCmp, hLblCmp, hCmpSum;
extern HWND hLblPath, hPath, hBrowse, hReload, hOnTop, hOptInfo, hOptLbl, hLoadHist, hRestoreGen, hRestoreCmp;
extern HFONT fNormal, fBold, fTitle, fTimer, fSection, fTab;
extern std::vector<HWND> gPages[PG_COUNT];
extern std::vector<RECT> gCards[PG_COUNT];  // rounded panels painted behind controls
extern std::vector<HWND> gOnCard;  // statics drawn on a card (card background)
extern int gPage;
extern HWND gHover;
extern bool gOnTopOn;
extern bool gOverlayOn;
extern bool gStartInTray, gExiting;
extern UINT gTaskbarCreated;  // explorer restarted -> add the tray icon again
extern HWND hLblOverlay, hRecLbl, hRecMinus, hRecVal, hRecPlus, hCloseChk, hExitBtn;
extern HWND hAlphaLbl, hAlphaMinus, hAlphaVal, hAlphaPlus;
extern HWND hCmdChk, hCmdBar;
extern HWND hLblTheme, hThemeSel;
extern HWND hLblUpd, hUpdChk, hUpdInfo, hUpdTab;
extern bool gAutoUpdate;
extern HWND hCmpTimerLoad;  // Campaign: zone time including its loading screens
extern HWND hCmpPause, hCmpStop;
extern HWND hLblKeys, hKeyLbl[4], hKeyBtn[4], hKeysChk;
extern bool gHotkeysOn;  // Options: hotkeys enabled
extern HWND hTabExp, hExpName, hExpLevel, hExpArea, hExpTabEff, hExpMapEff, hExpGain, hExpInfo, hExpRowLbl, hExpOvChk,
    hExpHint;
extern std::wstring gExpName;
extern int gExpLevel;
extern int gTabEff, gMapEff, gExpGain;  // Experience tab: tablet / map effectiveness and tablet exp gain, in %
extern bool gExpOnOverlay;
extern HANDLE gMutex;
extern bool gGameVisible;  // PoE2 window exists and is not minimized
extern bool gCmdOn;
extern HWND hOverlay, hOverlayChk;
extern HFONT fOvHead, fOvSmall, fOvTiny;
extern std::map<HWND, Btn> gBtnKind;
extern Tracker g;
extern std::wstring gLogPath, gExeDir;
extern uint64_t gOffset;
extern std::string gHead;  // first bytes of the log: changes when the game starts a new session/file
extern std::string gPartial;
extern bool gHistDirty, gFileOk;
extern std::vector<std::string> gActs;  // act sub-tabs on the Campaign page
extern std::vector<HWND> gActBtns;
extern std::string gSelAct, gCmpShownAct;
extern std::vector<int> gCmpRows;  // list row -> zone index
extern int gLastCurZone;
extern int gHistCurRow;  // Maps list: row of the map being played
extern int gActRowX, gActRowY;
extern std::map<HWND, unsigned> gHiddenCols;
extern int gMapFilter;
extern uint64_t gLinesRead;
extern std::atomic<int> gImportPct;  // -1 = idle (Load past events)
extern bool gSkipExisting;  // track only new log lines (no replay of what is already in the log)
extern bool gHistoryLoaded;
extern bool gShrunk;
extern std::wstring gDataDir;  // %APPDATA%\Poe2MapStat - settings, area names, stored runs
extern SessionFile gMapFile;
extern SessionFile gCmpFile;
extern SessionFile gSumFile;
extern std::map<HWND, SortState> gSort;
extern std::vector<MenuItem> gMenuItems;  // owner-draw item data (kept alive while the menu is open)
extern double gOvScale;
extern int gOvRecords;
extern int gOvTransparency;  // overlay transparency in percent (0 = solid, 100 = invisible)

// ---------------------------------------------------------------- util.cpp
std::wstring W(const std::string& s);
std::string U8(const std::wstring& w);
void setTextIfChanged(HWND h, const std::wstring& t);
std::wstring iniPath();
std::string areasPath();
std::string nowClock();
uint32_t nowTick();
std::wstring lowerW(std::wstring s);
std::wstring secs(double s);
std::string readFileUtf8(const std::wstring& path);
bool fileExists(const std::wstring& p);
std::wstring regString(HKEY root, const wchar_t* key, const wchar_t* value);

// ---------------------------------------------------------------- theme_ui.cpp
void initDarkMode();
void darkControl(HWND h);
void darkTitleBar(HWND h);
HFONT font(int px, int weight, const wchar_t* face);
void makeFonts();
void loadStartupTheme();
void updateThemeLabel();
void setThemeColors();
void themeMenu();

// ---------------------------------------------------------------- sessions.cpp
std::wstring newSessionPath(const SessionFile& sf, const std::wstring& date = L"", const std::wstring& suffix = L"");
std::wstring fileName(const std::wstring& p);
void setSessionFile(SessionFile& sf, const std::wstring& p);
void initSessionFiles();
void saveFile(const SessionFile& sf);
void saveData();
void loadData();
bool ask(const wchar_t* title, const wchar_t* text);
void resetSummaryData();
void resetGeneralSession();
void restoreSession(SessionFile& sf);
void resetCampaignRun();
void startCampaignRun();  // new run without asking (hotkey / Start after Stop)

// ---------------------------------------------------------------- csv_export.cpp
void exportListView(HWND lv, const wchar_t* defName);
std::wstring exportName(const wchar_t* what);

// ---------------------------------------------------------------- log_reader.cpp
void pollLog();
void fullReload();
void browse();
void applyPath();
void resolveLogPath();

// ---------------------------------------------------------------- lists.cpp
bool searchMatch(HWND lv, const std::wstring& name, const std::wstring& id);
void sortRows(HWND lv, std::vector<LvRow>& rows);
void writeRows(HWND lv, const std::vector<LvRow>& rows);
void fitColumns(HWND lv, const int* weights, int n);
const wchar_t* listKey(HWND lv);
void saveColumnOrder(HWND lv);
void loadColumnOrder(HWND lv);
void addSearchBox(HWND lv);
void showColumnMenu(HWND lv, int col);
LRESULT listCustomDraw(NMLVCUSTOMDRAW* cd);
HWND list(int id, int page, const wchar_t* const* cols, const int* widths, int n, unsigned rightMask);

// ---------------------------------------------------------------- widgets.cpp
void place(HWND h, int x, int y, int w, int hgt);
RECT card(int page, int x, int y, int w, int h);
void fillRound(HDC dc, const RECT& r, int rad, COLORREF fill, COLORREF border);
void paintMain(HDC dc, const RECT& client);
void drawButton(const DRAWITEMSTRUCT* d);
LRESULT CALLBACK BtnProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR);
void measureMenu(MEASUREITEMSTRUCT* m);
void drawMenu(const DRAWITEMSTRUCT* d);
HWND mk(const wchar_t* cls, const wchar_t* txt, DWORD style, int id, HFONT f, int page, DWORD ex = 0);
HWND btn(const wchar_t* txt, int id, int page, Btn kind);
HWND label(const wchar_t* txt, int id, HFONT f, int page, bool onCard = false, DWORD extra = 0);
COLORREF staticColor(HWND c);

// ---------------------------------------------------------------- page_general.cpp
bool unnamed(const std::string& name, const std::string& id);
std::wstring areaName(const std::string& name, const std::string& id);
void refreshUi();

// ---------------------------------------------------------------- page_summary.cpp
void refreshSummary();
void hideTip();
void summaryHover(HWND lv, LPARAM lp);

// ---------------------------------------------------------------- hotkeys.cpp
enum { HK_STOP, HK_PAUSE, HK_START, HK_LAYOUT, HK_COUNT };
void loadHotkeys();
void installKeyHook();
void removeKeyHook();
std::wstring hotkeyName(int i);
void beginKeyCapture(int i);
void keyCaptured(UINT vk, int mods);
void campaignAction(int i);
void toggleHotkeys();

// ---------------------------------------------------------------- page_experience.cpp
int expAreaLevel();
int expArea();
double expEffMult();  // effectiveness (tablets + map) as an experience multiplier, applied to the monster base exp
double expGainMult();  // increased experience gain as a multiplier, applied after the level penalty
bool expNoMonsters();
std::wstring expPct(double m);
void loadExperience();
void refreshExperience();
void expEditChanged(HWND h);
void toggleExpOverlay();

// ---------------------------------------------------------------- page_campaign.cpp
void selectAct(const std::string& act);
void refreshCampaign(bool force);
void placeActs();

// ---------------------------------------------------------------- page_layouts.cpp
extern HWND hLayHint, hLayFolder, hGallery;
void createLayoutsPage();
void refreshLayouts();  // rescan the folders (page shown, hotkey changed)
void openLayoutFolder();
void takeLayoutShot();  // hotkey: Windows snipping, the picture is saved to the current zone
void layoutClipboardUpdate();

// ---------------------------------------------------------------- layout.cpp
void showMapsPage();  // Maps tab: the sub-page shown last (Session or Summary)
void showPage(int p);
void layout(int w, int h);
void createControls();

// ---------------------------------------------------------------- overlay_common.cpp
void dragStart(HWND self);
void snapOverlay(HWND self, RECT* r);
HWND findGame();
bool gameClientRect(RECT& r);
void anchorSave(HWND ov, const wchar_t* sec);
void updateOverlayVisibility();
void overlayMenu(HWND ov);

// ---------------------------------------------------------------- overlay_timer.cpp
int ovIniInt(const wchar_t* key, int def);
void ovIniSet(const wchar_t* key, int v);
void ovFonts(double sc);
void saveOverlayPos();
void setOverlayRecords(int n);
void setOverlayTransparency(int pct);
BYTE overlayAlpha();
void ovSyncFooter();
bool ovCampaignShown();  // the timer overlay shows the campaign (footer visible)
void showOverlay(bool on);

// ---------------------------------------------------------------- overlay_cmdbar.cpp
void showCmdBar(bool on);

// ---------------------------------------------------------------- import.cpp
void setImportButton();
void importDone(ImportResult* r);
void pastEventsMenu();

// ---------------------------------------------------------------- update_ui.cpp
#ifdef UPDATE_REPO
void updCheck(bool manual);
void updFound(UpdResult* r);
void updInstall(UpdResult* r);
#endif

// ---------------------------------------------------------------- tray.cpp
void showMain();
void trayIcon(DWORD op);
void trayMenu();
void trayBalloon(const std::wstring& title, const std::wstring& text);

// ---------------------------------------------------------------- main.cpp
void tick();
