// Layouts tab: screenshots of zone layouts, kept in %APPDATA%\Poe2MapStat\layouts\<group>\<zone>\<date>.png.
// Left: acts, interludes and maps (one expanded at a time) with their zones. Right: thumbnails of the selected
// zone; a click opens the picture in the Windows viewer, a right click shows more.
// The hotkey saves the whole game window, or (Options) starts the Windows snipping tool (ms-screenclip:) and saves
// the snip it puts on the clipboard; either goes to the zone the player is in. Snips of the map overlay become outline pictures, joined per instance (seed) where they
// overlap (src/core/outline); the screenshots themselves are kept in <zone>\pieces\<seed>.
#include "app.h"

#include <objidl.h>
#include <gdiplus.h>

#include <mutex>

#include "outline.h"
#include "outline_graph.h"

HWND hLayHint = nullptr, hLayFolder = nullptr, hGallery = nullptr, hLayCropChk = nullptr, hLayGraph = nullptr;
bool gLayoutCrop = false;

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
    RECT labelR = {};  // where the name is drawn (for renaming in place)
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
std::wstring gSelPath;  // the selected picture (click), "" = none
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

// Any picture with "graph" in its name is a graph (shown by the GraphOverlay, not used as an outline).
bool isGraph(const std::wstring& n) { return lowerW(n).find(L"graph") != std::wstring::npos; }

bool isPicture(const std::wstring& n) {
    std::wstring l = lowerW(n);
    for (const wchar_t* e : {L".png", L".jpg", L".jpeg", L".bmp"})
        if (l.size() > wcslen(e) && l.compare(l.size() - wcslen(e), wcslen(e), e) == 0) return true;
    return false;
}

// The points found on an outline picture are kept next to it: "<name>.txt" (outline::saveLabels).
// The points found on an outline picture are kept in the zone's "Metadata\<seed>" folder, as "<name>.txt"
// (outline::saveLabels). Older versions kept them next to the picture.
std::wstring sidecarOf(const std::wstring& pic) {
    size_t slash = pic.find_last_of(L"\\/"), dot = pic.find_last_of(L'.');
    std::wstring dir = pic.substr(0, slash);
    std::wstring stem = pic.substr(slash + 1, dot == std::wstring::npos || dot < slash ? std::wstring::npos : dot - slash - 1);
    std::wstring seed = L"other";
    size_t at = stem.find(L" seed ");
    if (at != std::wstring::npos) {
        size_t e = at + 6;
        while (e < stem.size() && iswdigit(stem[e])) ++e;
        if (e > at + 6) seed = stem.substr(at + 6, e - at - 6);
    }
    return dir + L"\\Metadata\\" + seed + L"\\" + stem + L".txt";
}
std::wstring oldSidecarOf(const std::wstring& pic) { return pic.substr(0, pic.find_last_of(L'.')) + L".txt"; }
std::wstring sidecarFound(const std::wstring& pic) {  // where a picture's points are now ("" = none)
    if (fileExists(sidecarOf(pic))) return sidecarOf(pic);
    return fileExists(oldSidecarOf(pic)) ? oldSidecarOf(pic) : L"";
}
void makeParent(const std::wstring& file) {  // the folders a file goes in
    for (size_t i = file.find(L'\\', 3); i != std::wstring::npos; i = file.find(L'\\', i + 1))
        CreateDirectoryW(file.substr(0, i).c_str(), nullptr);
}

std::string readText(const std::wstring& path) {
    std::string r;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return r;
    char buf[4096];
    DWORD n;
    while (ReadFile(f, buf, sizeof buf, &n, nullptr) && n) r.append(buf, n);
    CloseHandle(f);
    return r;
}

bool writeText(const std::wstring& path, const std::string& s) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD n = 0;
    bool ok = WriteFile(f, s.data(), (DWORD)s.size(), &n, nullptr) && n == s.size();
    CloseHandle(f);
    return ok;
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

// `texts`: drawn on the picture (white, with a dark shadow).
bool savePng(const outline::Image& img, const std::wstring& path,
             const std::vector<std::pair<std::wstring, POINT>>* texts = nullptr) {
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
    if (texts) {
        Gdiplus::Graphics gr(&b);
        gr.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        Gdiplus::FontFamily fam(gFontUi.c_str());
        const Gdiplus::FontFamily* use =
            fam.GetLastStatus() == Gdiplus::Ok ? &fam : Gdiplus::FontFamily::GenericSansSerif();
        Gdiplus::Font font(use, 17, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush white(Gdiplus::Color(255, 255, 255, 255)), shade(Gdiplus::Color(255, 0, 0, 0));
        for (auto& t : *texts) {
            Gdiplus::PointF p((Gdiplus::REAL)t.second.x, (Gdiplus::REAL)t.second.y);
            gr.DrawString(t.first.c_str(), -1, &font, Gdiplus::PointF(p.X + 1, p.Y + 1), &shade);
            gr.DrawString(t.first.c_str(), -1, &font, p, &white);
        }
    }
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
// overlaps; the screenshot itself goes to "pieces\<seed>". Other pictures are saved as they are.
std::mutex gOutlineBusy;  // one at a time: two quick presses (or a graph) work on the same files

// Outlines of an instance in a zone's folder ("<date> seed <seed>.png" and its points), loaded.
void loadOutlines(const std::wstring& dir, uint32_t seed, std::vector<std::wstring>& paths,
                  std::vector<outline::Outline>& outs) {
    std::wstring tag = L" seed " + std::to_wstring(seed);
    for (auto& f : listDir(dir, false)) {
        if (isGraph(f) || (f.find(tag + L".") == std::wstring::npos && f.find(tag + L" (") == std::wstring::npos))
            continue;
        outline::Outline o;
        if (!withPicture(dir + L"\\" + f, [&](Gdiplus::Bitmap& b) { return toImage(b, o.img); })) continue;
        o.labels = outline::loadLabels(readText(sidecarFound(dir + L"\\" + f)));
        paths.push_back(dir + L"\\" + f);
        outs.push_back(std::move(o));
    }
}

bool saveOutline(const outline::Outline& o, const std::wstring& path) {
    makeParent(sidecarOf(path));
    if (!savePng(o.img, path) || !writeText(sidecarOf(path), outline::saveLabels(o.labels))) return false;
    DeleteFileW(oldSidecarOf(path).c_str());
    return true;
}

void deleteOutline(const std::wstring& path) {
    DeleteFileW(path.c_str());
    DeleteFileW(sidecarOf(path).c_str());
    DeleteFileW(oldSidecarOf(path).c_str());
}

// How much of a map an outline shows: its wall and text pixels, and its labels.
long detail(const outline::Outline& o) {
    long n = (long)o.labels.size() * 2000;
    for (size_t i = 0; i < o.img.px.size(); i += 6)
        n += std::min({o.img.px[i], o.img.px[i + 1], o.img.px[i + 2]}) >= 150;
    return n;
}

// One outline per instance: of pieces that do not overlap (yet), the one showing the most is kept. The others'
// screenshots stay in "pieces", so they can join it later (Make graph builds the outline from them again).
int keepMostDetailed(const std::vector<outline::Outline>& outs) {
    int best = -1;
    for (int i = 0; i < (int)outs.size(); ++i)
        if (!outs[i].empty() && (best < 0 || detail(outs[i]) > detail(outs[best]))) best = i;
    return best;
}

std::wstring storeShot(const outline::Image& shot, int gi, const std::wstring& zone, uint32_t seed,
                       std::wstring* outlinePath = nullptr) {
    std::lock_guard<std::mutex> lock(gOutlineBusy);
    gdipInit();
    makeDirs(gi, zone);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t d[64];
    swprintf(d, 64, L"%04d-%02d-%02d %02d-%02d-%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::wstring dir = zoneDir(gi, zone), tag = seed ? L" seed " + std::to_wstring(seed) : L"";
    outline::Mask e = outline::edges(shot);
    if (!seed || std::count(e.m.begin(), e.m.end(), 1) < 300)  // no map outline in it, or instance unknown
        return savePng(shot, freePath(dir + L"\\" + d + tag)) ? L"saved" : L"";

    std::wstring pieces = dir + L"\\pieces\\" + std::to_wstring(seed);
    CreateDirectoryW((dir + L"\\pieces").c_str(), nullptr);
    CreateDirectoryW(pieces.c_str(), nullptr);
    savePng(shot, freePath(pieces + L"\\" + d));
    std::vector<std::wstring> paths;  // outlines of this instance
    std::vector<outline::Outline> outs;
    loadOutlines(dir, seed, paths, outs);
    outline::Added a = outline::add(outs, shot);
    int keep = keepMostDetailed(outs);
    if (keep < 0) return L"";
    std::wstring path = keep < (int)paths.size() ? paths[keep] : freePath(dir + L"\\" + d + tag);
    if (!saveOutline(outs[keep], path)) return L"";
    if (outlinePath) *outlinePath = path;
    for (size_t i = 0; i < paths.size(); ++i)  // joined into the kept one, or showing less of the map
        if ((int)i != keep) deleteOutline(paths[i]);
    if (keep != a.into) return L"kept the larger outline (this piece does not overlap it yet)";
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

}  // namespace

// Seed of the instance the player is in (0 = unknown, e.g. a sub-area).
uint32_t currentSeed() {
    if (g.curZone >= 0 && g.curZone < (int)g.zones.size() && g.zones[g.curZone].areaId == g.location)
        return g.zones[g.curZone].seed;
    if (g.hasCurrent && g.cur.areaId == g.location) return g.cur.seed;
    return 0;
}

namespace {

void stopPending() {
    if (gPending) RemoveClipboardFormatListener(hMain);
    gPending = false;
}

// Joining takes about a second for a full window, so it runs on a worker thread; the result comes back as
// WM_LAYOUT_DONE (layoutDone).
struct ShotDone {
    std::wstring what, zone;
    int gi;
    bool graph = false;  // a points graph (makeLayoutGraph), not a screenshot
    int used = 0, runs = 0;
    std::wstring outline;  // the outline picture the screenshot went into
    uint32_t seed = 0;
};

void storeAsync(HBITMAP hb, int gi, const std::wstring& zone, uint32_t seed) {
    gdipInit();
    outline::Image shot;
    {
        Gdiplus::Bitmap b(hb, nullptr);
        if (b.GetLastStatus() != Gdiplus::Ok || !toImage(b, shot) || shot.empty()) return shotSaved(L"", gi, zone);
    }
    std::thread([shot = std::move(shot), gi, zone, seed] {
        std::wstring path;
        auto* r = new ShotDone{storeShot(shot, gi, zone, seed, &path), zone, gi, false, 0, 0, {}, 0};
        r->outline = path, r->seed = seed;
        if (!PostMessageW(hMain, WM_LAYOUT_DONE, 0, (LPARAM)r)) delete r;
    }).detach();
}

// The game window without the parts that stay in place on screen, so they cannot make two pieces look alike:
// the bar at the top (buffs), the sides and the bottom (life, mana, skill bar), the minimap corner (same line
// colour, other scale), the character in the middle (life bar, spell effects). This app's overlays are not in it. The map
// overlay there comes from other pieces.
void captureGame(int gi, const std::wstring& zone, uint32_t seed) {
    RECT r;
    if (!gameClientRect(r)) {
        HWND gw = findGame();
        if (!gw || !GetWindowRect(gw, &r)) return shotSaved(L"", gi, zone);
    }
    int W = r.right - r.left, H = r.bottom - r.top;
    RECT c = {r.left + W * 12 / 100, r.top + H * 8 / 100, r.left + W * 88 / 100, r.top + H * 85 / 100};
    int w = c.right - c.left, h = c.bottom - c.top;
    // overlays a screen capture would show (older Windows): hidden while the picture is taken
    std::vector<HWND> shown;
    for (HWND ov : {hOverlay, hCmdBar, hGraphOv, hLayoutOv})
        if (capturedOnScreen(ov) && IsWindowVisible(ov)) shown.push_back(ov), ShowWindow(ov, SW_HIDE);
    if (!shown.empty()) DwmFlush(), Sleep(30);
    HDC sdc = GetDC(nullptr), mdc = CreateCompatibleDC(sdc);
    HBITMAP hb = CreateCompatibleBitmap(sdc, w, h);
    HGDIOBJ o = SelectObject(mdc, hb);
    BitBlt(mdc, 0, 0, w, h, sdc, c.left, c.top, SRCCOPY | CAPTUREBLT);
    for (HWND ov : shown) ShowWindow(ov, SW_SHOWNOACTIVATE);
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RECT minimap = {r.left + W * 78 / 100, r.top, r.right, r.top + H * 32 / 100};
    RECT player = {r.left + W * 44 / 100, r.top + H * 18 / 100, r.left + W * 56 / 100, r.top + H * 50 / 100};
    std::vector<RECT> hide = {minimap, player};
    for (RECT x : hide) {
        OffsetRect(&x, -c.left, -c.top);
        FillRect(mdc, &x, black);
    }
    SelectObject(mdc, o);
    DeleteDC(mdc);
    ReleaseDC(nullptr, sdc);
    storeAsync(hb, gi, zone, seed);
    DeleteObject(hb);
}

// ---- renaming a picture in place: an edit box over its name; Enter or clicking elsewhere keeps, Esc cancels
const UINT WM_RENAME_END = WM_APP + 60;
HWND gRename = nullptr;
std::wstring gRenamePath;

void endRename(bool keep) {
    if (!gRename) return;
    wchar_t buf[MAX_PATH] = L"";
    GetWindowTextW(gRename, buf, MAX_PATH);
    HWND e = gRename;
    gRename = nullptr;  // before DestroyWindow: losing the focus ends the rename again
    DestroyWindow(e);
    std::wstring name = buf;
    while (!name.empty() && (name.back() == L' ' || name.back() == L'.')) name.pop_back();
    while (!name.empty() && name[0] == L' ') name.erase(0, 1);
    size_t slash = gRenamePath.find_last_of(L"\\"), dot = gRenamePath.find_last_of(L'.');
    std::wstring dir = gRenamePath.substr(0, slash), old = gRenamePath.substr(slash + 1, dot - slash - 1);
    if (!keep || name.empty() || name == old) return;
    if (name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) {
        MessageBoxW(hMain, L"A file name cannot contain \\ / : * ? \" < > |", L"Rename", MB_ICONINFORMATION);
        return;
    }
    std::wstring to = dir + L"\\" + name + gRenamePath.substr(dot);
    if (fileExists(to)) {
        MessageBoxW(hMain, (L"\"" + name + L"\" already exists in this folder.").c_str(), L"Rename",
                    MB_ICONINFORMATION);
        return;
    }
    if (!MoveFileW(gRenamePath.c_str(), to.c_str())) {
        MessageBoxW(hMain, L"The picture could not be renamed.", L"Rename", MB_ICONWARNING);
        return;
    }
    std::wstring side = sidecarFound(gRenamePath);
    if (!side.empty()) makeParent(sidecarOf(to)), MoveFileW(side.c_str(), sidecarOf(to).c_str());
    gSelPath = to;
    refreshLayouts();
    graphOverlayRefresh();  // "graph" in the name makes it a graph
}

LRESULT CALLBACK RenameProc(HWND e, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_GETDLGCODE) return DLGC_WANTALLKEYS | DefSubclassProc(e, msg, wp, lp);  // Enter / Esc reach it
    if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) {
        PostMessageW(hGallery, WM_RENAME_END, wp == VK_RETURN, 0);
        return 0;
    }
    if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;  // no beep
    if (msg == WM_KILLFOCUS) PostMessageW(hGallery, WM_RENAME_END, 1, 0);
    return DefSubclassProc(e, msg, wp, lp);
}

void beginRename(int i) {
    if (i < 0 || i >= (int)gShots.size()) return;
    endRename(true);
    gRenamePath = gShots[i].path;
    RECT r = gShots[i].labelR;
    gRename = CreateWindowExW(0, L"EDIT", gShots[i].label.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                              r.left, r.top, r.right - r.left, r.bottom - r.top, hGallery, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
    SendMessageW(gRename, WM_SETFONT, (WPARAM)fNormal, TRUE);
    SetWindowSubclass(gRename, RenameProc, 1, 0);
    SendMessageW(gRename, EM_SETSEL, 0, -1);
    SetFocus(gRename);
}

void deleteShot(int i) {  // to the Recycle Bin
    if (i < 0 || i >= (int)gShots.size()) return;
    std::wstring from = gShots[i].path + L'\0';  // list ends with two zeros
    for (const std::wstring& side : {sidecarOf(gShots[i].path), oldSidecarOf(gShots[i].path)})
        if (fileExists(side)) from += side + L'\0';
    SHFILEOPSTRUCTW op = {};
    op.hwnd = hMain;
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
    SHFileOperationW(&op);
    refreshLayouts();
    graphOverlayRefresh();
}

void menuShot(int i) {
    if (i < 0 || i >= (int)gShots.size()) return;
    std::wstring path = gShots[i].path;
    gMenuItems.clear();
    gMenuItems.reserve(5);
    gMenuItems.push_back({L"Open", false});
    gMenuItems.push_back({L"Show in folder", false});
    gMenuItems.push_back({L"Rename", false});
    gMenuItems.push_back({L"", true});
    gMenuItems.push_back({L"Delete (to Recycle Bin)", false});
    HMENU m = CreatePopupMenu();
    MENUINFO mi = {};
    mi.cbSize = sizeof mi;
    mi.fMask = MIM_BACKGROUND;
    mi.hbrBack = brCard;
    SetMenuInfo(m, &mi);
    const int ids[] = {1, 2, 4, 0, 3};
    for (int k = 0; k < 5; ++k)
        AppendMenuW(m, MF_OWNERDRAW | (gMenuItems[k].separator ? MF_DISABLED : 0), ids[k], (LPCWSTR)&gMenuItems[k]);
    POINT pt;
    GetCursorPos(&pt);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hMain, nullptr);
    DestroyMenu(m);
    if (cmd == 1) ShellExecuteW(hMain, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (cmd == 2)
        ShellExecuteW(hMain, L"open", L"explorer.exe", (L"/select,\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    if (cmd == 4) beginRename(i);
    if (cmd == 3) deleteShot(i);
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
        std::wstring sub =
            std::wstring(kGroups[gSelGroup].name) + L"  \u00b7  " + std::to_wstring(gShots.size()) +
            (gShots.size() == 1 ? L" screenshot" : L" screenshots");
        drawText(dc, sub, {x0, 40, rc.right, 62}, fNormal, C::Comment, DT_SINGLELINE | DT_VCENTER);
        int cols = std::max(1, (rw + kGap) / (kThumbW + kGap));
        IntersectClipRect(dc, x0, kHeadH, rc.right, rc.bottom);
        for (int i = 0; i < (int)gShots.size(); ++i) {
            Shot& s = gShots[i];
            int cx = x0 + (i % cols) * (kThumbW + kGap);
            int cy = kHeadH + (i / cols) * (kThumbH + kLabelH + kGap) - gRightScroll;
            RECT cr = {cx, cy, cx + kThumbW, cy + kThumbH};
            RECT hr = {cx, cy, cx + kThumbW, cy + kThumbH + kLabelH};  // picture and name
            s.labelR = {cx, cy + kThumbH, cx + kThumbW, cy + kThumbH + kLabelH};
            if (cy + kThumbH + kLabelH < 0 || cy > rc.bottom) {
                hits.push_back({hr, 2, i});
                continue;
            }
            bool sel = s.path == gSelPath;
            if (sel) fillRound(dc, hr, 10, C::Line, C::Line);
            fillRound(dc, cr, 10, C::Card, sel ? C::Purple : hot(2, i) ? C::Purple : C::Line);
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
            drawText(dc, s.label, s.labelR, fNormal, sel || hot(2, i) ? C::Fg : C::Comment,
                     DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
            hits.push_back({hr, 2, i});
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

// Pictures dragged onto the tab are copied into a zone: the one dropped on in the list, else the selected one.
// File names are kept (an outline "<date> seed <n>.png" stays one), " (2)" is added when the name is taken.
void dropFiles(HDROP drop) {
    POINT pt;
    DragQueryPoint(drop, &pt);
    int gi = gSelGroup;
    std::wstring zone = gSelZone;
    int hit = hitAt(pt);
    if (hit >= 0 && gHits[hit].kind == 1 && gHits[hit].idx < (int)gZones.size())
        gi = gOpen, zone = gZones[gHits[hit].idx];
    UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::wstring> files;
    for (UINT i = 0; i < n; ++i) {
        wchar_t f[MAX_PATH];
        if (DragQueryFileW(drop, i, f, MAX_PATH)) files.push_back(f);
    }
    DragFinish(drop);
    if (gi < 0 || zone.empty()) {
        MessageBoxW(hMain, L"Pick a zone on the left first (or drop the pictures on a zone's name).", L"Layouts",
                    MB_ICONINFORMATION);
        return;
    }
    makeDirs(gi, zone);
    std::wstring dir = zoneDir(gi, zone);
    int copied = 0, skipped = 0;
    for (auto& f : files) {
        std::wstring name = f.substr(f.find_last_of(L"\\/") + 1);
        if (!isPicture(name)) {
            ++skipped;
            continue;
        }
        size_t dot = name.find_last_of(L'.');
        std::wstring base = dir + L"\\" + name.substr(0, dot), ext = name.substr(dot);
        std::wstring to = base + ext;
        for (int i = 2; fileExists(to); ++i) to = base + L" (" + std::to_wstring(i) + L")" + ext;
        if (CopyFileW(f.c_str(), to.c_str(), TRUE)) {
            ++copied;
            std::wstring side = sidecarFound(f);
            if (!side.empty()) makeParent(sidecarOf(to)), CopyFileW(side.c_str(), sidecarOf(to).c_str(), TRUE);
        } 
        else ++skipped;
    }
    gOpen = gi;
    buildZones();
    selectZone(gi, zone);
    if (skipped)
        MessageBoxW(hMain,
                    (std::to_wstring(copied) + L" picture(s) copied to " + zone + L", " + std::to_wstring(skipped) +
                     L" skipped (only .png, .jpg, .jpeg and .bmp).")
                        .c_str(),
                    L"Layouts", MB_ICONINFORMATION);
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
            endRename(true);  // the box would stay where the name was
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
                gSelPath = gShots[ht.idx].path;  // select; double-click opens
            }
            gHot = -1;
            clampScroll();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONDOWN:
            SetFocus(h);  // for F2 / Enter / Delete
            return 0;
        case WM_LBUTTONDBLCLK: {  // on the picture: open it; on its name: rename
            POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
            int hit = hitAt(pt);
            if (hit < 0 || gHits[hit].kind != 2 || gHits[hit].idx >= (int)gShots.size()) return 0;
            int i = gHits[hit].idx;
            gSelPath = gShots[i].path;
            if (PtInRect(&gShots[i].labelR, pt)) beginRename(i);
            else ShellExecuteW(hMain, L"open", gShots[i].path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_GETDLGCODE:  // the main window's dialog keys would take Enter
            return DLGC_WANTALLKEYS;
        case WM_KEYDOWN: {  // like Explorer: F2 renames, Enter opens, Delete moves to the Recycle Bin
            int i = -1;
            for (int k = 0; k < (int)gShots.size(); ++k)
                if (gShots[k].path == gSelPath) i = k;
            if (i < 0) break;
            if (wp == VK_F2) beginRename(i);
            if (wp == VK_RETURN) ShellExecuteW(hMain, L"open", gShots[i].path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if (wp == VK_DELETE) deleteShot(i);
            return 0;
        }
        case WM_RBUTTONUP: {
            int hit = hitAt({(short)LOWORD(lp), (short)HIWORD(lp)});
            if (hit >= 0 && gHits[hit].kind == 2) {
                gSelPath = gShots[gHits[hit].idx].path;
                InvalidateRect(h, nullptr, FALSE);
                menuShot(gHits[hit].idx);
            }
            return 0;
        }
        case WM_DROPFILES:
            dropFiles((HDROP)wp);
            return 0;
        case WM_RENAME_END:
            endRename(wp != 0);
            return 0;
        case WM_CTLCOLOREDIT:
            SetBkColor((HDC)wp, C::Card);
            SetTextColor((HDC)wp, C::Fg);
            return (LRESULT)brCard;
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
    wc.style = CS_DBLCLKS;
    RegisterClassExW(&wc);
    hGallery = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 10, 10,
                               hMain, nullptr, wc.hInstance, nullptr);
    gPages[PG_LAYOUTS].push_back(hGallery);
    hLayHint = label(L"", ID_LAY_HINT, fNormal, PG_LAYOUTS, false, SS_ENDELLIPSIS);
    hLayFolder = btn(L"Open folder", ID_LAY_FOLDER, PG_LAYOUTS, Btn::Normal);
    hLayGraph = btn(L"Make graph", ID_LAY_GRAPH, PG_LAYOUTS, Btn::Accent);
    gLayoutCrop = GetPrivateProfileIntW(L"layouts", L"SelectArea", 0, iniPath().c_str()) != 0;
}

void toggleLayoutCrop() {
    gLayoutCrop = !gLayoutCrop;
    WritePrivateProfileStringW(L"layouts", L"SelectArea", gLayoutCrop ? L"1" : L"0", iniPath().c_str());
    InvalidateRect(hLayCropChk, nullptr, FALSE);
    refreshLayouts();
}

void refreshLayouts() {
    if (!hGallery) return;
    std::wstring key = hotkeyName(HK_LAYOUT);
    setTextIfChanged(hLayHint, gHotkeysOn && key != L"Not set"
                                   ? std::wstring()
                                   : std::wstring(L"Set a key for Layout screenshot in Options to "
                                                  L"take screenshots in game."));
    buildZones();
    loadShots();
    clampScroll();
    InvalidateRect(hGallery, nullptr, FALSE);
}

// What the open group's folders and the selected zone hold (names, sizes, times): changes when pictures are added,
// removed or renamed outside the app (Explorer).
static std::wstring layoutsState() {
    std::wstring st;
    auto scan = [&](const std::wstring& dir) {
        WIN32_FIND_DATAW fd;
        HANDLE f = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (f == INVALID_HANDLE_VALUE) return;
        do {
            st += fd.cFileName;
            st += L'|' + std::to_wstring(fd.nFileSizeLow) + L'|' + std::to_wstring(fd.ftLastWriteTime.dwLowDateTime) +
                  L'\n';
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    };
    if (gOpen >= 0) {
        std::wstring g = layoutsDir() + L"\\" + kGroups[gOpen].name;
        scan(g);
        for (auto& z : listDir(g, true)) scan(g + L"\\" + z);  // picture counts in the zone list
    }
    if (gSelGroup >= 0 && !gSelZone.empty()) scan(zoneDir(gSelGroup, gSelZone));
    return st;
}

// Called every 0.5 s: while the Layouts tab is shown, its folders are checked every second and the tab is
// refreshed when they changed.
void layoutsWatch() {
    static std::wstring last;
    static int n = 0;
    if (gPage != PG_LAYOUTS || !hGallery || gRename || ++n % 2) return;
    std::wstring st = layoutsState();
    if (st == last) return;
    bool first = last.empty();
    last = st;
    if (first) return;
    if (!gSelPath.empty() && !fileExists(gSelPath)) gSelPath.clear();
    refreshLayouts();
    graphOverlayRefresh();
}

// Points graph of the selected zone from its outline pictures (src/core/outline_graph), saved as
// "graph <date>.png" in the zone's folder. Runs on a worker thread.
void makeLayoutGraph() {
    if (gSelGroup < 0 || gSelZone.empty()) {
        MessageBoxW(hMain, L"Pick a zone on the left first.", L"Make graph", MB_ICONINFORMATION);
        return;
    }
    std::wstring dir = zoneDir(gSelGroup, gSelZone);
    std::vector<std::wstring> files;  // outline pictures: "<date> seed <seed>.png"
    for (auto& f : listDir(dir, false))
        if (f.find(L" seed ") != std::wstring::npos && !isGraph(f)) files.push_back(dir + L"\\" + f);
    if (files.size() < 2) {
        MessageBoxW(hMain,
                    (L"A graph needs outlines of at least 2 runs of this zone; there " +
                     std::wstring(files.size() == 1 ? L"is 1." : L"are " + std::to_wstring(files.size()) + L"."))
                        .c_str(),
                    L"Make graph", MB_ICONINFORMATION);
        return;
    }
    SetWindowTextW(hLayGraph, L"Making graph...");
    EnableWindow(hLayGraph, FALSE);
    int gi = gSelGroup;
    std::wstring zone = gSelZone;
    std::thread([files, dir, gi, zone] {
        gdipInit();
        std::vector<outline::Outline> outs;
        {
            std::lock_guard<std::mutex> lock(gOutlineBusy);
            std::vector<uint32_t> seeds;
            for (auto& f : files) {
                uint32_t s = (uint32_t)wcstoul(f.c_str() + f.find(L" seed ") + 6, nullptr, 10);
                if (s && std::find(seeds.begin(), seeds.end(), s) == seeds.end()) seeds.push_back(s);
            }
            for (uint32_t s : seeds) {
                std::vector<std::wstring> paths;
                std::vector<outline::Outline> o;
                loadOutlines(dir, s, paths, o);
                bool old = false;  // made before points were kept: made again from the screenshots in pieces
                for (auto& p : paths) old |= outline::oldLabels(readText(sidecarFound(p)));
                std::wstring pieces = dir + L"\\pieces\\" + std::to_wstring(s);
                auto shots = listDir(pieces, false);
                if (old && !shots.empty()) {
                    std::sort(shots.begin(), shots.end());  // by date
                    std::vector<outline::Outline> re;
                    for (auto& f : shots) {
                        outline::Image img;
                        if (withPicture(pieces + L"\\" + f, [&](Gdiplus::Bitmap& b) { return toImage(b, img); }))
                            outline::add(re, img);
                    }
                    if (!re.empty()) {
                        for (auto& p : paths) deleteOutline(p);
                        std::wstring tag = L" seed " + std::to_wstring(s);
                        std::wstring date = shots[0].substr(0, shots[0].find_last_of(L'.'));
                        size_t paren = date.find(L" (");
                        if (paren != std::wstring::npos) date.resize(paren);
                        int keep = keepMostDetailed(re);  // one per instance
                        if (keep >= 0) saveOutline(re[keep], freePath(dir + L"\\" + date + tag));
                        o.clear();
                        if (keep >= 0) o.push_back(std::move(re[keep]));
                        paths.clear();
                    }
                }
                int keep = keepMostDetailed(o);  // one per instance
                if (keep >= 0) {
                    if (o.size() > 1 && paths.size() == o.size())
                        for (size_t i = 0; i < paths.size(); ++i)
                            if ((int)i != keep) deleteOutline(paths[i]);
                    outs.push_back(std::move(o[keep]));
                }
            }
        }
        outline::Graph gr = outline::pointsGraph(outs);
        auto* r = new ShotDone{L"", zone, gi, true, gr.used, (int)outs.size(), {}, 0};
        if (!gr.img.empty()) {
            SYSTEMTIME t;
            GetLocalTime(&t);
            wchar_t d[64];
            swprintf(d, 64, L"graph %04d-%02d-%02d %02d-%02d-%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                     t.wSecond);
            // caption and legend (how many runs had each point, and whether it stays in one place)
            std::vector<std::pair<std::wstring, POINT>> texts;
            texts.push_back({std::to_wstring(gr.used) + L" of " + std::to_wstring(outs.size()) +
                                 L" outlines lined up on the entrances  \u00b7  a dot where each run had the point  \u00b7  "
                                 L"numbered rings: spots the inner places swap between",
                             POINT{16, 12}});
            for (auto& t : gr.texts) texts.push_back({W(t.text), POINT{t.x, t.y}});
            if (savePng(gr.img, freePath(dir + L"\\" + d), &texts)) r->what = L"graph";
        }
        if (!PostMessageW(hMain, WM_LAYOUT_DONE, 0, (LPARAM)r)) delete r;
    }).detach();
}

// Graph pictures of the zone the player is in, newest first (for the GraphOverlay).
std::vector<std::wstring> currentZoneGraphs() {
    std::vector<std::wstring> r;
    int gi = groupOf(g.location);
    if (gi < 0) return r;
    std::wstring dir = zoneDir(gi, resolveZone(gi, W(g.locationName.empty() ? g.nameOf(g.location) : g.locationName)));
    for (auto& f : listDir(dir, false))
        if (isGraph(f)) r.push_back(dir + L"\\" + f);
    std::sort(r.begin(), r.end(), [](auto& a, auto& b) { return lowerW(a) > lowerW(b); });
    return r;
}

HBITMAP layoutPicture(const std::wstring& path, int maxW, int maxH, int& w, int& h) {
    return makeThumb(path, maxW, maxH, w, h);
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
    if (!gLayoutCrop) return captureGame(gi, gPendZone, gPendSeed);
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
void layoutDone(LPARAM lp) {
    std::unique_ptr<ShotDone> r((ShotDone*)lp);
    if (!r->graph) {
        shotSaved(r->what, r->gi, r->zone);
        if (!r->outline.empty() && r->seed == currentSeed()) layoutOverlayShow(r->outline, r->seed);
        return;
    }
    SetWindowTextW(hLayGraph, L"Make graph");
    EnableWindow(hLayGraph, TRUE);
    if (r->what.empty()) {
        MessageBoxW(hMain,
                    (L"The outlines could not be lined up (" + std::to_wstring(r->runs) +
                     L" outlines). Each needs at least 2 named points (exits, waypoint) that another outline has "
                     L"too.")
                        .c_str(),
                    L"Make graph", MB_ICONINFORMATION);
        return;
    }
    trayBalloon(L"Layout graph made", std::to_wstring(r->used) + L" of " + std::to_wstring(r->runs) +
                                          L" outlines lined up  \u00b7  " + r->zone);
    if (gSelGroup == r->gi && gSelZone == r->zone) refreshLayouts();
    graphOverlayRefresh();
}

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
    int gi = gPendGroup;
    std::wstring zone = gPendZone;
    uint32_t seed = gPendSeed;
    stopPending();
    if (hb) storeAsync(hb, gi, zone, seed);  // copies the picture before the clipboard is closed
    CloseClipboard();
    if (!hb) shotSaved(L"", gi, zone);
}
