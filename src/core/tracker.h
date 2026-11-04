// Tracker for map runs and campaign zones. Portable C++, no Windows dependencies.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "log_line.h"

enum class RunState { Loading, Active, Paused, Finished };
enum class EndReason { None, NewSeed, Manual };

// Visit of a sub-area (Abyssal Depths, Lightless Void, Dark Domain, Expedition boss) from inside a map.
// Its time is part of the map time and is also listed on its own in Summary.
struct SubVisit {
    std::string areaId;
    uint32_t seed = 0;
    int level = 0;
    uint64_t ms = 0;
    int entries = 0;
};

struct Run {
    int id = 0;
    std::string areaId, name;
    int level = 0;
    uint32_t seed = 0;
    std::string date;  // "2026/11/01" (log date of generation)
    std::string genTime, enterTime, endTime;  // wall clock "HH:MM:SS"
    uint32_t segStartTick = 0;
    bool inSegment = false;
    uint64_t activeMs = 0;  // closed segments only
    uint32_t countedTick = 0;  // log tick up to which activeMs counts (re-reading older lines adds nothing)
    double loadSec = 0;
    int entries = 0;
    std::vector<SubVisit> subs;  // sub-areas visited during this run (one per instance)
    std::string hideoutId, hideoutName;  // where the map was opened from
    bool inHideout = false;  // currently back at that hideout
    uint32_t hideoutStartTick = 0;
    uint64_t hideoutMs = 0;  // time spent back in that hideout while paused
    int hideoutReturns = 0;
    uint64_t hideoutAt(uint32_t nowTick) const {
        return hideoutMs + (inHideout ? (uint32_t)(nowTick - hideoutStartTick) : 0);
    }
    RunState state = RunState::Loading;
    EndReason reason = EndReason::None;

    uint64_t activeAt(uint32_t nowTick) const {
        return activeMs + (inSegment ? (uint32_t)(nowTick - segStartTick) : 0);
    }
};

// One campaign zone instance (area + seed). Same seed again = resume.
struct Zone {
    int id = 0;
    std::string areaId, name, firstEnter, lastEnter;
    int level = 0;
    uint32_t seed = 0;
    bool town = false;
    bool inSegment = false;
    uint32_t segStartTick = 0;
    uint64_t activeMs = 0;
    int entries = 0;
    double loadSec = 0;
    uint32_t leftTick = 0;  // tick of the last exit from the zone
    std::string lastLeft;   // log time of the last exit (21:13:15)
    bool closed = false;  // instance expired -> never resumed
    uint64_t activeAt(uint32_t nowTick) const {
        return activeMs + (inSegment ? (uint32_t)(nowTick - segStartTick) : 0);
    }
};

const char* stateName(RunState s);

class Tracker {
public:
    std::vector<Run> history;  // finished runs, oldest first
    bool hasCurrent = false;
    Run cur;
    std::string location;  // areaId the player is in now
    uint32_t locationSeed = 0;  // its instance, from the log (also when no timer runs)
    std::string locationName;  // display name from the loading screen
    std::string sessionStart;  // first timestamp of the log
    uint32_t lastTick = 0;
    std::string homeId, homeName;  // last hideout seen (the map's starting point)
    bool inHideoutNow = false;  // standing in a hideout right now (any hideout)
    int locationLevel = 0;  // area level of where the player is now
    uint32_t hideoutStayTick = 0;  // when the current hideout stay started
    std::string lastTime;
    std::function<void()> onHistoryChanged;
    std::function<void(const std::string& name, int level, const LogLine& l)> onLevelUp;  // ": Name (Class) is now level N"
    bool sawLoadingScreen = false;  // this log session has [LOADING SCREEN] lines (older game versions don't);
        // set from outside when reading starts in the middle of a log

    // Campaign zones
    std::map<std::string, std::string> areaNames;  // areaId -> display name (learned from [SCENE] lines)
    // Campaign areas: lowest and highest area level seen. Some zones (Act 4, Interludes) follow the character
    // level within a range, so they are generated at different levels; this keeps the range seen so far.
    std::map<std::string, std::pair<int, int>> areaLevels;
    void noteAreaLevel(const std::string& areaId, int level);
    // "46-51" for a zone that follows the character level (built-in list, else the range seen), "" for fixed ones
    std::string levelRange(const std::string& areaId) const;
    bool areaNamesChanged = false;
    bool tablesChanged = false;  // a new zone / act id was added to the area tables (area_tables.h)
    bool loadAreaNames(const std::string& path);
    bool saveAreaNames(const std::string& path) const;
    std::string nameOf(const std::string& areaId) const;
    // Name of the area the player is in: its loading-screen name once that loading screen is over, until then
    // (and right after a teleport, when locationName is still the previous area) the name known for its id.
    std::string locationDisplay() const {
        return locationLoaded && !locationName.empty() ? locationName : nameOf(location);
    }
    bool locationIsLoaded() const { return locationLoaded; }

    std::vector<Zone> zones;  // in order of first visit
    int curZone = -1;  // index into zones, -1 = not in campaign
    // Same area+seed re-entered within this time after leaving -> time is added to the same row.
    // Later -> new row (towns always have seed 1, so this splits town time into separate runs).
    static constexpr uint32_t kResumeWindowMs = 60 * 60 * 1000;
    void expireZones(uint32_t nowTick);

    // "Save and Clear" / start after a stop: a clean run from zero. A campaign zone the player is in
    // right now becomes its first row.
    std::string campaignStartTime;  // wall clock of the reset (or first zone)
    void resetCampaign(uint32_t tick, const std::string& time);

    // Campaign clock, controlled by the user (hotkeys and buttons on the Campaign tab).
    // Paused: zones are still listed but no time is counted. Stopped: the run is finished and frozen,
    // nothing is tracked until a new run is started with resetCampaign().
    enum class Clock { Running, Paused, Stopped };
    Clock cmpClock = Clock::Running;
    void toggleCampaignPause(uint32_t tick);
    void stopCampaign(uint32_t tick);

    // "Reset Session" on the Maps tab: hides older finished maps there (Summary keeps all).
    size_t generalStart = 0;  // first history index shown on the Maps tab
    void resetGeneral();

    // Summary is independent of the Maps list: it counts history[summaryStart..], the open map and
    // summaryArchive (runs of Maps lists that were replaced by a restore or an import). Only
    // "Reset Summary" clears it; the map open at that moment is left out (summarySkipId).
    size_t summaryStart = 0;
    int summarySkipId = -1;
    std::vector<Run> summaryArchive;
    void resetSummary();
    // Runs counted in Summary with their time, each map instance once.
    std::vector<std::pair<const Run*, uint64_t>> summaryRuns(uint32_t nowTick) const;
    static bool isCampaign(const std::string& area);  // G1_2, G1_town, C_G2_3, P1_1 ...
    static std::string actOf(const std::string& area);  // "Act 1", "Act 2 (Cruel)", ...

    // Restore map session / Load past events: the Maps list comes from `src`. Runs counted in Summary
    // so far move to summaryArchive, the new runs are added to Summary. Campaign and the live state stay.
    void replaceMaps(const Tracker& src);
    void reset();  // clears all data
    void resetSession();  // new log / app start: drop transient state, keep stored runs & zones
    void suspend(uint32_t nowTick);  // app closing / game restarted: close the open map and zone timers

    // Logged out (or the game was closed) and back into the same area within this time: the time away
    // counts for that map, zone or hideout stay. Longer breaks are not counted.
    static constexpr uint32_t kOfflineKeepMs = 15 * 60 * 1000;

    // Persistence. Maps and campaign zones are stored in separate files
    // (MapSession / CampaignSession), so each part can be reset and restored on its own. Summary state
    // has a file of its own (SummarySession).
    enum Parts { kMaps = 1, kCampaign = 2, kSummary = 4, kAll = 7 };
    std::string serialize(int parts = kAll) const;
    void deserialize(const std::string& data, int parts = kAll);  // replaces only those parts
    void replaceCampaign(const Tracker& src);  // campaign zones from `src`; the current zone keeps running
    void feed(const std::string& rawLine);

    // In a sub-area of the open map (Abyssal Depths, Lightless Void, ...): `location` is that sub-area.
    bool inSubArea() const { return hasCurrent && curSub >= 0 && isSubArea(location); }
    // Time spent in the current sub-area (all visits of that instance), live. Part of the map time as well.
    uint64_t subAreaAt(uint32_t nowTick) const {
        if (!inSubArea()) return 0;
        return cur.subs[curSub].ms + (cur.inSegment ? (uint32_t)(nowTick - cur.segStartTick) : 0);
    }

    static bool isSafeArea(const std::string& area, uint32_t seed);
    static bool isSubArea(const std::string& area);
    static bool isHideout(const std::string& area);

private:
    enum class Pending { None, Run, SubArea, Safe, Zone };
    void closeZone(uint32_t tick);
    bool sceneNamePending = false;  // take area name from first [SCENE] line after Generating
    int curSub = -1;  // index into cur.subs while standing in a sub-area
    Pending pending = Pending::None;
    bool inSafe = true;
    int nextId = 1;
    bool locationLoaded = false;  // the loading screen of `location` is over

    // Logout / game restart: where the player was, so returning to the same area can count the time away.
    bool offline = false;
    uint32_t offTick = 0, offSeed = 0, offStayTick = 0;
    std::string offArea;
    uint64_t returnGap = 0;  // time away to add when the area being loaded is the one left
    void goOffline(uint32_t tick);

    void closeSegment(uint32_t tick);
    void closeHideout(uint32_t tick);
    void finish(EndReason r, const LogLine* l);
    void startRun(const std::string& area, int level, uint32_t seed, const LogLine& l);
    void onGenerate(const std::string& area, int level, uint32_t seed, const LogLine& l);

public:
    int areasEntered = 0;  // "Generating level" lines seen (any area)

private:
    void onLoaded(const std::string& name, double sec, const LogLine& l);
    void onDisconnect(const LogLine& l);
};

std::string fmtDuration(uint64_t ms);  // H:MM:SS or M:SS
