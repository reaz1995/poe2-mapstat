// One parsed log line: "2026/11/01 21:13:15 604497796 2caa229f [DEBUG Client 1234] message".
#pragma once
#include <cstdint>
#include <string>

struct LogLine {
    std::string date;   // 2026/11/01
    std::string time;   // 21:13:15
    uint32_t tick = 0;  // ms counter (GetTickCount of the PoE machine)
    std::string hash;   // e.g. 3ef23348 = NPC dialogue
    std::string level;  // INFO / DEBUG / WARN / CRIT
    std::string msg;
};
bool parseLine(const std::string& s, LogLine& out);
