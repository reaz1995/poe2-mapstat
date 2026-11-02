// Parsing of one LatestClient.txt / Client.txt line.
#include "log_line.h"

#include <cstring>

#include "string_util.h"

// "2026/11/01 21:13:15 604497796 2caa229f [DEBUG Client 1234] message"
bool parseLine(const std::string& s, LogLine& o) {
    if (s.size() < 30 || s[4] != '/' || s[10] != ' ') return false;
    size_t p1 = 10, p2 = s.find(' ', p1 + 1);           // time
    if (p2 == std::string::npos) return false;
    size_t p3 = s.find(' ', p2 + 1);                      // tick
    if (p3 == std::string::npos) return false;
    size_t p4 = s.find(' ', p3 + 1);                      // hash
    if (p4 == std::string::npos || p4 + 1 >= s.size() || s[p4 + 1] != '[') return false;
    size_t close = s.find(']', p4 + 1);
    if (close == std::string::npos) return false;
    o.date = s.substr(0, 10);
    o.time = s.substr(p1 + 1, p2 - p1 - 1);
    o.tick = (uint32_t)strtoul(s.c_str() + p2 + 1, nullptr, 10);
    o.hash = s.substr(p3 + 1, p4 - p3 - 1);
    size_t lvEnd = s.find(' ', p4 + 2);
    o.level = s.substr(p4 + 2, (lvEnd < close ? lvEnd : close) - p4 - 2);
    size_t m = close + 1;
    if (m < s.size() && s[m] == ' ') ++m;
    o.msg = s.substr(m);
    while (!o.msg.empty() && (o.msg.back() == '\r' || o.msg.back() == '\n')) o.msg.pop_back();
    return true;
}
