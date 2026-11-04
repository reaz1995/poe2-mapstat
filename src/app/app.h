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

#include "steps.h"
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
    ID_TAB_CSES,
    ID_TAB_CRUNS,
    ID_CMP_RUNLIST,
    ID_CMP_CMPLIST,
    ID_RUNS_LOAD,
    ID_OV_COMPARE,
    ID_TAB_CMP,
    ID_GEAR,
    ID_SUMHDR,
    ID_SUMLIST,
    ID_LBL_PATH,
    ID_OPT_INFO,
    ID_EXP_GEN,
    ID_EXP_SUM,
    ID_EXP_CMP,
    ID_EXP_RUNS,
    ID_RUNS_IMPORT,
    ID_EXP_RUNCMP,
    ID_GEN_RESET,
    ID_SUM_RESET,
    ID_CMP_TITLE,
    ID_CMP_TIMER,
    ID_CMP_STATUS,
    ID_CMP_LIST,
    ID_CMP_ACTLIST,
    ID_LBL_CMP,
    ID_LBL_SUM,
    ID_CMP_SUM,
    ID_OPT_LBL,
    ID_LOAD_HIST,
    ID_RESTORE_GEN,
    ID_OVERLAY,
    ID_LBL_OVERLAY,
    ID_REC_LBL,
    ID_REC_MINUS,
    ID_REC_VAL,
    ID_REC_PLUS,
    ID_ALPHA_LBL,
    ID_ALPHA_SLIDER,
    ID_ALPHA_VAL,
    ID_OV_GENERAL,
    ID_CAPTURE_CHK,
    ID_OV_EACH,
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
    ID_LBL_KEYS,
    ID_TAB_EXP,
    ID_LVL_CMP,
    ID_LVL_MAPS,
    ID_LVL_EXPORT,
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
    ID_EXP_LIST,
    ID_TAB_LAY,
    ID_LAY_FOLDER,
    ID_LAY_CROP,
    ID_LAY_GRAPH,
    ID_GRAPH_OV,
    ID_GRAPH_ONLY,
    ID_CLICKTHRU,
    ID_TAB_STEPS,
    ID_LAY_IMPORT,
    ID_STEPS_EDIT,
    ID_STEPS_LIST,
    ID_STEPS_IMPORT,
    ID_STEPS_SAVE,
    ID_STEPS_SAVEAS,
    ID_STEPS_RESET,
    ID_STEPS_INFO,
    ID_STEPS_OV,
    ID_STEPS_NOTES,
    ID_OPT_PILL0,
    ID_OPT_PILL1,
    ID_OPT_PILL2,
    ID_STEPS_LBL,
    ID_STEPS_MINUS,
    ID_STEPS_VAL,
    ID_STEPS_PLUS,
    ID_KEY_LBL0 = 1600,  // + hotkey index
    ID_KEY_BTN0 = 1620,  // + hotkey index
    ID_ACT_FIRST = 2000,
    ID_TIMER_POLL = 1,
    ID_TIMER_UPD = 2,
    ID_TIMER_CLICK = 3
};

// Page indices
enum {
    PG_GENERAL = 0,
    PG_SUMMARY = 1,
    PG_CAMPAIGN = 2,
    PG_OPTIONS = 3,
    PG_EXPERIENCE = 4,
    PG_LAYOUTS = 5,
    PG_STEPS = 6,
    PG_CMP_RUNS = 7,  // Campaign tab, Summary pill: the saved runs
    PG_COUNT
};

static const wchar_t* kDefaultLog =
    L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Path of Exile 2\\logs\\LatestClient.txt";

static const UINT WM_TRAY = WM_APP + 1, WM_SHOWMAIN = WM_APP + 2;

// button looks
enum class Btn { Normal, Danger, Accent, Tab, Gear, Check, Pill };

// Maps tab filter by map category (right-click on the Map column)
enum { CAT_ALL = 0, CAT_MAPS, CAT_EXPEDITION, CAT_SPECIAL };

static const wchar_t* kCatName[] = {L"All", L"Maps", L"Expedition", L"Special"};

static const UINT WM_IMPORTED = WM_APP + 3;
static const UINT WM_HOTKEY_ACT = WM_APP + 21, WM_KEY_CAPTURED = WM_APP + 22, WM_LAYOUT_DONE = WM_APP + 23;

// Stored data: three kinds of files in %APPDATA%\Poe2MapStat\sessions, named by prefix + start date:
//   "MapSession <date>.tsv"       maps of the Maps tab
//   "CampaignSession <date>.tsv"  campaign zones
//   "SummarySession <date>.tsv"   what Summary counts
// A reset keeps the old file (state before the reset) and continues in a new one, so each part can be restored.
struct SessionFile {
    const wchar_t* prefix;  // file name prefix
    const wchar_t* key;  // settings.ini [main] key
    int parts;  // Tracker::Parts stored in it
    std::wstring path;
};

// ---- sortable list filling. Rows are rebuilt from data every refresh, so sorting is applied here.
struct LvRow {
    std::vector<std::wstring> c;
    int tag;
};

struct SortState {
    int col = -1;
    bool desc = false;
};

// Relative column widths; columns are stretched to fill the list width on every resize.
static const int kHistW[] = {35, 140, 70, 190, 100, 75, 60, 75, 60};

static const int kSumW[] = {170, 70, 210, 70, 100, 90, 80};

static const int kCmpW[] = {36, 220, 70, 95, 100, 85, 100, 65, 90, 70, 75};

// ---- column menu: right click on a header or a row -> Hide <column> / Unhide all
struct MenuItem {
    std::wstring text;
    bool separator;
};

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

struct UpdResult {
    bool ok = false, manual = false;
    ReleaseInfo rel;
    std::string data, err;
};

// ---------------------------------------------------------------- shared state (defined in the module that owns it)
extern Theme gTheme;  // active theme
extern std::string gThemeKey;  // embedded theme file name ("" = default Dracula)
extern std::wstring gFontUi, gFontMono;
extern HBRUSH brBg, brCard, brDarkest;
extern HWND hMain;
extern HWND hTabGen, hTabCmp, hTabLay, hGear;
extern HWND hSubSession, hSubSummary;  // Maps tab: Session / Summary sub-tabs
extern HWND hCmpSubSession, hCmpSubRuns, hCmpRunList, hLblRuns, hCmpCmpList, hLblCmpCmp, hExpRuns, hExpRunCmp, hRunsImport;  // Campaign tab: Session / Summary (saved runs)
extern HWND hRunsLoadChk;  // Campaign -> Summary: Include loading time
extern bool gRunsLoad;
void showCampaignPage();  // the Campaign tab's last pill
void refreshCampaignRuns();
void importCampaignRun();
LRESULT campaignRunsNotify(NMHDR* nh);
LRESULT campaignCompareNotify(NMHDR* nh);
void invalidateBestZones();
void campaignRunsSaveOrder(HWND lv);  // a column was dragged: both run lists take the new order
int64_t bestZoneMs(const std::string& areaId);  // best time in a zone over the saved runs, -1 if none
std::wstring zoneVsBest(const std::string& areaId, uint32_t now);
// the act before the current one: name, time and "(+3m15s)" against the pinned run or the best; false if none
bool previousAct(uint32_t now, std::wstring& name, std::wstring& time, std::wstring& cmp);
int64_t bestActMs(const std::string& act);  // time to beat for a whole act, -1 if none
std::wstring diffInParens(int64_t ms);       // "(+3m15s)" / "(-20s)"
extern bool gOvCompare;  // Options: MapTimerOverlay compares the zone time with the best run
extern HWND hCmpBestChk;  // Options: Time Comparison AddOn
extern HWND hTitle, hStatus, hTimer, hDetLbl, hDetails, hSummary, hHist, hLblHist;
extern HWND hGenReset, hExpGen;
extern HWND hSumHdr, hSumList, hSumReset, hExpSum, hLblSum;
extern HWND hCmpTitle, hCmpTimer, hCmpStatus, hCmpList, hExpCmp, hLblCmp, hCmpSum;
extern HWND hCmpActList;  // the Act Summary pill: one row per act
static const int kCmpActW[] = {170, 100, 90, 90, 90, 70, 100};
void campaignTimerMenu(POINT pt);  // right-click on the campaign timer: Pause, Stop, Generate Act Summary, Save and Clear
void campaignShowLists();
extern HWND hOptPill[3];  // Options: Overlays, Hotkeys, Theme
extern int gOptPill;
void setOptPill(int i);
extern HWND hLblPath, hPath, hBrowse, hReload, hOnTop, hOptInfo, hOptLbl, hLoadHist, hRestoreGen;
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
extern HWND hAlphaLbl, hAlphaSlider, hAlphaVal, hOvGeneralLbl, hOvEachLbl;
extern HWND hCmdChk, hCmdBar;
extern HWND hLblTheme, hThemeSel;
extern HWND hLblUpd, hUpdChk, hUpdInfo, hUpdTab;
extern bool gAutoUpdate;
extern HWND hCmpTimerLoad;  // Campaign: zone time including its loading screens
extern HWND hLblKeys, hKeyLbl[], hKeyBtn[];  // one per hotkey (HK_COUNT)
extern HWND hTabExp, hExpName, hExpLevel, hExpArea, hExpTabEff, hExpMapEff, hExpGain, hExpInfo, hExpRowLbl, hExpOvChk,
    hExpHint, hExpList, hLvlCmp, hLvlMaps, hLvlExport;
static const int kLvlW[] = {40, 220, 70, 80, 80, 110, 90, 150};  // level ups list
void expListChanged();
extern bool gLvlMaps;  // level ups list shows maps (else campaign zones)
void setLevelMode(bool maps);  // level ups list sorted: filled again
std::wstring expRequiredText();  // experience to the next level ("12.3M"), "" when the level is not known
std::wstring expCampaignPerHourText(uint64_t campaignMs);  // the campaign run so far, "" if not known
std::wstring expPerHourText();  // experience per hour of the last level that has a known time, "" if none
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
std::wstring getText(HWND h);  // a control's text
// Windows' file / folder dialogs; empty when cancelled
std::vector<std::wstring> pickFiles(HWND owner, const wchar_t* title, const wchar_t* filterName,
                                    const wchar_t* filterSpec, const std::wstring& start = L"");
std::wstring pickFolder(HWND owner, const wchar_t* title, const std::wstring& start = L"");
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
void restoreSessionFrom(SessionFile& sf, const std::wstring& file);
std::vector<std::wstring> campaignRunFiles();  // CampaignSession files, newest first
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
// Owner-drawn slider (min..max, step 1): drag, wheel or arrow keys; sends WM_HSCROLL (lParam = the slider) to the
// main window while it changes.
HWND slider(int id, int page, int lo, int hi);
int sliderValue(HWND h);
void setSliderValue(HWND h, int v);
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
void showListTip(const std::wstring& key, const std::wstring& title,
                 const std::vector<std::pair<std::wstring, std::wstring>>& lines);
void campaignRunsHover(HWND lv, LPARAM lp);  // Campaign -> Summary: an act cell shows its zones
void summaryHover(HWND lv, LPARAM lp);

// ---------------------------------------------------------------- hotkeys.cpp
enum { HK_STOP, HK_PAUSE, HK_START, HK_LAYOUT, HK_STEP_DONE, HK_STEP_UNDO, HK_STEP_EXPAND, HK_COUNT };
void loadHotkeys();
void installKeyHook();
void removeKeyHook();
std::wstring hotkeyName(int i);
void beginKeyCapture(int i);
void keyCaptured(UINT vk, int mods);
void hotkeyMenu(int i, LPARAM at);  // right-click on a hotkey button
void campaignAction(int i);

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
extern HWND hLayFolder, hGallery, hLayCropChk, hLayGraph, hLayImport;
void importLayouts();  // Import...: pictures into the selected zone, or a folder into the zones it names
extern bool gLayoutCrop;  // Options: layout screenshot = selected area (snipping tool), else the whole game window
void createLayoutsPage();
void refreshLayouts();  // rescan the folders (page shown, hotkey changed)
void layoutsWatch();  // from the 0.5 s tick: refresh the tab when its folders change outside the app
void openLayoutFolder();
void toggleLayoutCrop();
void makeLayoutGraph();  // points graph of the selected zone (src/core/outline_graph)
std::vector<std::wstring> currentZonePictures(bool graphsOnly);  // graphs, then outlines of the current zone
bool isGraph(const std::wstring& name);  // "graph" in the name
HBITMAP layoutPicture(const std::wstring& path, int maxW, int maxH, int& w, int& h);  // scaled copy of a picture
void takeLayoutShot();  // hotkey: Windows snipping, the picture is saved to the current zone
void layoutClipboardUpdate();
void layoutDone(LPARAM lp);  // WM_LAYOUT_DONE: a screenshot was saved / joined on the worker thread

std::vector<std::wstring> knownZoneNames();  // campaign zones (in order), then other areas seen in the log

// ---------------------------------------------------------------- page_steps.cpp
extern HWND hTabSteps;
void createStepsPage();
void layoutStepsPage(int L, int T, int R, int B);
void stepsCommand(int id, int code);
void stepsAction(int hk);  // HK_STEP_DONE / HK_STEP_UNDO: the next step of the zone done / the last one undone
void stepsChanged();  // steps or progress changed: overlay and tab
void stepsTick();  // 0.5 s heartbeat: follows the zone
void stepsHideSuggestions();  // the main window moved / lost the focus
bool stepsDrawItem(DRAWITEMSTRUCT* d);  // the preset list (owner-drawn); false = not ours
void stepsSaveOnExit();  // unsaved changes of the preset are kept
std::wstring pickSaveFile(HWND owner, const wchar_t* title, const std::wstring& folder, const std::wstring& name,
                          const wchar_t* ext);
const std::vector<steps::Step>& stepsAll();
steps::Progress& stepsProgress();
std::wstring stepsZoneName();  // the zone the player is in ("" in hideouts)
std::vector<int> stepsCurrent(bool* zoneSteps);  // steps of that zone
std::vector<steps::Note> stepsNotes();  // notes for that zone, then the ones for every zone
extern HWND hNotesChk;
extern bool gStepsNotes;  // Steps tab: notes on the overlay
void setStepsNotes(bool on);

// ---------------------------------------------------------------- overlay_steps.cpp
extern HWND hStepsOv, hStepsChk, hStepsLbl, hStepsMinus, hStepsVal, hStepsPlus;
extern bool gStepsOn;
extern int gStepsShown;  // Options: steps shown on the overlay (1-10)
void showStepsOverlay(bool on);
void stepsOverlayRefresh();
bool stepsOverlayFixedHeight();  // its height was dragged (else it fits the steps)
void stepsOverlayFitHeight();
void stepsOverlayExpand(bool on);  // held hotkey: every step of the zone, back when let go
void setStepsShown(int n);

// ---------------------------------------------------------------- area_tables_ui.cpp
void loadAreaTables();  // baked tables/*.txt + %APPDATA%\Poe2MapStat\tables
void areaTablesTick();  // 0.5 s: save new ids, reload edited files
void areaTablesChanged();  // names changed: rename layout folders, refresh what shows names
void layoutsFollowTables();  // page_layouts: folders named by an act key / zone id get the name the tables now know

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

// ---------------------------------------------------------------- overlay_graph.cpp
extern HWND hGraphOv, hGraphChk, hGraphOnlyChk;
uint32_t currentSeed();
bool hideFromCapture(HWND h);  // left out of screenshots (Windows 10 2004+); false where not supported
bool capturedOnScreen(HWND h);  // an overlay that screenshots would still show
extern bool gHideInCapture;  // Options: overlays left out of screenshots
extern HWND hCaptureChk;
void setHideInCapture(bool on);
bool hideCapturedOverlays(int ms);  // hides the overlays screenshots would show for ms; false = none to hide
// Click-through overlays: clicks go to the game below, except while Alt is held (then they can be clicked / moved).
extern bool gClickThru;
extern HWND hClickChk;
void setClickThru(bool on);
void clickThruPoll();  // every 50 ms
extern bool gGraphOn, gGraphOnly;
void setGraphOnly(bool on);  // Options: the graph overlay shows graphs only, no outlines
void graphOverlayShowPicture(const std::wstring& path);  // show this picture (an outline just made)
void showGraphOverlay(bool on);
void graphOverlayRefresh();  // the zone or its graphs changed
void graphOverlayTick();  // 0.5 s heartbeat: follows the zone the player is in

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
