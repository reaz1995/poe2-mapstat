// Tracker: feeds log lines, classifies areas and switches between maps, sub-areas, hideouts and campaign zones.
#include <cstdlib>

#include "tracker.h"
#include "string_util.h"

static const char* kDialogueHash = "3ef23348";  // hash column of NPC dialogue lines

const char* stateName(RunState s) {
    switch (s) {
        case RunState::Loading: return "Loading";
        case RunState::Active: return "In map";
        case RunState::Paused: return "Paused";
        case RunState::Finished: return "Finished";
    }
    return "?";
}

std::string fmtDuration(uint64_t ms) {
    uint64_t s = ms / 1000;
    char b[32];
    if (s >= 3600) snprintf(b, sizeof b, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    else snprintf(b, sizeof b, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    return b;
}

bool Tracker::isSafeArea(const std::string& a, uint32_t seed) {
    // Hideouts and towns are generated with seed 1.
    return seed <= 1 || startsWith(a, "Hideout");
}

bool Tracker::isHideout(const std::string& a) { return startsWith(a, "Hideout"); }

bool Tracker::isSubArea(const std::string& a) {
    // Areas that can be opened from inside any running map. Entered from a run, their time counts for that run;
    // entered from the hideout, they are a run of their own.
    //   Abyss_Depths*        Abyssal Depths
    //   Abyss_Boss*          Lightless Void (Boss1) / Dark Domain (Boss2): Abyssal Depths with a boss room (79+ maps)
    //   ExpeditionSubArea_*  Expedition boss arenas
    return startsWith(a, "Abyss_Depths") || startsWith(a, "Abyss_Boss") || startsWith(a, "ExpeditionSubArea_");
}

void Tracker::reset() {
    summaryStart = 0;
    summarySkipId = -1;
    summaryArchive.clear();
    cmpClock = Clock::Running;
    offline = false;
    generalStart = 0;
    campaignStartTime.clear();
    zones.clear();
    history.clear();
    nextId = 1;
    resetSession();
}

void Tracker::resetSession() {
    inHideoutNow = false;
    curZone = -1;
    hasCurrent = false;
    cur = Run{};
    npcLines.clear();
    location.clear();
    locationLoaded = false;
    locationName.clear();
    sessionStart.clear();
    lastTick = 0;
    lastTime.clear();
    pending = Pending::None;
    inSafe = true;
    sceneNamePending = false;
    sawLoadingScreen = false;
    curSub = -1;
    homeId.clear();
    homeName.clear();
}

void Tracker::goOffline(uint32_t tick) {
    if (offline || location.empty()) return;
    offline = true;
    offTick = tick;
    offArea = location;
    offSeed = locationSeed;
    offStayTick = inHideoutNow ? hideoutStayTick : 0;
}

void Tracker::suspend(uint32_t now) {
    goOffline(now);
    if (hasCurrent && cur.inSegment) lastTick = now;  // count time up to now for the open map
    if (hasCurrent) finish(EndReason::Manual, nullptr);
    closeZone(now);
    curZone = -1;
}

void Tracker::onGenerate(const std::string& area, int level, uint32_t seed, const LogLine& l) {
    returnGap = 0;
    if (offline) {  // first area after a logout or a game restart
        offline = false;
        uint32_t away = l.tick - offTick;
        if (area == offArea && seed == offSeed && away < kOfflineKeepMs) returnGap = away;
        else offStayTick = 0;
    }
    bool wasInRunArea = hasCurrent && cur.inSegment;
    closeSegment(l.tick);  // loading screens never count as map time
    closeHideout(l.tick);
    closeZone(l.tick);
    curZone = -1;
    curSub = -1;  // the segment that just closed was the last one in that sub-area
    sceneNamePending = true;
    inHideoutNow = false;
    if (isHideout(area) && area != homeId) { homeId = area; homeName.clear(); }
    location = area;
    locationLevel = level;
    locationSeed = seed;
    locationLoaded = false;

    // Campaign zone: own timer per area+seed, pauses any map run.
    if (isCampaign(area)) {
        inSafe = true;
        if (hasCurrent) cur.state = RunState::Paused;
        if (cmpClock == Clock::Stopped) {  // campaign run finished: nothing is tracked until a new one starts
            pending = Pending::None;
            return;
        }
        expireZones(l.tick);
        for (size_t i = 0; i < zones.size(); ++i)
            if (!zones[i].closed && zones[i].areaId == area && zones[i].seed == seed) curZone = (int)i;
        if (curZone < 0) {
            Zone z;
            z.id = (int)zones.size() + 1;
            z.areaId = area;
            z.name = nameOf(area);
            z.level = level;
            z.seed = seed;
            z.town = area.find("town") != std::string::npos;
            zones.push_back(z);
            curZone = (int)zones.size() - 1;
        }
        pending = Pending::Zone;
        return;
    }

    // Low-level hubs, not maps: Abyss_Hub = The Well of Souls (Act 2, lvl 22, desecration crafting),
    // IncursionHub at low level (0.4 Vaal hub). High-level visits of those ids still count as maps.
    bool menuScene = (area == "Abyss_Hub" || area == "IncursionHub") && level < 30;
    if (isSafeArea(area, seed) || menuScene) {
        inSafe = true;
        pending = Pending::Safe;
        if (hasCurrent) cur.state = RunState::Paused;  // leaving = pause, never finish
        return;
    }

    // Sub-area entered straight from the running map -> part of that run.
    if (isSubArea(area) && wasInRunArea && !inSafe) {
        pending = Pending::SubArea;
        for (size_t i = 0; i < cur.subs.size(); ++i)  // same sub-area instance again -> same entry
            if (cur.subs[i].areaId == area && cur.subs[i].seed == seed) curSub = (int)i;
        if (curSub < 0) {
            SubVisit v;
            v.areaId = area;
            v.seed = seed;
            v.level = level;
            cur.subs.push_back(v);
            curSub = (int)cur.subs.size() - 1;
        }
        cur.state = RunState::Loading;
        return;
    }
    inSafe = false;

    // Same map instance again (portal back in, or back from a sub-area).
    if (hasCurrent && cur.areaId == area && cur.seed == seed) {
        pending = Pending::Run;
        cur.state = RunState::Loading;
        return;
    }
    // Same instance as the last finished map (game crash / relog / app restart, then back through the portal):
    // continue that row instead of starting a new one. Not across a General/Summary reset.
    // Area + seed identify one instance, so any earlier row of it is continued (never two rows per instance).
    int match = -1;
    for (size_t i = history.size(); i-- > std::max(generalStart, summaryStart);)
        if (history[i].areaId == area && history[i].seed == seed) { match = (int)i; break; }
    if (match >= 0) {
        if (hasCurrent) finish(EndReason::NewSeed, &l);  // the open map ends and is appended after the match
        {
            cur = history[match];
            history.erase(history.begin() + match);
            hasCurrent = true;
            cur.state = RunState::Loading;
            cur.reason = EndReason::None;
            cur.endTime.clear();
            cur.inSegment = false;
            cur.inHideout = false;
            pending = Pending::Run;
            npcLines.clear();
            if (onHistoryChanged) onHistoryChanged();
            return;
        }
    }

    // A different seed: whatever was open is now finished.
    if (hasCurrent) finish(EndReason::NewSeed, &l);
    startRun(area, level, seed, l);
    pending = Pending::Run;
}

// True when `tick` is not newer than what is already stored (the app restarted inside the same area and read
// the last area change again). Entry, loading and time are then not counted twice.
static bool alreadyCounted(uint32_t counted, uint32_t tick) {
    return counted != 0 && (uint32_t)(counted - tick) < 12u * 3600u * 1000u;
}

void Tracker::onLoaded(const std::string& name, double sec, const LogLine& l) {
    locationName = name;
    locationLoaded = true;
    uint64_t gap = returnGap;
    returnGap = 0;
    if (pending == Pending::Zone && curZone >= 0) {
        Zone& z = zones[curZone];
        z.name = name;
        bool again = alreadyCounted(z.leftTick, l.tick);
        if (z.entries == 0) z.firstEnter = l.time;
        if (campaignStartTime.empty()) campaignStartTime = l.time;
        z.lastEnter = l.time;
        bool running = cmpClock == Clock::Running;
        if (!again) {
            z.entries++;
            if (running) z.loadSec += sec, z.activeMs += gap;
        }
        if (running) {
            z.segStartTick = again ? z.leftTick : l.tick;
            z.inSegment = true;
        }
        pending = Pending::None;
        return;
    }
    if (pending == Pending::Safe && isHideout(location)) {  // stay in a hideout starts after loading
        inHideoutNow = true;
        hideoutStayTick = gap && offStayTick ? offStayTick : l.tick;  // short logout: the stay goes on
        if (gap && hasCurrent && cur.hideoutId == location) cur.hideoutMs += gap;
    }
    offStayTick = 0;
    if (pending == Pending::Safe && location == homeId) {
        homeName = name;
        if (hasCurrent && cur.hideoutId == homeId) {   // back at the starting point
            if (cur.hideoutName.empty()) cur.hideoutName = name;
            cur.inHideout = true;
            cur.hideoutStartTick = l.tick;
            cur.hideoutReturns++;
        }
    }
    bool again = hasCurrent && alreadyCounted(cur.countedTick, l.tick);
    if (pending == Pending::SubArea && hasCurrent && curSub >= 0 && curSub < (int)cur.subs.size() && !again)
        cur.subs[curSub].entries++;
    if (pending == Pending::Run || pending == Pending::SubArea) {
        if (hasCurrent) {
            if (pending == Pending::Run) {
                cur.name = name;
                if (cur.entries == 0) cur.enterTime = l.time;
                if (!again) cur.entries++;
            }
            if (!again) cur.loadSec += sec;
            if (!again && pending == Pending::Run) cur.activeMs += gap;
            cur.segStartTick = again ? cur.countedTick : l.tick;
            cur.inSegment = true;
            cur.state = RunState::Active;
        }
    }
    pending = Pending::None;
}

void Tracker::onDialogue(const std::string& speaker, const std::string& text, const LogLine& l) {
    if (!hasCurrent || !cur.inSegment) return;
    npcLines.push_back(l.time + "  " + speaker + ": " + text);
    if (npcLines.size() > 200) npcLines.erase(npcLines.begin());
}

void Tracker::onDisconnect(const LogLine& l) {
    goOffline(l.tick);
    inHideoutNow = false;
    closeSegment(l.tick);
    closeZone(l.tick);
    curZone = -1;
    closeHideout(l.tick);
    inSafe = true;
    pending = Pending::None;
    if (hasCurrent) cur.state = RunState::Paused;
}

void Tracker::feed(const std::string& raw) {
    LogLine l;
    if (!parseLine(raw, l)) return;
    if (sessionStart.empty()) sessionStart = l.date + " " + l.time;
    lastTick = l.tick;
    lastTime = l.time;
    const std::string& m = l.msg;

    if (startsWith(m, "Generating level ")) {
        int level = 0;
        char area[160] = {0};
        unsigned long seed = 0;
        if (sscanf(m.c_str(), "Generating level %d area \"%159[^\"]\" with seed %lu", &level, area, &seed) == 3)
            onGenerate(area, level, (uint32_t)seed, l);
        return;
    }
    if (startsWith(m, "[LOADING SCREEN] (")) {
        sawLoadingScreen = true;
        size_t close = m.find(") Duration = ");
        if (close != std::string::npos)
            onLoaded(m.substr(18, close - 18), atof(m.c_str() + close + 13), l);
        return;
    }
    if (startsWith(m, "[SCENE] Set Source [")) {
        std::string name = m.substr(20, m.size() > 21 ? m.size() - 21 : 0);
        if (sceneNamePending && !location.empty() && !name.empty() && name[0] != '(') {
            sceneNamePending = false;
            auto it = areaNames.find(location);
            if (it == areaNames.end() || it->second != name) { areaNames[location] = name; areaNamesChanged = true; }
            if (curZone >= 0 && zones[curZone].areaId == location) zones[curZone].name = name;
            if (hasCurrent && cur.areaId == location && cur.entries == 0) cur.name = name;
        }
        // Older game versions write no [LOADING SCREEN] lines: then the scene switch marks entering the area.
        if (!sawLoadingScreen && pending != Pending::None && !name.empty() && name[0] != '(') onLoaded(name, 0, l);
        return;
    }
    if (startsWith(m, ": ") && onLevelUp) {  // ": Name (Class) is now level 31"
        size_t p = m.find(" is now level ");
        size_t c = m.rfind(" (", p);
        if (p != std::string::npos && c != std::string::npos && c > 2) {
            int lvl = atoi(m.c_str() + p + 14);
            if (lvl >= 1 && lvl <= 100) onLevelUp(m.substr(2, c - 2), lvl);
        }
        return;
    }
    // Back to the login server: logged out to character selection (or disconnected).
    if (startsWith(m, "Abnormal disconnect") ||
        (startsWith(m, "Async connecting to ") && m.find(".login.") != std::string::npos)) {
        onDisconnect(l);
        return;
    }
    if (l.hash == kDialogueHash && !m.empty()) {
        // Chat lines start with #,$,@,%,& or ':' (system). NPC lines are "Speaker: text".
        char c = m[0];
        if (c == '#' || c == '$' || c == '@' || c == '%' || c == '&' || c == ':' || c == '[') return;
        size_t sep = m.find(": ");
        if (sep == std::string::npos || sep > 80) return;
        onDialogue(m.substr(0, sep), trim(m.substr(sep + 2)), l);
    }
}
