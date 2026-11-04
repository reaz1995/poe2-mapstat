#include "area_tables.h"

#include <cctype>
#include <sstream>

#include "string_util.h"

bool knownName(const std::string& n) {
    std::string l;
    for (char c : n) l += (char)tolower((unsigned char)c);
    return !n.empty() && l != "unknown";
}

void AreaTable::parse(const std::string& text) {
    m_e.clear();
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (k.empty() || has(k)) continue;
        m_e.push_back({k, v.empty() ? "unknown" : v});
    }
}

std::string AreaTable::text(const char* header) const {
    std::string s = header;
    for (auto& [k, v] : m_e) s += k + " = " + v + "\n";
    return s;
}

std::string AreaTable::name(const std::string& key) const {
    for (auto& [k, v] : m_e)
        if (k == key) return knownName(v) ? v : "";
    return "";
}

bool AreaTable::has(const std::string& key) const {
    for (auto& e : m_e)
        if (e.first == key) return true;
    return false;
}

bool AreaTable::add(const std::string& key, const std::string& name) {
    if (key.empty() || has(key)) return false;
    // next to the zones of the same act, so the file stays in campaign order
    std::string act = actKeyOf(key);
    size_t at = m_e.size();
    if (!act.empty())
        for (size_t i = 0; i < m_e.size(); ++i)
            if (actKeyOf(m_e[i].first) == act) at = i + 1;
    m_e.insert(m_e.begin() + (long)at, {key, knownName(name) ? name : "unknown"});
    return true;
}

bool AreaTable::mergeFrom(const AreaTable& newer) {
    bool changed = false;
    for (auto& [k, v] : newer.m_e) {
        if (!has(k)) {
            changed |= add(k, v);
            continue;
        }
        for (auto& e : m_e)
            if (e.first == k && !knownName(e.second) && knownName(v)) e.second = v, changed = true;
    }
    return changed;
}

AreaTable& zoneTable() {
    static AreaTable t;
    return t;
}

AreaTable& actTable() {
    static AreaTable t;
    return t;
}

std::string actKeyOf(const std::string& a) {
    size_t i = a.compare(0, 2, "C_") == 0 ? 2 : 0;
    if (i >= a.size() || (a[i] != 'G' && a[i] != 'P')) return "";
    size_t j = i + 1;
    while (j < a.size() && isdigit((unsigned char)a[j])) ++j;
    if (j == i + 1 || (j < a.size() && a[j] != '_')) return "";
    return a.substr(0, j);
}
