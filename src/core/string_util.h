// Small string helpers shared by the core.
#pragma once
#include <string>

inline bool startsWith(const std::string& s, const char* p) {
    return s.compare(0, std::char_traits<char>::length(p), p) == 0;
}
inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
