// Campaign zones: one timer per zone instance, acts, Reset Campaign Run.
#include <cctype>

#include "tracker.h"
#include "string_util.h"

// Campaign area IDs: [C_]G<act>_<n> or G<act>_town, interludes P<n>_<m>.
bool Tracker::isCampaign(const std::string& a) {
    size_t i = startsWith(a, "C_") ? 2 : 0;
    if (i >= a.size() || (a[i] != 'G' && a[i] != 'P')) return false;
    size_t j = i + 1;
    while (j < a.size() && isdigit((unsigned char)a[j])) ++j;
    return j > i + 1 && j < a.size() && a[j] == '_';
}

std::string Tracker::actOf(const std::string& a) {
    if (!isCampaign(a)) return "";
    bool cruel = startsWith(a, "C_");
    size_t i = cruel ? 2 : 0;
    std::string num;
    for (size_t j = i + 1; j < a.size() && isdigit((unsigned char)a[j]); ++j) num += a[j];
    std::string r = (a[i] == 'P' ? "Interlude " : "Act ") + num;
    return cruel ? r + " (Cruel)" : r;
}

void Tracker::closeZone(uint32_t tick) {
    if (curZone >= 0 && zones[curZone].inSegment) {
        Zone& z = zones[curZone];
        z.activeMs += (uint32_t)(tick - z.segStartTick);
        z.inSegment = false;
        z.leftTick = tick;
    }
}

void Tracker::expireZones(uint32_t now) {
    for (size_t i = 0; i < zones.size(); ++i) {
        Zone& z = zones[i];
        if (z.closed || z.inSegment || (int)i == curZone || z.entries == 0) continue;
        if ((uint32_t)(now - z.leftTick) > kResumeWindowMs) z.closed = true;
    }
}

void Tracker::resetCampaign(uint32_t tick, const std::string& time) {
    campaignStartTime = time;
    cmpClock = Clock::Running;
    zones.clear();
    curZone = -1;
    if (!isCampaign(location)) return;
    Zone z;
    z.id = 1;
    z.areaId = location;
    z.name = locationLoaded && !locationName.empty() ? locationName : nameOf(location);
    z.level = locationLevel;
    z.seed = locationSeed;
    z.town = location.find("town") != std::string::npos;
    if (locationLoaded) {
        z.entries = 1;
        z.firstEnter = z.lastEnter = time;
        z.segStartTick = tick;
        z.inSegment = true;
    } else {
        pending = Pending::Zone;  // still loading: the loading screen line enters it
    }
    zones.push_back(z);
    curZone = 0;
}

void Tracker::toggleCampaignPause(uint32_t tick) {
    if (cmpClock == Clock::Stopped) return;
    if (cmpClock == Clock::Running) {
        closeZone(tick);
        cmpClock = Clock::Paused;
        return;
    }
    cmpClock = Clock::Running;
    if (curZone >= 0 && locationLoaded && !zones[curZone].inSegment) {
        zones[curZone].segStartTick = tick;
        zones[curZone].inSegment = true;
    }
}

void Tracker::stopCampaign(uint32_t tick) {
    if (cmpClock == Clock::Stopped) return;
    closeZone(tick);
    curZone = -1;
    if (pending == Pending::Zone) pending = Pending::None;
    cmpClock = Clock::Stopped;
}

void Tracker::replaceCampaign(const Tracker& src) {
    bool live = curZone >= 0 && curZone < (int)zones.size();
    Zone cz;
    if (live) cz = zones[curZone];
    zones = src.zones;
    campaignStartTime = src.campaignStartTime;
    cmpClock = src.cmpClock;
    curZone = -1;
    if (!live || cmpClock == Clock::Stopped) return;
    if (cmpClock == Clock::Paused) cz.inSegment = false;
    for (size_t i = 0; i < zones.size(); ++i)  // current zone is part of the restored run: continue that row
        if (!zones[i].closed && zones[i].areaId == cz.areaId && zones[i].seed == cz.seed) {
            zones[i].inSegment = cz.inSegment;
            zones[i].segStartTick = cz.segStartTick;
            curZone = (int)i;
            return;
        }
    cz.id = (int)zones.size() + 1;  // otherwise the current zone is appended
    zones.push_back(cz);
    curZone = (int)zones.size() - 1;
}
