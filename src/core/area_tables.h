// Zone and act names by id, from the tables in tables/ (zones.txt, acts.txt): "key = name" per line, in campaign
// order, "unknown" while the name is not known yet.
//
//   zones.txt   G1_2 = Clearfell             (area id = zone name)
//   acts.txt    G1 = Act 1                   (start of the area ids = act name)
//               G5 = unknown                 -> shown as "G5" until it is filled in
//
// The app bakes the repo tables in and keeps an editable copy in %APPDATA%\Poe2MapStat\tables. Names typed there
// win; "unknown" there is filled from a newer app's tables; ids the game shows that no table has are added as
// "unknown". Everything that names a zone or an act (campaign, layouts, overlays, steps) reads them.
#pragma once

#include <string>
#include <utility>
#include <vector>

class AreaTable {
public:
    using Entry = std::pair<std::string, std::string>;  // key, name ("unknown" = not known)

    void parse(const std::string& text);  // replaces the entries
    std::string text(const char* header) const;
    // name for a key, "" when not listed or unknown
    std::string name(const std::string& key) const;
    bool has(const std::string& key) const;
    // adds the key (at the end, or after the last key of the same act) when it is not listed; true if added
    bool add(const std::string& key, const std::string& name = "unknown");
    // known names of `newer` fill the "unknown" ones here, its keys missing here are added
    bool mergeFrom(const AreaTable& newer);
    // the known names of `other` replace the ones here (loaded from a file), its keys missing here are added;
    // how many names changed or were added
    int takeNames(const AreaTable& other);
    bool isActTable() const;  // every key is an act key (G1, C_G2, P1): acts.txt, else zones.txt
    const std::vector<Entry>& entries() const { return m_e; }

private:
    std::vector<Entry> m_e;
};

AreaTable& zoneTable();
AreaTable& actTable();

// "G1_2" -> "G1", "C_G2_3" -> "C_G2", "P1_4" -> "P1"; "" for areas outside the campaign
std::string actKeyOf(const std::string& areaId);
bool knownName(const std::string& name);  // not empty and not "unknown"
