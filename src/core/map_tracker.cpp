// Map runs: segments of play time, finishing, hideout returns, General / Summary resets.
#include <set>

#include "tracker.h"
#include "string_util.h"

void Tracker::closeSegment(uint32_t tick) {
    if (hasCurrent && cur.inSegment) {
        uint32_t d = (uint32_t)(tick - cur.segStartTick);
        cur.activeMs += d;
        cur.countedTick = tick;
        if (curSub >= 0 && curSub < (int)cur.subs.size()) cur.subs[curSub].ms += d;
        cur.inSegment = false;
    }
}

void Tracker::closeHideout(uint32_t tick) {
    if (hasCurrent && cur.inHideout) {
        cur.hideoutMs += (uint32_t)(tick - cur.hideoutStartTick);
        cur.inHideout = false;
    }
}

void Tracker::finish(EndReason r, const LogLine* l) {
    if (!hasCurrent) return;
    closeSegment(l ? l->tick : lastTick);
    closeHideout(l ? l->tick : lastTick);
    cur.state = RunState::Finished;
    cur.reason = r;
    cur.endTime = l ? l->time : lastTime;
    history.push_back(cur);
    hasCurrent = false;
    curSub = -1;
    npcLines.clear();
    if (onHistoryChanged) onHistoryChanged();
}

void Tracker::startRun(const std::string& area, int level, uint32_t seed, const LogLine& l) {
    cur = Run{};
    cur.id = nextId++;
    cur.areaId = area;
    cur.name = nameOf(area);
    cur.level = level;
    cur.seed = seed;
    cur.genTime = l.time;
    cur.date = l.date;
    cur.hideoutId = homeId;
    cur.hideoutName = homeName;
    cur.state = RunState::Loading;
    hasCurrent = true;
    npcLines.clear();
}

void Tracker::replaceMaps(const Tracker& src) {
    for (size_t i = summaryStart; i < history.size(); ++i)
        if (history[i].id != summarySkipId) summaryArchive.push_back(history[i]);
    history = src.history;
    generalStart = std::min(src.generalStart, history.size());
    summaryStart = 0;
    summarySkipId = -1;
    nextId = std::max(1, src.nextId);
    if (hasCurrent) {  // the open map may already be in the imported rows: keep one row, the live one
        for (size_t i = 0; i < history.size(); ++i) {
            const Run& r = history[i];
            if (r.areaId == cur.areaId && r.seed == cur.seed) {
                cur.activeMs = std::max(cur.activeMs, r.activeMs);
                cur.entries = std::max(cur.entries, r.entries);
                history.erase(history.begin() + (long)i);
                break;
            }
        }
        cur.id = nextId++;
    }
    if (onHistoryChanged) onHistoryChanged();
}

void Tracker::resetSummary() {
    summaryStart = history.size();
    summarySkipId = hasCurrent ? cur.id : -1;
    summaryArchive.clear();
}

std::vector<std::pair<const Run*, uint64_t>> Tracker::summaryRuns(uint32_t now) const {
    std::vector<std::pair<const Run*, uint64_t>> out;
    std::set<std::string> seen;  // one map instance once, the newest copy wins
    auto first = [&](const Run& r) { return seen.insert(r.date + "|" + r.genTime + "|" + r.areaId + "|" + std::to_string(r.seed)).second; };
    if (hasCurrent && cur.entries > 0 && cur.id != summarySkipId && first(cur)) out.push_back({&cur, cur.activeAt(now)});
    for (size_t i = history.size(); i-- > summaryStart;)
        if (history[i].id != summarySkipId && first(history[i])) out.push_back({&history[i], history[i].activeMs});
    for (size_t i = summaryArchive.size(); i-- > 0;)
        if (first(summaryArchive[i])) out.push_back({&summaryArchive[i], summaryArchive[i].activeMs});
    return out;
}

void Tracker::resetGeneral() {
    if (hasCurrent) finish(EndReason::Manual, nullptr);  // still counted in Summary
    npcLines.clear();
    pending = pending == Pending::Run || pending == Pending::SubArea ? Pending::None : pending;
    generalStart = history.size();
    if (onHistoryChanged) onHistoryChanged();
}
