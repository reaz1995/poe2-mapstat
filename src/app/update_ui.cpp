// Self-update from GitHub Releases (Release builds with GITHUB_REPO in .env).
#include "app.h"

#ifdef UPDATE_REPO

static LRESULT CALLBACK ProgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
static void progress(const std::wstring& text, UINT closeAfterMs = 0);
static void progressClose();

static std::atomic<bool> gUpdBusy{false};

// small themed status window over the app: "Checking..." -> "Updating..." (closes itself)
static HWND hProg = nullptr;

static std::wstring gProgText;

static LRESULT CALLBACK ProgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        FillRect(dc, &rc, brBg);
        fillRound(dc, rc, 12, C::Card, C::Purple);
        SetBkMode(dc, TRANSPARENT);
        SelectObject(dc, fBold);
        SetTextColor(dc, C::Fg);
        DrawTextW(dc, gProgText.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(h, &ps);
        return 0;
    }
    if (msg == WM_TIMER) {  // auto close
        KillTimer(h, 1);
        DestroyWindow(h);
        return 0;
    }
    if (msg == WM_DESTROY) hProg = nullptr;
    return DefWindowProcW(h, msg, wp, lp);
}

static void progress(const std::wstring& text, UINT closeAfterMs) {
    if (!hProg) {
        static bool reg = false;
        if (!reg) {
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof wc;
            wc.lpfnWndProc = ProgProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursor(nullptr, IDC_WAIT);
            wc.lpszClassName = L"Poe2MapStatProgress";
            RegisterClassExW(&wc);
            reg = true;
        }
        RECT mr;
        GetWindowRect(hMain, &mr);
        const int w = 320, hh = 64;
        hProg = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"Poe2MapStatProgress", L"", WS_POPUP,
                                (mr.left + mr.right - w) / 2, (mr.top + mr.bottom - hh) / 2, w, hh, hMain, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
        SetWindowRgn(hProg, CreateRoundRectRgn(0, 0, w + 1, hh + 1, 12, 12), FALSE);
        ShowWindow(hProg, SW_SHOWNOACTIVATE);
    }
    gProgText = text;
    InvalidateRect(hProg, nullptr, TRUE);
    UpdateWindow(hProg);
    if (closeAfterMs) SetTimer(hProg, 1, closeAfterMs, nullptr);
}

static void progressClose() {
    if (hProg) DestroyWindow(hProg);
}

// manual (button): no questions - check, update, restart. Automatic (on start): asks before installing.
void updCheck(bool manual) {
    if (gUpdBusy.exchange(true)) return;
    if (manual) progress(L"Checking for updates...");
    std::thread([manual] {
        auto* r = new UpdResult;
        r->manual = manual;
        r->ok = fetchLatestRelease(UPDATE_REPO, r->rel, r->err);
        PostMessageW(hMain, WM_UPD_FOUND, 0, (LPARAM)r);
    }).detach();
}

void updFound(UpdResult* r) {
    std::unique_ptr<UpdResult> keep(r);
    if (!r->ok) {
        gUpdBusy = false;
        progressClose();
        if (r->manual) MessageBoxW(hMain, (L"Update check failed: " + W(r->err)).c_str(), L"Poe2 | MapStat", MB_ICONWARNING);
        return;
    }
    if (!isNewer(r->rel.tag, APP_VERSION)) {
        gUpdBusy = false;
        if (r->manual) progress(L"Up to date  (v" + W(APP_VERSION) + L")", 1800);
        return;
    }
    if (!r->manual) {  // found on start: ask first
        std::string notes = r->rel.body.size() > 700 ? r->rel.body.substr(0, 700) + "..." : r->rel.body;
        std::wstring q = L"Version " + W(r->rel.tag) + L" is available (you have v" + W(APP_VERSION) + L").\n\n" +
                         W(notes) + L"\n\nDownload and install it now? The app restarts, your data is saved first.";
        if (MessageBoxW(hMain, q.c_str(), L"Update available", MB_YESNO | MB_ICONINFORMATION) != IDYES) {
            gUpdBusy = false;
            return;
        }
    }
    progress(L"Updating to " + W(r->rel.tag) + L"...");
    ReleaseInfo rel = r->rel;
    std::thread([rel] {
        auto* d = new UpdResult;
        d->rel = rel;
        d->ok = downloadAsset(rel, d->data, d->err);
        PostMessageW(hMain, WM_UPD_DOWNLOADED, 0, (LPARAM)d);
    }).detach();
}

// write next to the exe, swap with the running exe (a running exe can be renamed), restart
void updInstall(UpdResult* r) {
    std::unique_ptr<UpdResult> keep(r);
    gUpdBusy = false;
    std::wstring exe = gExeDir + L"\\Poe2MapStat.exe";
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    exe = self;
    std::wstring fresh = exe + L".new", old = gExeDir + L"\\Poe2MapStat.old.exe";
    auto fail = [&](const std::wstring& why) {
        progressClose();
        std::wstring m = L"The update could not be installed: " + why + L"\n\nOpen the release page to download it yourself?";
        if (MessageBoxW(hMain, m.c_str(), L"Update", MB_YESNO | MB_ICONWARNING) == IDYES)
            ShellExecuteW(hMain, L"open", W(r->rel.pageUrl).c_str(), nullptr, nullptr, SW_SHOW);
    };
    if (!r->ok) return fail(W(r->err));
    FILE* f = _wfopen(fresh.c_str(), L"wb");
    if (!f) return fail(L"no write access to the app folder");
    bool wrote = fwrite(r->data.data(), 1, r->data.size(), f) == r->data.size();
    fclose(f);
    if (!wrote) return fail(L"writing the file failed");
    DeleteFileW(old.c_str());
    if (!MoveFileExW(exe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(fresh.c_str());
        return fail(L"the running program could not be replaced");
    }
    if (!MoveFileExW(fresh.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExW(old.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);  // put the old one back
        return fail(L"the new program could not be put in place");
    }
    // save everything, free the single-instance lock, start the new exe, quit
    if (hOverlay) saveOverlayPos();
    g.suspend(nowTick());
    saveData();
    if (gMutex) {
        ReleaseMutex(gMutex);
        CloseHandle(gMutex);
        gMutex = nullptr;
    }
    ShellExecuteW(nullptr, L"open", exe.c_str(), L"--updated", gExeDir.c_str(), SW_SHOWNORMAL);
    gExiting = true;
    DestroyWindow(hMain);
}

#endif  // UPDATE_REPO
