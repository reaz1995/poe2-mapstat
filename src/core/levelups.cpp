// Level ups of a character (see levelups.h).
#include "levelups.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

#include "log_line.h"
#include "string_util.h"

int64_t logSeconds(const std::string& date, const std::string& time) {
    int y, mo, d, h = 0, mi = 0, s = 0;
    if (sscanf(date.c_str(), "%d/%d/%d", &y, &mo, &d) != 3) return 0;
    sscanf(time.c_str(), "%d:%d:%d", &h, &mi, &s);
    // days from 1970-01-01 (proleptic Gregorian calendar)
    y -= mo <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + doe - 719468;
    return days * 86400 + h * 3600 + mi * 60 + s;
}

static bool sameName(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

std::vector<LevelUp> scanLevelUps(std::istream& log, const std::string& name) {
    return scanLevelUps([&](std::string& line) { return (bool)std::getline(log, line); }, name);
}

std::vector<LevelUp> scanLevelUps(const std::function<bool(std::string&)>& nextLine, const std::string& name) {
    std::vector<LevelUp> r;
    std::string line, areaId, areaName;
    int areaLevel = 0;
    bool naming = false;  // the next scene name is the area's
    bool moved = true;    // an area was entered since the last level up (unknown before the first one: yes)
    LogLine l;
    while (nextLine(line)) {
        // cheap checks first: most lines are none of these
        bool gen = line.find("Generating level ") != std::string::npos;
        bool scene = !gen && naming && line.find("[SCENE] Set Source [") != std::string::npos;
        bool up = !gen && !scene && line.find(" is now level ") != std::string::npos;
        if (!gen && !scene && !up) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!parseLine(line, l)) continue;
        const std::string& m = l.msg;
        if (gen) {
            char area[160] = {0};
            int level = 0;
            if (sscanf(m.c_str(), "Generating level %d area \"%159[^\"]\"", &level, area) == 2) {
                areaId = area, areaLevel = level, areaName.clear(), naming = true, moved = true;
            }
        } else if (scene) {
            std::string n = m.substr(20, m.size() > 21 ? m.size() - 21 : 0);
            if (!n.empty() && n[0] != '(') areaName = n, naming = false;
        } else if (startsWith(m, ": ")) {  // ": Name (Class) is now level 31"
            size_t p = m.find(" is now level "), c = m.rfind(" (", p);
            if (p == std::string::npos || c == std::string::npos || c <= 2) continue;
            if (!sameName(m.substr(2, c - 2), name)) continue;
            int to = atoi(m.c_str() + p + 14);
            if (to < 2 || to > 100) continue;
            r.push_back({l.date, l.time, areaId, areaName, areaLevel, to - 1, to, !moved});
            moved = false;
        }
    }
    return r;
}

void mergeLevelUps(std::vector<LevelUp>& into, const std::vector<LevelUp>& more) {
    for (const LevelUp& m : more) {
        bool have = false;
        for (LevelUp& e : into)
            if (e.date == m.date && e.time == m.time && e.to == m.to) have = true, e.oneZone = m.oneZone;
        if (!have) into.push_back(m);
    }
    std::stable_sort(into.begin(), into.end(), [](const LevelUp& a, const LevelUp& b) {
        return logSeconds(a.date, a.time) < logSeconds(b.date, b.time);
    });
}

std::string saveLevelUps(const std::vector<LevelUp>& ups) {
    std::string s =
        "# Poe2 | MapStat level ups: date\ttime\tarea id\tarea name\tarea level\tfrom\tto\tone zone (1: no area "
        "entered during the level)\n";
    for (const LevelUp& u : ups)
        s += u.date + '\t' + u.time + '\t' + u.areaId + '\t' + u.areaName + '\t' + std::to_string(u.areaLevel) + '\t' +
             std::to_string(u.from) + '\t' + std::to_string(u.to) + '\t' + (u.oneZone ? "1" : "0") + '\n';
    return s;
}

std::vector<LevelUp> loadLevelUps(const std::string& text) {
    std::vector<LevelUp> r;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f;
        size_t a = 0;
        for (size_t b; (b = line.find('\t', a)) != std::string::npos; a = b + 1) f.push_back(line.substr(a, b - a));
        f.push_back(line.substr(a));
        if (f.size() < 7) continue;
        r.push_back({f[0], f[1], f[2], f[3], atoi(f[4].c_str()), atoi(f[5].c_str()), atoi(f[6].c_str()),
                     f.size() > 7 && f[7] == "1"});
    }
    return r;
}

int64_t levelSeconds(const std::vector<LevelUp>& ups, size_t i) {
    if (i == 0 || i >= ups.size() || ups[i - 1].to != ups[i].from) return -1;
    int64_t d = logSeconds(ups[i].date, ups[i].time) - logSeconds(ups[i - 1].date, ups[i - 1].time);
    return d > 0 ? d : -1;
}
