// Minimal YAML subset for themes: `key: value`, nested maps by indentation, `#` comments,
// quoted or plain strings, booleans, colours "#rrggbb" / "#rgb".
#include "theme.h"

#include <cctype>
#include <sstream>

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
bool isHex(const std::string& s) {
    for (char c : s)
        if (!std::isxdigit((unsigned char)c)) return false;
    return !s.empty();
}

// value part after "key:" -> unquoted scalar (comments removed)
std::string scalar(const std::string& raw) {
    std::string v = trim(raw);
    if (!v.empty() && (v[0] == '"' || v[0] == '\'')) {
        size_t e = v.find(v[0], 1);
        return e == std::string::npos ? v.substr(1) : v.substr(1, e - 1);
    }
    // plain "#282a36" is accepted as a colour even though real YAML would read it as a comment
    if (v.size() >= 4 && v[0] == '#') {
        size_t n = 1;
        while (n < v.size() && std::isxdigit((unsigned char)v[n])) ++n;
        if ((n == 7 || n == 4) && (n == v.size() || std::isspace((unsigned char)v[n]))) return v.substr(0, n);
    }
    if (!v.empty() && v[0] == '#') return "";
    size_t c = v.find(" #");
    if (c != std::string::npos) v = v.substr(0, c);
    return trim(v);
}

bool colour(const std::string& v, uint32_t& out) {
    if (v.size() < 2 || v[0] != '#') return false;
    std::string h = v.substr(1);
    if (!isHex(h)) return false;
    if (h.size() == 3) h = {h[0], h[0], h[1], h[1], h[2], h[2]};
    if (h.size() != 6) return false;
    out = (uint32_t)std::stoul(h, nullptr, 16);
    return true;
}

}  // namespace

bool parseTheme(const std::string& text, Theme& t, std::string& err, std::string& warn) {
    struct Col { const char* key; uint32_t Theme::*m; };
    static const Col cols[] = {
        {"background", &Theme::bg}, {"bg", &Theme::bg}, {"card", &Theme::card}, {"darkest", &Theme::darkest},
        {"line", &Theme::line}, {"stripe", &Theme::stripe}, {"foreground", &Theme::fg}, {"fg", &Theme::fg},
        {"comment", &Theme::comment}, {"cyan", &Theme::cyan}, {"green", &Theme::green},
        {"orange", &Theme::orange}, {"pink", &Theme::pink}, {"purple", &Theme::purple}, {"red", &Theme::red},
        {"yellow", &Theme::yellow},
    };
    Theme r = t;  // apply only if the whole file is valid
    std::istringstream in(text);
    std::string ln, section;
    int no = 0;
    auto fail = [&](const std::string& m) {
        err = "line " + std::to_string(no) + ": " + m;
        return false;
    };
    while (std::getline(in, ln)) {
        ++no;
        if (no == 1 && ln.size() >= 3 && (unsigned char)ln[0] == 0xEF) ln = ln.substr(3);  // UTF-8 BOM
        std::string s = trim(ln);
        if (s.empty() || s[0] == '#' || s == "---" || s == "...") continue;
        if (ln.find('\t') != std::string::npos && ln.find_first_not_of(" \t") > ln.find('\t'))
            return fail("tabs are not allowed for indentation");
        size_t indent = ln.find_first_not_of(' ');
        size_t colon = s.find(':');
        if (colon == std::string::npos) return fail("expected 'key: value'");
        std::string key = lower(trim(s.substr(0, colon)));
        std::string val = scalar(s.substr(colon + 1));
        if (indent == 0) {
            if (val.empty()) {  // section header
                section = key;
                if (section != "colors" && section != "colours" && section != "fonts")
                    warn += (warn.empty() ? "" : ", ") + key;
                continue;
            }
            section.clear();
            if (key == "name") r.name = val;
            else if (key == "author") r.author = val;
            else if (key == "mode") {
                std::string m = lower(val);
                if (m != "dark" && m != "light") return fail("mode must be dark or light");
                r.dark = m == "dark";
            } else
                warn += (warn.empty() ? "" : ", ") + key;
            continue;
        }
        if (section == "colors" || section == "colours") {
            const Col* c = nullptr;
            for (auto& x : cols)
                if (key == x.key) c = &x;
            if (!c) {
                warn += (warn.empty() ? "" : ", ") + section + "." + key;
                continue;
            }
            uint32_t v;
            if (!colour(val, v)) return fail(key + ": '" + val + "' is not a #rrggbb colour");
            r.*(c->m) = v;
        } else if (section == "fonts") {
            if (val.empty()) return fail(key + ": font name missing");
            if (key == "ui") r.fontUi = val;
            else if (key == "mono") r.fontMono = val;
            else warn += (warn.empty() ? "" : ", ") + section + "." + key;
        } else if (section.empty()) {
            return fail("unexpected indentation");
        }
    }
    t = r;
    return true;
}
