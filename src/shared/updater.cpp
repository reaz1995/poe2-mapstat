#include "updater.h"

#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <cstdio>
#include <vector>

namespace {

std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// HTTPS GET (follows redirects: GitHub sends asset downloads to its CDN). Certificate checks stay on.
bool httpGet(const std::string& url, std::string& out, std::string& err) {
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256], path[2048];
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) {
        err = "bad url";
        return false;
    }
    HINTERNET s = WinHttpOpen(L"Poe2MapStat-Updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(L"Poe2MapStat-Updater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) {
        err = "WinHttpOpen failed";
        return false;
    }
    WinHttpSetTimeouts(s, 10000, 10000, 15000, 60000);
    bool ok = false;
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         WINHTTP_FLAG_SECURE)
                    : nullptr;
    if (r) {
        const wchar_t* hdr = L"Accept: application/vnd.github+json, application/octet-stream\r\n";
        if (WinHttpSendRequest(r, hdr, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(r, nullptr)) {
            DWORD status = 0, sz = sizeof status;
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &status, &sz, WINHTTP_NO_HEADER_INDEX);
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(r, &avail) || avail == 0) break;
                std::vector<char> buf(avail);
                DWORD got = 0;
                if (!WinHttpReadData(r, buf.data(), avail, &got) || got == 0) break;
                out.append(buf.data(), got);
                if (out.size() > 64u * 1024 * 1024) break;  // safety limit, responses are much smaller
            }
            if (status == 200) ok = true;
            else err = "HTTP " + std::to_string(status);
        } else {
            err = "no connection (" + std::to_string(GetLastError()) + ")";
        }
        WinHttpCloseHandle(r);
    } else {
        err = "connect failed";
    }
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}

// Minimal JSON helpers for the GitHub release object.
std::string jsonString(const std::string& j, size_t from, const char* key, size_t* at = nullptr) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k, from);
    if (p == std::string::npos) return "";
    p = j.find(':', p + k.size());
    if (p == std::string::npos) return "";
    p = j.find_first_not_of(" \t\r\n", p + 1);
    if (p == std::string::npos || j[p] != '"') return "";
    std::string v;
    for (size_t i = p + 1; i < j.size(); ++i) {
        char ch = j[i];
        if (ch == '"') {
            if (at) *at = i;
            return v;
        }
        if (ch == '\\' && i + 1 < j.size()) {
            char e = j[++i];
            if (e == 'n') v += '\n';
            else if (e == 'r') {}
            else if (e == 't') v += '\t';
            else if (e == 'u' && i + 4 < j.size()) {  // keep ASCII, replace the rest
                unsigned cp = (unsigned)strtoul(j.substr(i + 1, 4).c_str(), nullptr, 16);
                v += cp < 128 ? (char)cp : '?';
                i += 4;
            } else v += e;
        } else {
            v += ch;
        }
    }
    return "";
}
uint64_t jsonNumber(const std::string& j, size_t from, size_t to, const char* key) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k, from);
    if (p == std::string::npos || p > to) return 0;
    p = j.find(':', p);
    return p == std::string::npos ? 0 : strtoull(j.c_str() + p + 1, nullptr, 10);
}

std::string sha256Hex(const std::string& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    unsigned char out[32] = {};
    std::string hex;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
            BCryptHashData(h, (PUCHAR)data.data(), (ULONG)data.size(), 0);
            if (BCryptFinishHash(h, out, sizeof out, 0) == 0) {
                char b[3];
                for (unsigned char x : out) {
                    snprintf(b, sizeof b, "%02x", x);
                    hex += b;
                }
            }
            BCryptDestroyHash(h);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return hex;
}

std::vector<int> versionParts(const std::string& v) {
    std::vector<int> r;
    size_t i = (!v.empty() && (v[0] == 'v' || v[0] == 'V')) ? 1 : 0;
    while (i < v.size() && isdigit((unsigned char)v[i])) {
        int n = 0;
        while (i < v.size() && isdigit((unsigned char)v[i])) n = n * 10 + (v[i++] - '0');
        r.push_back(n);
        if (i < v.size() && v[i] == '.') ++i;
        else break;
    }
    return r;
}

}  // namespace

bool isNewer(const std::string& tag, const std::string& current) {
    auto a = versionParts(tag), b = versionParts(current);
    if (a.empty()) return false;
    while (a.size() < b.size()) a.push_back(0);
    while (b.size() < a.size()) b.push_back(0);
    return a > b;
}

bool fetchLatestRelease(const std::string& repo, ReleaseInfo& out, std::string& err) {
    std::string j;
    if (!httpGet("https://api.github.com/repos/" + repo + "/releases/latest", j, err)) {
        if (err == "HTTP 404") err = "no release published yet (or the repository is private)";
        return false;
    }
    out.tag = jsonString(j, 0, "tag_name");
    out.body = jsonString(j, 0, "body");
    out.pageUrl = jsonString(j, 0, "html_url");
    size_t assets = j.find("\"assets\"");
    if (out.tag.empty() || assets == std::string::npos) {
        err = "unexpected answer from GitHub";
        return false;
    }
    // first asset whose name ends with .exe
    for (size_t p = assets; (p = j.find("\"name\"", p)) != std::string::npos;) {
        size_t end = 0;
        std::string name = jsonString(j, p, "name", &end);
        if (name.size() > 4 && _stricmp(name.c_str() + name.size() - 4, ".exe") == 0) {
            size_t next = j.find("\"name\"", end);  // fields of this asset come before the next asset's name
            if (next == std::string::npos) next = j.size();
            out.assetUrl = jsonString(j, end, "browser_download_url");
            std::string dg = jsonString(j, end, "digest");
            size_t dgAt = j.find("\"digest\"", end);
            out.digest = (dgAt != std::string::npos && dgAt < next) ? dg : "";
            out.size = jsonNumber(j, end, next, "size");
            break;
        }
        p = end ? end : p + 6;
    }
    if (out.assetUrl.empty()) {
        err = "the latest release has no .exe file attached";
        return false;
    }
    return true;
}

bool downloadAsset(const ReleaseInfo& r, std::string& data, std::string& err) {
    if (!httpGet(r.assetUrl, data, err)) return false;
    if (data.size() < 64 * 1024 || data[0] != 'M' || data[1] != 'Z') {
        err = "the downloaded file is not a valid program";
        return false;
    }
    if (r.size && data.size() != r.size) {
        err = "download incomplete";
        return false;
    }
    if (r.digest.rfind("sha256:", 0) == 0 && sha256Hex(data) != r.digest.substr(7)) {
        err = "checksum mismatch - the file was not installed";
        return false;
    }
    return true;
}
