// Small helpers: string conversion, time, files, settings paths.
#include "app.h"

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
