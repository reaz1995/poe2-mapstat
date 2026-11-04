// Area id -> display name (from [SCENE] lines) and area level range seen, kept in areas.txt.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <set>

#include "area_tables.h"
#include "tracker.h"
#include "string_util.h"

// zones.txt (area_tables) first, then the name the log gave, then the id
std::string Tracker::nameOf(const std::string& id) const {
    std::string t = zoneTable().name(id);
    if (!t.empty()) return t;
    auto it = areaNames.find(id);
    return it == areaNames.end() ? id : it->second;
}

bool Tracker::loadAreaNames(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string id = trim(line.substr(0, eq)), name = trim(line.substr(eq + 1));
        size_t bar = name.rfind(" | ");  // "Kingsmarch | 45-55": level range after the name
        if (bar != std::string::npos) {
            int lo = 0, hi = 0;
            if (sscanf(name.c_str() + bar + 3, "%d-%d", &lo, &hi) == 2 && lo > 0 && hi >= lo) areaLevels[id] = {lo, hi};
            name = trim(name.substr(0, bar));
        }
        if (!name.empty()) areaNames[id] = name;
    }
    return true;
}

void Tracker::noteAreaLevel(const std::string& id, int level) {
    if (level <= 0) return;
    auto it = areaLevels.find(id);
    if (it == areaLevels.end()) {
        areaLevels[id] = {level, level};
        areaNamesChanged = true;
    } else if (level < it->second.first || level > it->second.second) {
        it->second.first = std::min(it->second.first, level);
        it->second.second = std::max(it->second.second, level);
        areaNamesChanged = true;
    }
}

// Zones whose area level follows the character level within a range (lowest, highest), by zone name.
// Zones not listed here use the range seen in the log (areaLevels).
static const struct {
    const char* name;
    int lo, hi;
} kZoneRanges[] = {
    // Act 2
    {"Mastodon Badlands", 21, 26},
    {"The Bone Pits", 22, 27},
    {"Keth", 21, 25},
    {"The Lost City", 22, 26},
    {"Buried Shrines", 23, 27},
    {"Valley of the Titans", 21, 25},
    {"The Titan Grotto", 22, 27},
    // Act 4
    {"Shrike Island", 46, 51},
    {"Abandoned Prison", 46, 51},
    {"Solitary Confinement", 47, 52},
    {"Whakapanu Island", 47, 51},
    {"Singing Caverns", 47, 52},
    {"Isle of Kin", 46, 51},
    {"Volcanic Warrens", 47, 52},
    {"Kedge Bay", 46, 51},
    {"Journey's End", 47, 52},
    {"Eye of Hinekora", 46, 51},
    {"Halls of the Dead", 47, 52},
    // Interlude 1
    {"Scorched Farmlands", 54, 62},
    {"Stones of Serle", 54, 62},
    {"The Blackwood", 55, 63},
    {"Holten", 55, 63},
    {"Wolvenhold", 56, 64},
    {"Holten Estate", 56, 64},
    // Interlude 2
    {"The Khari Crossing", 54, 62},
    {"Pools of Khatal", 55, 63},
    {"Sel Khari Sanctuary", 55, 63},
    {"The Galai Gates", 56, 64},
    {"Qimah", 56, 64},
    {"Qimah Reservoir", 56, 64},
    // Interlude 3
    {"Ashen Forest", 54, 62},
    {"Kriar Village", 54, 62},
    {"Glacial Tarn", 55, 63},
    {"Howling Caves", 55, 63},
    {"Kriar Peaks", 56, 64},
    {"Etched Ravine", 56, 64},
    {"The Cuachic Vault", 56, 64},
};

// Name compared without case, spaces and punctuation ("Isle Of Kin" = "Isle of Kin").
static std::string nameKey(const std::string& s) {
    std::string k;
    for (char c : s)
        if (isalnum((unsigned char)c)) k += (char)tolower((unsigned char)c);
    return k;
}

std::string Tracker::levelRange(const std::string& id) const {
    int lo = 0, hi = 0;
    std::string key = nameKey(nameOf(id));
    for (auto& z : kZoneRanges)
        if (nameKey(z.name) == key) lo = z.lo, hi = z.hi;
    if (!lo) {
        auto it = areaLevels.find(id);
        if (it != areaLevels.end()) lo = it->second.first, hi = it->second.second;
    }
    if (!lo || lo == hi) return "";
    return std::to_string(lo) + "-" + std::to_string(hi);
}

bool Tracker::saveAreaNames(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << "# areaId = display name | lowest-highest area level seen   (filled automatically from the log)\n";
    std::set<std::string> ids;
    for (auto& kv : areaNames) ids.insert(kv.first);
    for (auto& kv : areaLevels) ids.insert(kv.first);
    for (auto& id : ids) {
        f << id << " = " << nameOf(id);
        auto it = areaLevels.find(id);
        if (it != areaLevels.end()) f << " | " << it->second.first << "-" << it->second.second;
        f << "\n";
    }
    return true;
}
