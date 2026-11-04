// Small helpers: string conversion, time, files, settings paths.
#include "app.h"

#include <shobjidl.h>

std::wstring W(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string U8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

void setTextIfChanged(HWND h, const std::wstring& t) {
    static wchar_t buf[4096];
    GetWindowTextW(h, buf, 4096);
    if (t != buf) SetWindowTextW(h, t.c_str());
}

std::wstring iniPath() { return gDataDir + L"\\settings.ini"; }

std::string areasPath() { return U8(gDataDir + L"\\areas.txt"); }

std::string nowClock() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char t[16];
    snprintf(t, sizeof t, "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return t;
}

// Game and tracker run on the same PC, so GetTickCount() matches the log's tick column.
uint32_t nowTick() {
    uint32_t t = GetTickCount();
    uint32_t d = t - g.lastTick;
    return d < 12u * 3600u * 1000u ? t : g.lastTick;
}

std::wstring lowerW(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

std::wstring secs(double s) {
    char b[16];
    snprintf(b, sizeof b, "%.1fs", s);
    return W(b);
}

// ---- files and registry
std::string readFileUtf8(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return "";
    std::string d;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.append(buf, n);
    fclose(f);
    return d;
}

std::wstring getText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s((size_t)n + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize((size_t)n);
    return s;
}

bool fileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring regString(HKEY root, const wchar_t* key, const wchar_t* value) {
    wchar_t buf[MAX_PATH];
    DWORD sz = sizeof buf;
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buf, &sz) != ERROR_SUCCESS) return L"";
    return buf;
}

// ---- file / folder pickers (Windows' own dialogs); empty when cancelled
static std::vector<std::wstring> runPicker(HWND owner, const wchar_t* title, DWORD opts, const wchar_t* filterName,
                                           const wchar_t* filterSpec, const std::wstring& start) {
    std::vector<std::wstring> out;
    IFileOpenDialog* d = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return out;
    DWORD o = 0;
    d->GetOptions(&o);
    d->SetOptions(o | opts | FOS_FORCEFILESYSTEM);
    d->SetTitle(title);
    if (filterSpec) {
        COMDLG_FILTERSPEC f[] = {{filterName, filterSpec}, {L"All files", L"*.*"}};
        d->SetFileTypes(2, f);
    }
    IShellItem* si = nullptr;
    if (!start.empty() && SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&si)))) {
        d->SetFolder(si);
        si->Release();
    }
    if (SUCCEEDED(d->Show(owner))) {
        IShellItemArray* items = nullptr;
        if (SUCCEEDED(d->GetResults(&items))) {
            DWORD n = 0;
            items->GetCount(&n);
            for (DWORD i = 0; i < n; ++i) {
                IShellItem* it = nullptr;
                wchar_t* p = nullptr;
                if (SUCCEEDED(items->GetItemAt(i, &it)) && SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p)))
                    out.push_back(p);
                CoTaskMemFree(p);
                if (it) it->Release();
            }
            items->Release();
        }
    }
    d->Release();
    return out;
}

std::vector<std::wstring> pickFiles(HWND owner, const wchar_t* title, const wchar_t* filterName,
                                    const wchar_t* filterSpec, const std::wstring& start) {
    return runPicker(owner, title, FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST, filterName, filterSpec, start);
}

std::wstring pickFolder(HWND owner, const wchar_t* title, const std::wstring& start) {
    auto r = runPicker(owner, title, FOS_PICKFOLDERS, nullptr, nullptr, start);
    return r.empty() ? L"" : r[0];
}

std::wstring pickSaveFile(HWND owner, const wchar_t* title, const std::wstring& folder, const std::wstring& name,
                          const wchar_t* ext) {
    std::wstring out;
    IFileSaveDialog* d = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return out;
    DWORD o = 0;
    d->GetOptions(&o);
    d->SetOptions(o | FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM);
    d->SetTitle(title);
    std::wstring spec = std::wstring(L"*.") + ext;
    COMDLG_FILTERSPEC f[] = {{spec.c_str(), spec.c_str()}};
    d->SetFileTypes(1, f);
    d->SetDefaultExtension(ext);
    d->SetFileName(name.c_str());
    IShellItem* si = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&si)))) {
        d->SetFolder(si);
        si->Release();
    }
    if (SUCCEEDED(d->Show(owner))) {
        IShellItem* r = nullptr;
        wchar_t* p = nullptr;
        if (SUCCEEDED(d->GetResult(&r)) && SUCCEEDED(r->GetDisplayName(SIGDN_FILESYSPATH, &p))) out = p;
        CoTaskMemFree(p);
        if (r) r->Release();
    }
    d->Release();
    return out;
}
