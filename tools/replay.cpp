// Test harness: replay a log file through the tracker and print map runs and campaign zones.
// Build on Linux: g++ -std=c++20 -O2 -Isrc tools/replay.cpp src/core.cpp -o replay
#include <fstream>
#include <iostream>

#include "tracker.h"

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: replay <LatestClient.txt>\n"; return 1; }
    Tracker t;
    std::ifstream f(argv[1]);
    std::string line;
    while (std::getline(f, line)) t.feed(line);

    auto print = [&](const Run& r, uint64_t ms) {
        printf("#%-3d %-30s L%-2d seed %-10u gen %s enter %-8s load %5.1fs map %7s entries %d  %s\n", r.id,
               r.areaId.c_str(), r.level, r.seed, r.genTime.c_str(), r.enterTime.c_str(), r.loadSec,
               fmtDuration(ms).c_str(), r.entries, stateName(r.state));
        if (!r.hideoutId.empty())
            printf("       hideout: %s returns %d, time %s\n", r.hideoutName.c_str(), r.hideoutReturns,
                   fmtDuration(r.hideoutMs).c_str());
        for (auto& s : r.subs)
            printf("       sub-area: %s L%d %s entries %d\n", s.areaId.c_str(), s.level, fmtDuration(s.ms).c_str(), s.entries);
    };
    for (auto& r : t.history) print(r, r.activeMs);
    if (t.hasCurrent) { printf("CURRENT:\n"); print(t.cur, t.cur.activeAt(t.lastTick)); }
    for (auto& z : t.zones)
        printf("zone #%d %-14s %-8s %-22s seed %-10u first %s entries %d time %s%s\n", z.id,
               Tracker::actOf(z.areaId).c_str(), z.areaId.c_str(), z.name.c_str(), z.seed, z.firstEnter.c_str(),
               z.entries, fmtDuration(z.activeAt(t.lastTick)).c_str(), z.town ? " [town]" : "");
}
