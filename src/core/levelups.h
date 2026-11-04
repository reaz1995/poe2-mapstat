#pragma once
// Level ups of a character, from the log (": Name (Class) is now level N"): when, where, and how long each level
// took. Kept per character in a small text file; the whole log can be read to find the earlier ones.
#include <cstdint>
#include <functional>
#include <istream>
#include <string>
#include <vector>

struct LevelUp {
    std::string date, time;  // 2026/11/03, 13:22:15 (log time)
    std::string areaId, areaName;  // where the character was
    int areaLevel = 0;
    int from = 0, to = 0;  // level before and after
    bool oneZone = false;  // the whole level was gained there: no area entered since the level up before
};

// Seconds of a log date and time (2026/11/03, 13:22:15), for differences; 0 when not readable.
int64_t logSeconds(const std::string& date, const std::string& time);

// All level ups of `name` (not case sensitive) in a log, oldest first. `nextLine` gives the log's lines one by one
// (false at the end).
std::vector<LevelUp> scanLevelUps(const std::function<bool(std::string&)>& nextLine, const std::string& name);
std::vector<LevelUp> scanLevelUps(std::istream& log, const std::string& name);

// Adds level ups not there yet (same date, time and level), keeps them oldest first; for ones already there,
// oneZone is taken from `more` (the log, read again, knows it better).
void mergeLevelUps(std::vector<LevelUp>& into, const std::vector<LevelUp>& more);

std::string saveLevelUps(const std::vector<LevelUp>& ups);  // one tab-separated line each
std::vector<LevelUp> loadLevelUps(const std::string& text);

// Time the level took: from the level up before it (to the level this one starts from), else -1.
int64_t levelSeconds(const std::vector<LevelUp>& ups, size_t i);
