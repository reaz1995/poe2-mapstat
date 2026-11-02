// Area id -> display name, learned from [SCENE] lines and kept in areas.txt.
#include <fstream>

#include "tracker.h"
#include "string_util.h"

std::string Tracker::nameOf(const std::string& id) const {
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
        areaNames[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return true;
}

bool Tracker::saveAreaNames(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << "# areaId = display name   (filled automatically from [LOADING SCREEN] lines)\n";
    for (auto& [id, n] : areaNames) f << id << " = " << n << "\n";
    return true;
}
