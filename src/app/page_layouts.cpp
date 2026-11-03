// Layouts tab: screenshots of zone layouts, kept in %APPDATA%\Poe2MapStat\layouts\<group>\<zone>\<date>.png.
// Left: acts, interludes and maps (one expanded at a time) with their zones. Right: thumbnails of the selected
// zone; a click opens the picture in the Windows viewer, a right click shows more.
// The hotkey starts the Windows snipping tool (ms-screenclip:); the snip it puts on the clipboard is saved to the
// zone the player is in. Snips of the map overlay become outline pictures, joined per instance (seed) where they
// overlap (src/core/outline); the snips themselves are kept in <zone>\pieces.
#include "app.h"

#include <objidl.h>
#include <gdiplus.h>

#include "outline.h"

HWND hLayHint = nullptr, hLayFolder = nullptr, hGallery = nullptr;

namespace {

struct Group {
    const wchar_t* name;
    std::vector<const wchar_t*> zones;  // in campaign order; zones seen in the log or on disk are added
};

const Group kGroups[] = {
    {L"Act 1",
     {L"The Riverbank", L"Clearfell Encampment", L"Clearfell", L"Mud Burrow", L"The Grelwood", L"The Red Vale",
      L"The Grim Tangle", L"Cemetery of the Eternals", L"Mausoleum of the Praetor", L"Tomb of the Consort",
      L"Hunting Grounds", L"Freythorn", L"Ogham Farmlands", L"Ogham Village", L"The Manor Ramparts", L"Ogham Manor"}},
    {L"Act 2",
     {L"Vastiri Outskirts", L"The Ardura Caravan", L"Mawdun Quarry", L"Mawdun Mine", L"Traitor's Passage",
      L"The Halani Gates", L"Trial of the Sekhemas", L"Keth", L"The Lost City", L"Buried Shrines", L"Mastodon Badlands",
      L"The Bone Pits", L"Valley of the Titans", L"The Titan Grotto", L"Deshar", L"Path of Mourning",
      L"The Spires of Deshar", L"The Dreadnought", L"Dreadnought Vanguard"}},
    {L"Act 3",
     {L"Sandswept Marsh", L"Ziggurat Encampment", L"Jungle Ruins", L"The Venom Crypts", L"Chimeral Wetlands",
      L"Jiquani's Machinarium", L"Jiquani's Sanctum", L"The Azak Bog", L"Infested Barrens", L"The Matlan Waterways",
      L"The Drowned City", L"The Molten Vault", L"Apex of Filth", L"Temple of Kopec", L"Utzaal", L"Aggorat",
      L"The Black Chambers"}},
    {L"Act 4",
     {L"Kingsmarch", L"Isle of Kin", L"Volcanic Warrens", L"Kedge Bay", L"Journey's End", L"Whakapanu Island",
      L"Singing Caverns", L"Abandoned Prison", L"Solitary Confinement", L"Shrike Island", L"Eye of Hinekora",
      L"Halls of the Dead", L"Arastas", L"The Excavation", L"Ngakanu", L"Heart of the Tribe"}},
    {L"Interlude 1",
     {L"Scorched Farmlands", L"Stones of Serle", L"The Blackwood", L"Holten", L"Wolvenhold", L"Holten Estate"}},
    {L"Interlude 2",
     {L"Khari Crossing", L"Pools of Khatal", L"Sel Khari Sanctuary", L"The Galai Gates", L"Qimah", L"Qimah Reservoir"}},
    {L"Interlude 3",
     {L"Ashen Forest", L"Kriar Village", L"Glacial Tarn", L"Howling Caves", L"Kriar Peaks", L"Etched Ravine",
      L"The Cuachic Vault"}},
    {L"Maps", {}},  // filled from the log and from disk
};
const int kGroupCount = (int)(sizeof kGroups / sizeof kGroups[0]);
const int kMaps = kGroupCount - 1;

const int kLeftW = 250;  // zone menu
const int kHeadH = 70;  // title above the thumbnails
const int kThumbW = 240, kThumbH = 150, kGap = 14, kLabelH = 24;

struct Shot {
    std::wstring path, label;
    HBITMAP thumb = nullptr;
    int w = 0, h = 0;
};

struct Hit {
    RECT r;
    int kind;  // 0 = group, 1 = zone, 2 = screenshot
    int idx;
};

int gOpen = 0;  // expanded group, -1 = none
std::vector<std::wstring> gZones;  // zones of the expanded group
std::vector<int> gCounts;  // screenshots per zone
int gSelGroup = -1;  // shown on the right
std::wstring gSelZone;
std::vector<Shot> gShots;
std::vector<Hit> gHits;
int gHot = -1;  // hovered hit
int gLeftScroll = 0, gRightScroll = 0, gLeftTotal = 0, gRightTotal = 0;
bool gTracking = false;

// screenshot waiting for the snipping tool
bool gPending = false;
ULONGLONG gPendingAt = 0;
DWORD gClipSeq = 0;
int gPendGroup = -1;
std::wstring gPendZone;
uint32_t gPendSeed = 0;

std::wstring nameKey(const std::wstring& s) {
    std::wstring k;
    for (wchar_t c : s)
        if (iswalnum(c)) k += (wchar_t)towlower(c);
    return k;
}

// Zone name usable as a folder name.
std::wstring safeName(const std::wstring& s) {
    std::wstring r;
    for (wchar_t c : s)
        if (c >= 32 && !wcschr(L"\\/:*?\"<>|", c)) r += c;
    while (!r.empty() && (r.back() == L'.' || r.back() == L' ')) r.pop_back();
    return r.empty() ? L"Unknown" : r;
}

std::wstring layoutsDir() { return gDataDir + L"\\layouts"; }

std::wstring zoneDir(int gi, const std::wstring& zone) {
    return layoutsDir() + L"\\" + kGroups[gi].name + L"\\" + safeName(zone);
}

void makeDirs(int gi, const std::wstring& zone) {
    CreateDirectoryW(layoutsDir().c_str(), nullptr);
    CreateDirectoryW((layoutsDir() + L"\\" + kGroups[gi].name).c_str(), nullptr);
    if (!zone.empty()) CreateDirectoryW(zoneDir(gi, zone).c_str(), nullptr);
}

bool isPicture(const std::wstring& n) {
    std::wstring l = lowerW(n);
    for (const wchar_t* e : {L".png", L".jpg", L".jpeg", L".bmp"})
        if (l.size() > wcslen(e) && l.compare(l.size() - wcslen(e), wcslen(e), e) == 0) return true;
    return false;
}

// Names in a folder: sub-folders, or pictures.
std::vector<std::wstring> listDir(const std::wstring& dir, bool folders) {
    std::vector<std::wstring> r;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return r;
    do {
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (folders ? (isDir && fd.cFileName[0] != L'.') : (!isDir && isPicture(fd.cFileName)))
            r.push_back(fd.cFileName);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return r;
}

// Group of an area id: the act or interlude, Maps for other areas (sub-areas too), -1 for hideouts.
int groupOf(const std::string& id) {
    if (id.empty() || Tracker::isHideout(id)) return -1;
    if (!Tracker::isCampaign(id)) return kMaps;
    std::string a = Tracker::actOf(id);
    size_t c = a.find(" (Cruel)");
    if (c != std::string::npos) a.resize(c);
    for (int i = 0; i < kMaps; ++i)
        if (W(a) == kGroups[i].name) return i;
    return -1;
}

// The name a zone is stored under: the built-in spelling, else an existing folder with the same name.
std::wstring resolveZone(int gi, const std::wstring& name) {
    std::wstring k = nameKey(name);
    for (const wchar_t* z : kGroups[gi].zones)
        if (nameKey(z) == k) return z;
    for (auto& d : listDir(layoutsDir() + L"\\" + kGroups[gi].name, true))
        if (nameKey(d) == k) return d;
    return name;
}

void buildZones() {
    gZones.clear();
    gCounts.clear();
    if (gOpen < 0) return;
    std::vector<std::wstring> extra;
    auto have = [&](const std::wstring& n) {
        std::wstring k = nameKey(n);
        for (auto& z : gZones)
            if (nameKey(z) == k) return true;
        for (auto& z : extra)
            if (nameKey(z) == k) return true;
        return false;
    };
    for (const wchar_t* z : kGroups[gOpen].zones) gZones.push_back(z);
    for (auto& kv : g.areaNames) {  // zones seen in the log
        if (groupOf(kv.first) != gOpen) continue;
        if (gOpen == kMaps && kv.first.rfind("Map", 0) != 0) continue;  // only real maps
        std::wstring n = W(kv.second);
        if (!n.empty() && !have(n)) extra.push_back(n);
    }
    for (auto& d : listDir(layoutsDir() + L"\\" + kGroups[gOpen].name, true))
        if (!have(d)) extra.push_back(d);
    std::sort(extra.begin(), extra.end(), [](auto& a, auto& b) { return lowerW(a) < lowerW(b); });
    gZones.insert(gZones.end(), extra.begin(), extra.end());
    if (gOpen == kMaps) std::sort(gZones.begin(), gZones.end(), [](auto& a, auto& b) { return lowerW(a) < lowerW(b); });
    for (auto& z : gZones) gCounts.push_back((int)listDir(zoneDir(gOpen, z), false).size());
}

void freeShots(std::vector<Shot>& v) {
    for (auto& s : v)
        if (s.thumb) DeleteObject(s.thumb);
    v.clear();
}

// Screenshots of the selected zone, newest first (file names are dates). Thumbnails already made are kept.
void loadShots() {
    std::vector<Shot> old;
    old.swap(gShots);
    if (gSelGroup >= 0 && !gSelZone.empty()) {
        std::wstring dir = zoneDir(gSelGroup, gSelZone);
        auto files = listDir(dir, false);
        std::sort(files.begin(), files.end(), [](auto& a, auto& b) { return lowerW(a) > lowerW(b); });
        for (auto& f : files) {
            Shot s;
            s.path = dir + L"\\" + f;
            s.label = f.substr(0, f.find_last_of(L'.'));
            for (auto& o : old)
                if (o.thumb && o.path == s.path) {
                    s.thumb = o.thumb, s.w = o.w, s.h = o.h;
                    o.thumb = nullptr;
                }
            gShots.push_back(s);
        }
    }
    freeShots(old);
}

void gdipInit() {
    static ULONG_PTR token = 0;
    if (token) return;
    Gdiplus::GdiplusStartupInput in;
    Gdiplus::GdiplusStartup(&token, &in, nullptr);
}

const CLSID kPng = {0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};

// Opens a picture file and calls f(bitmap). The file is read into memory, so it is not kept open (it can be
// deleted or overwritten).
template <class F>
bool withPicture(const std::wstring& path, F f) {
    gdipInit();
    std::string d = readFileUtf8(path);
    if (d.empty()) return false;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, d.size());
    if (!mem) return false;
    memcpy(GlobalLock(mem), d.data(), d.size());
    GlobalUnlock(mem);
    IStream* st = nullptr;
    if (CreateStreamOnHGlobal(mem, TRUE, &st) != S_OK) {
        GlobalFree(mem);
        return false;
    }
    bool ok = false;
    {
        Gdiplus::Bitmap img(st);
        if (img.GetLastStatus() == Gdiplus::Ok && img.GetWidth() && img.GetHeight()) ok = f(img);
    }
    st->Release();
    return ok;
}

// Scaled copy of a picture.
HBITMAP makeThumb(const std::wstring& path, int maxW, int maxH, int& w, int& h) {
    HBITMAP out = nullptr;
    withPicture(path, [&](Gdiplus::Bitmap& img) {
        double s = std::min({(double)maxW / img.GetWidth(), (double)maxH / img.GetHeight(), 1.0});
        w = std::max(1, (int)std::lround(img.GetWidth() * s));
        h = std::max(1, (int)std::lround(img.GetHeight() * s));
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        out = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!out) return false;
        HDC mdc = CreateCompatibleDC(nullptr);
        HGDIOBJ o = SelectObject(mdc, out);
        {
            Gdiplus::Graphics gr(mdc);
            gr.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            gr.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            gr.DrawImage(&img, 0, 0, w, h);
        }
        SelectObject(mdc, o);
        DeleteDC(mdc);
        return true;
    });
    return out;
}

// RGB copy of a GDI+ bitmap.
bool toImage(Gdiplus::Bitmap& b, outline::Image& img) {
    Gdiplus::Rect r(0, 0, (INT)b.GetWidth(), (INT)b.GetHeight());
    Gdiplus::BitmapData d;
    if (b.LockBits(&r, Gdiplus::ImageLockModeRead, PixelFormat24bppRGB, &d) != Gdiplus::Ok) return false;
    img.w = r.Width, img.h = r.Height;
    img.px.resize((size_t)img.w * img.h * 3);
    for (int y = 0; y < img.h; ++y) {
        const uint8_t* s = (const uint8_t*)d.Scan0 + (size_t)y * d.Stride;  // B, G, R
        uint8_t* t = &img.px[(size_t)y * img.w * 3];
        for (int x = 0; x < img.w; ++x) t[x * 3] = s[x * 3 + 2], t[x * 3 + 1] = s[x * 3 + 1], t[x * 3 + 2] = s[x * 3];
    }
    b.UnlockBits(&d);
    return true;
}

bool savePng(const outline::Image& img, const std::wstring& path) {
    Gdiplus::Bitmap b(img.w, img.h, PixelFormat24bppRGB);
    Gdiplus::Rect r(0, 0, img.w, img.h);
    Gdiplus::BitmapData d;
    if (b.LockBits(&r, Gdiplus::ImageLockModeWrite, PixelFormat24bppRGB, &d) != Gdiplus::Ok) return false;
    for (int y = 0; y < img.h; ++y) {
        uint8_t* t = (uint8_t*)d.Scan0 + (size_t)y * d.Stride;
        const uint8_t* s = &img.px[(size_t)y * img.w * 3];
        for (int x = 0; x < img.w; ++x) t[x * 3] = s[x * 3 + 2], t[x * 3 + 1] = s[x * 3 + 1], t[x * 3 + 2] = s[x * 3];
    }
    b.UnlockBits(&d);
    return b.Save(path.c_str(), &kPng, nullptr) == Gdiplus::Ok;
}

// "<base>.png", or "<base> (2).png" ... when taken.
std::wstring freePath(const std::wstring& base) {
    std::wstring p = base + L".png";
    for (int i = 2; fileExists(p); ++i) p = base + L" (" + std::to_wstring(i) + L").png";
    return p;
}

// Saves a screenshot to the zone and says what became of it ("" = failed). A snip of the map overlay in a known
// instance becomes an outline picture "<date> seed <seed>.png", joined with the outlines of that instance it
// overlaps; the snip itself goes to "pieces". Other pictures are saved as they are.
std::wstring storeShot(HBITMAP hb, int gi, const std::wstring& zone, uint32_t seed) {
    gdipInit();
    makeDirs(gi, zone);
    outline::Image shot;
    {
        Gdiplus::Bitmap b(hb, nullptr);
        if (b.GetLastStatus() != Gdiplus::Ok || !toImage(b, shot) || shot.empty()) return L"";
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t d[64];
    swprintf(d, 64, L"%04d-%02d-%02d %02d-%02d-%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::wstring dir = zoneDir(gi, zone), tag = seed ? L" seed " + std::to_wstring(seed) : L"";
    outline::Mask e = outline::edges(shot);
    if (!seed || std::count(e.m.begin(), e.m.end(), 1) < 300)  // no map outline in it, or instance unknown
        return savePng(shot, freePath(dir + L"\\" + d + tag)) ? L"saved" : L"";

    CreateDirectoryW((dir + L"\\pieces").c_str(), nullptr);
    savePng(shot, freePath(dir + L"\\pieces\\" + d + tag));
    std::vector<std::wstring> paths;  // outlines of this instance
    std::vector<outline::Image> outs;
    for (auto& f : listDir(dir, false)) {
            if (f.find(tag + L".") == std::wstring::npos && f.find(tag + L" (") == std::wstring::npos) continue;
            outline::Image img;
        if (!withPicture(dir + L"\\" + f, [&](Gdiplus::Bitmap& b) { return toImage(b, img); })) continue;
        paths.push_back(dir + L"\\" + f);
        outs.push_back(std::move(img));
    }
    outline::Added a = outline::add(outs, shot);
    std::wstring path = a.into < (int)paths.size() ? paths[a.into] : freePath(dir + L"\\" + d + tag);
    if (!savePng(outs[a.into], path)) return L"";
    for (size_t i = 0; i < paths.size(); ++i)  // joined into the other one
        if (outs[i].empty()) DeleteFileW(paths[i].c_str());
    if (!a.overlapped) return L"new outline";
    if (a.overlapped == 1) return L"added to the outline";
    return L"joined " + std::to_wstring(a.overlapped) + L" outlines";
}

void selectZone(int gi, const std::wstring& zone) {
    gSelGroup = gi;
    gSelZone = zone;
    gRightScroll = 0;
    loadShots();
    if (hGallery) InvalidateRect(hGallery, nullptr, FALSE);
}

void shotSaved(const std::wstring& what, int gi, const std::wstring& zone) {
    if (what.empty()) {
        trayBalloon(L"Layout screenshot", L"The screenshot could not be saved.");
        return;
    }
    trayBalloon(L"Layout " + what, std::wstring(kGroups[gi].name) + L"  \u00b7  " + zone);
    gOpen = gi;  // show it in the gallery
    buildZones();
    selectZone(gi, zone);
}

// Seed of the instance the player is in (0 = unknown, e.g. a sub-area).
uint32_t currentSeed() {
    if (g.curZone >= 0 && g.curZone < (int)g.zones.size() && g.zones[g.curZone].areaId == g.location)
        return g.zones[g.curZone].seed;
    if (g.hasCurrent && g.cur.areaId == g.location) return g.cur.seed;
    return 0;
}

void stopPending() {
    if (gPending) RemoveClipboardFormatListener(hMain);
    gPending = false;
}

// Without the snipping tool: the whole game window.
void captureGame(int gi, const std::wstring& zone, uint32_t seed) {
    RECT r;
    if (!gameClientRect(r)) {
        HWND gw = findGame();
        if (!gw || !GetWindowRect(gw, &r)) return shotSaved(L"", gi, zone);
    }
    int w = r.right - r.left, h = r.bottom - r.top;
    HDC sdc = GetDC(nullptr), mdc = CreateCompatibleDC(sdc);
    HBITMAP hb = CreateCompatibleBitmap(sdc, w, h);
    HGDIOBJ o = SelectObject(mdc, hb);
    BitBlt(mdc, 0, 0, w, h, sdc, r.left, r.top, SRCCOPY | CAPTUREBLT);
    SelectObject(mdc, o);
    DeleteDC(mdc);
    ReleaseDC(nullptr, sdc);
    std::wstring what = storeShot(hb, gi, zone, seed);
    DeleteObject(hb);
    shotSaved(what, gi, zone);
}

void menuShot(int i) {
    if (i < 0 || i >= (int)gShots.size()) return;
    std::wstring path = gShots[i].path;
    gMenuItems.clear();
    gMenuItems.reserve(4);
    gMenuItems.push_back({L"Open", false});
    gMenuItems.push_back({L"Show in folder", false});
    gMenuItems.push_back({L"", true});
    gMenuItems.push_back({L"Delete (to Recycle Bin)", false});
    HMENU m = CreatePopupMenu();
    MENUINFO mi = {};
    mi.cbSize = sizeof mi;
    mi.fMask = MIM_BACKGROUND;
    mi.hbrBack = brCard;
    SetMenuInfo(m, &mi);
    const int ids[] = {1, 2, 0, 3};
    for (int k = 0; k < 4; ++k)
        AppendMenuW(m, MF_OWNERDRAW | (gMenuItems[k].separator ? MF_DISABLED : 0), ids[k], (LPCWSTR)&gMenuItems[k]);
    POINT pt;
    GetCursorPos(&pt);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hMain, nullptr);
    DestroyMenu(m);
    if (cmd == 1) ShellExecuteW(hMain, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (cmd == 2)
        ShellExecuteW(hMain, L"open", L"explorer.exe", (L"/select,\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    if (cmd == 3) {
        std::wstring from = path + L'\0';  // list ends with two zeros
        SHFILEOPSTRUCTW op = {};
        op.hwnd = hMain;
        op.wFunc = FO_DELETE;
        op.pFrom = from.c_str();
        op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
        SHFileOperationW(&op);
        refreshLayouts();
    }
}

void drawText(HDC dc, const std::wstring& t, RECT r, HFONT f, COLORREF c, UINT fmt) {
    HGDIOBJ o = SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, t.c_str(), (int)t.size(), &r, fmt | DT_NOPREFIX);
    SelectObject(dc, o);
}

// Small triangle: pointing down when the group is open, right when closed.
void drawArrow(HDC dc, int x, int cy, bool down, COLORREF c) {
    POINT p[3];
    if (down) p[0] = {x, cy - 3}, p[1] = {x + 9, cy - 3}, p[2] = {x + 4, cy + 3};
    else p[0] = {x + 2, cy - 5}, p[1] = {x + 2, cy + 5}, p[2] = {x + 7, cy};
    HBRUSH b = CreateSolidBrush(c);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, p, 3);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
}

void paint(HWND h) {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bb = CreateCompatibleBitmap(wdc, std::max(1, (int)rc.right), std::max(1, (int)rc.bottom));
    HGDIOBJ ob = SelectObject(dc, bb);
    FillRect(dc, &rc, brBg);
    SetBkMode(dc, TRANSPARENT);
    std::vector<Hit> hits;
    auto hot = [&](int kind, int idx) {
        return gHot >= 0 && gHot < (int)gHits.size() && gHits[gHot].kind == kind && gHits[gHot].idx == idx;
    };

    // ---- left: groups and the zones of the expanded one
    RECT lp = {0, 0, kLeftW, rc.bottom};
    fillRound(dc, lp, 12, C::Card, C::Line);
    IntersectClipRect(dc, 1, 6, kLeftW - 1, rc.bottom - 6);
    int y = 8 - gLeftScroll;
    for (int gi = 0; gi < kGroupCount; ++gi) {
        RECT r = {8, y, kLeftW - 8, y + 30};
        bool open = gi == gOpen;
        if (hot(0, gi)) fillRound(dc, r, 8, C::Line, C::Line);
        COLORREF gc = open ? C::Purple : C::Fg;
        drawArrow(dc, r.left + 10, (r.top + r.bottom) / 2, open, gc);
        drawText(dc, kGroups[gi].name, {r.left + 26, r.top, r.right, r.bottom}, fBold, gc, DT_SINGLELINE | DT_VCENTER);
        hits.push_back({r, 0, gi});
        y += 32;
        if (!open) continue;
        if (gZones.empty()) {
            drawText(dc, L"Nothing saved yet", {34, y, kLeftW - 12, y + 24}, fNormal, C::Comment,
                     DT_SINGLELINE | DT_VCENTER);
            y += 26;
        }
        for (int zi = 0; zi < (int)gZones.size(); ++zi) {
            RECT zr = {16, y, kLeftW - 8, y + 24};
            bool sel = gSelGroup == gi && nameKey(gZones[zi]) == nameKey(gSelZone);
            if (sel || hot(1, zi)) fillRound(dc, zr, 6, sel ? C::Line : C::Stripe, sel ? C::Line : C::Stripe);
            COLORREF col = sel ? C::Purple : gCounts[zi] ? C::Fg : C::Comment;
            drawText(dc, gZones[zi], {zr.left + 18, zr.top, zr.right - 34, zr.bottom}, fNormal, col,
                     DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            if (gCounts[zi])
                drawText(dc, std::to_wstring(gCounts[zi]), {zr.right - 34, zr.top, zr.right - 8, zr.bottom}, fNormal,
                         C::Comment, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
            hits.push_back({zr, 1, zi});
            y += 26;
        }
        y += 6;
    }
    gLeftTotal = y + gLeftScroll + 8;
    SelectClipRgn(dc, nullptr);

    // ---- right: thumbnails of the selected zone
    int x0 = kLeftW + 20, rw = rc.right - x0;
    if (gSelZone.empty()) {
        drawText(dc, L"Pick a zone on the left.", {x0, 0, rc.right, 40}, fNormal, C::Comment,
                 DT_SINGLELINE | DT_VCENTER);
        gRightTotal = 0;
    } else {
        drawText(dc, gSelZone, {x0, 0, rc.right, 40}, fTitle, C::Fg, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        std::wstring sub = std::wstring(kGroups[gSelGroup].name) + L"  \u00b7  " + std::to_wstring(gShots.size()) +
                           (gShots.size() == 1 ? L" screenshot" : L" screenshots") +
                           (gShots.empty() ? L"" : L"  \u00b7  click to open, right click for more");
        drawText(dc, sub, {x0, 40, rc.right, 62}, fNormal, C::Comment, DT_SINGLELINE | DT_VCENTER);
        int cols = std::max(1, (rw + kGap) / (kThumbW + kGap));
        IntersectClipRect(dc, x0, kHeadH, rc.right, rc.bottom);
        for (int i = 0; i < (int)gShots.size(); ++i) {
            Shot& s = gShots[i];
            int cx = x0 + (i % cols) * (kThumbW + kGap);
            int cy = kHeadH + (i / cols) * (kThumbH + kLabelH + kGap) - gRightScroll;
            RECT cr = {cx, cy, cx + kThumbW, cy + kThumbH};
            if (cy + kThumbH + kLabelH < 0 || cy > rc.bottom) {
                hits.push_back({cr, 2, i});
                continue;
            }
            fillRound(dc, cr, 10, C::Card, hot(2, i) ? C::Purple : C::Line);
            if (!s.thumb) s.thumb = makeThumb(s.path, kThumbW - 12, kThumbH - 12, s.w, s.h);
            if (s.thumb) {
                HDC tdc = CreateCompatibleDC(dc);
                HGDIOBJ to = SelectObject(tdc, s.thumb);
                BitBlt(dc, cx + (kThumbW - s.w) / 2, cy + (kThumbH - s.h) / 2, s.w, s.h, tdc, 0, 0, SRCCOPY);
                SelectObject(tdc, to);
                DeleteDC(tdc);
            } else {
                drawText(dc, L"cannot read", cr, fNormal, C::Comment, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            }
            drawText(dc, s.label, {cx, cy + kThumbH, cx + kThumbW, cy + kThumbH + kLabelH}, fNormal,
                     hot(2, i) ? C::Fg : C::Comment, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
            hits.push_back({cr, 2, i});
        }
        if (gShots.empty()) {
            std::wstring key = hotkeyName(HK_LAYOUT);
            drawText(dc,
                     gHotkeysOn && key != L"Not set"
                         ? L"No screenshots yet. Press " + key + L" while you are in this zone."
                         : std::wstring(L"No screenshots yet."),
                     {x0, kHeadH, rc.right, kHeadH + 24}, fNormal, C::Comment, DT_SINGLELINE | DT_VCENTER);
        }
        int rows = ((int)gShots.size() + cols - 1) / cols;
        gRightTotal = kHeadH + rows * (kThumbH + kLabelH + kGap);
        SelectClipRgn(dc, nullptr);
    }

    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, ob);
    DeleteObject(bb);
    DeleteDC(dc);
    EndPaint(h, &ps);
    gHits.swap(hits);
}

int hitAt(POINT p) {
    RECT rc;
    GetClientRect(hGallery, &rc);
    for (int i = 0; i < (int)gHits.size(); ++i) {
        const RECT& r = gHits[i].r;
        if (gHits[i].kind == 2 && p.y < kHeadH) continue;  // scrolled under the title
        if (gHits[i].kind != 2 && (p.y < 6 || p.y > rc.bottom - 6)) continue;
        if (PtInRect(&r, p)) return i;
    }
    return -1;
}

void clampScroll() {
    RECT rc;
    GetClientRect(hGallery, &rc);
    gLeftScroll = std::clamp(gLeftScroll, 0, std::max(0, gLeftTotal - (int)rc.bottom));
    gRightScroll = std::clamp(gRightScroll, 0, std::max(0, gRightTotal - (int)rc.bottom));
}

LRESULT CALLBACK GalleryProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            paint(h);
            return 0;
        case WM_SIZE:
            clampScroll();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_MOUSEMOVE: {
            if (!gTracking) {
                TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, h, 0};
                gTracking = TrackMouseEvent(&t);
            }
            int hit = hitAt({(short)LOWORD(lp), (short)HIWORD(lp)});
            if (hit != gHot) {
                gHot = hit;
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            gTracking = false;
            gHot = -1;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                SetCursor(LoadCursor(nullptr, gHot >= 0 ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_MOUSEWHEEL: {
            POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
            ScreenToClient(h, &p);
            int d = -GET_WHEEL_DELTA_WPARAM(wp) * 60 / WHEEL_DELTA;
            (p.x < kLeftW ? gLeftScroll : gRightScroll) += d;
            clampScroll();
            gHot = -1;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP: {
            int hit = hitAt({(short)LOWORD(lp), (short)HIWORD(lp)});
            if (hit < 0) return 0;
            Hit ht = gHits[hit];
            if (ht.kind == 0) {  // one group open at a time; clicking the open one closes it
                gOpen = gOpen == ht.idx ? -1 : ht.idx;
                buildZones();
                // the opened group's title goes to the top, so its zones are in view
                gLeftTotal = 16 + kGroupCount * 32 + (gOpen < 0 ? 0 : std::max(1, (int)gZones.size()) * 26 + 6);
                gLeftScroll = gOpen < 0 ? 0 : gOpen * 32;
            } else if (ht.kind == 1 && ht.idx < (int)gZones.size()) {
                selectZone(gOpen, gZones[ht.idx]);
            } else if (ht.kind == 2 && ht.idx < (int)gShots.size()) {
                ShellExecuteW(hMain, L"open", gShots[ht.idx].path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            gHot = -1;
            clampScroll();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_RBUTTONUP: {
            int hit = hitAt({(short)LOWORD(lp), (short)HIWORD(lp)});
            if (hit >= 0 && gHits[hit].kind == 2) menuShot(gHits[hit].idx);
            return 0;
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

void createLayoutsPage() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = GalleryProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"Poe2MapStatGallery";
    RegisterClassExW(&wc);
    hGallery = CreateWindowExW(0, wc.lpszClassName, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 10, 10, hMain, nullptr,
                               wc.hInstance, nullptr);
    gPages[PG_LAYOUTS].push_back(hGallery);
    hLayHint = label(L"", ID_LAY_HINT, fNormal, PG_LAYOUTS, false, SS_ENDELLIPSIS);
    hLayFolder = btn(L"Open folder", ID_LAY_FOLDER, PG_LAYOUTS, Btn::Normal);
}

void refreshLayouts() {
    if (!hGallery) return;
    std::wstring key = hotkeyName(HK_LAYOUT);
    setTextIfChanged(
        hLayHint, gHotkeysOn && key != L"Not set"
                      ? L"Press " + key + L" in a zone or map and select an area: the screenshot is saved to that zone."
                      : std::wstring(L"Turn on hotkeys and set a key for Layout screenshot in Options to "
                                     L"take screenshots in game."));
    buildZones();
    loadShots();
    clampScroll();
    InvalidateRect(hGallery, nullptr, FALSE);
}

void openLayoutFolder() {
    std::wstring dir = layoutsDir();
    if (gSelGroup >= 0 && !gSelZone.empty()) {
        makeDirs(gSelGroup, gSelZone);
        dir = zoneDir(gSelGroup, gSelZone);
    } else {
        CreateDirectoryW(dir.c_str(), nullptr);
    }
    ShellExecuteW(hMain, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void takeLayoutShot() {
    int gi = groupOf(g.location);
    if (gi < 0) {
        trayBalloon(L"Layout screenshot", L"Enter a campaign zone or a map first (not in a hideout).");
        return;
    }
    std::wstring name = W(g.locationName.empty() ? g.nameOf(g.location) : g.locationName);
    stopPending();
    gPendGroup = gi;
    gPendZone = resolveZone(gi, name);
    gPendSeed = currentSeed();
    gPendingAt = GetTickCount64();
    gClipSeq = GetClipboardSequenceNumber();
    gPending = AddClipboardFormatListener(hMain) != FALSE;
    if (!gPending ||
        (INT_PTR)ShellExecuteW(nullptr, L"open", L"ms-screenclip:", nullptr, nullptr, SW_SHOWNORMAL) <= 32) {
        stopPending();  // no snipping tool: take the whole game window
        captureGame(gi, gPendZone, gPendSeed);
    }
}

// The snipping tool puts the selected area on the clipboard.
void layoutClipboardUpdate() {
    if (!gPending) return;
    if (GetTickCount64() - gPendingAt > 120000) {  // snip cancelled long ago
        stopPending();
        return;
    }
    if (GetClipboardSequenceNumber() == gClipSeq || !IsClipboardFormatAvailable(CF_BITMAP)) return;
    bool open = false;
    for (int i = 0; i < 20 && !(open = OpenClipboard(hMain)); ++i) Sleep(25);  // the tool may still hold it
    if (!open) return;
    HBITMAP hb = (HBITMAP)GetClipboardData(CF_BITMAP);
    std::wstring what = hb ? storeShot(hb, gPendGroup, gPendZone, gPendSeed) : L"";
    CloseClipboard();
    int gi = gPendGroup;
    std::wstring zone = gPendZone;
    stopPending();
    shotSaved(what, gi, zone);
}
