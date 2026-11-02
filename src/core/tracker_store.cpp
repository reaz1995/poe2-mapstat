// Saving / loading: one record per line, tab-separated (MapSession / CampaignSession / SummarySession files).
#include <algorithm>
#include <cstdlib>
#include <sstream>

#include "tracker.h"
#include "string_util.h"

template <class T>
static std::string S(T v) { return std::to_string(v); }

static std::string joinTab(std::initializer_list<std::string> f) {
    std::string r;
    for (auto& x : f) {
        if (!r.empty()) r += '\t';
        for (char c : x) r += (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
    }
    return r + "\n";
}

static std::vector<std::string> splitTab(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == '\t') { out.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    out.push_back(cur);
    return out;
}

// "RUN" (General list) and "ARCH" (kept for Summary only) share one record layout.
static std::string runLine(const char* tag, const Run& r) {
    std::string subs;
    for (auto& x : r.subs)
        subs += (subs.empty() ? "" : ";") + x.areaId + ":" + S(x.seed) + ":" + S(x.level) + ":" + S(x.ms) + ":" + S(x.entries);
    return joinTab({tag, S(r.id), r.date, r.areaId, r.name, S(r.level), S(r.seed), r.genTime, r.enterTime, r.endTime,
                    S(r.activeMs), S(r.loadSec), S(r.entries), S((int)r.reason), r.hideoutId, r.hideoutName,
                    S(r.hideoutReturns), S(r.hideoutMs), subs, S(r.countedTick)});
}

static uint64_t u64(const std::string& s) { return (uint64_t)strtoull(s.c_str(), nullptr, 10); }

static Run parseRun(const std::vector<std::string>& f) {
    Run r;
    r.id = atoi(f[1].c_str());
    r.date = f[2];
    r.areaId = f[3];
    r.name = f[4];
    r.level = atoi(f[5].c_str());
    r.seed = (uint32_t)u64(f[6]);
    r.genTime = f[7];
    r.enterTime = f[8];
    r.endTime = f[9];
    r.activeMs = u64(f[10]);
    r.loadSec = atof(f[11].c_str());
    r.entries = atoi(f[12].c_str());
    r.reason = (EndReason)atoi(f[13].c_str());
    r.hideoutId = f[14];
    r.hideoutName = f[15];
    r.hideoutReturns = atoi(f[16].c_str());
    r.hideoutMs = u64(f[17]);
    std::stringstream sub(f[18]);
    for (std::string x; std::getline(sub, x, ';');) {  // "area:seed:level:ms:entries" (older files: "area")
        if (x.empty()) continue;
        SubVisit v;
        std::stringstream p(x);
        std::string part;
        for (int k = 0; std::getline(p, part, ':'); ++k) {
            if (k == 0) v.areaId = part;
            else if (k == 1) v.seed = (uint32_t)u64(part);
            else if (k == 2) v.level = atoi(part.c_str());
            else if (k == 3) v.ms = u64(part);
            else if (k == 4) v.entries = atoi(part.c_str());
        }
        r.subs.push_back(v);
    }
    if (f.size() >= 20) r.countedTick = (uint32_t)u64(f[19]);
    r.state = RunState::Finished;
    return r;
}

std::string Tracker::serialize(int parts) const {
    std::string o = "POE2MAPSTAT\t2\n";
    if (parts & kCampaign) o += joinTab({"CAMPAIGN", campaignStartTime, S((int)cmpClock)});
    if (parts & kMaps) o += joinTab({"STATE", S(generalStart), S(summaryStart), S(summarySkipId), campaignStartTime, S(nextId)});
    std::vector<Run> runs;
    if (parts & kMaps) runs = history;
    if (hasCurrent && (parts & kMaps)) {  // snapshot of the open map (crash safety); on a normal exit suspend() already finished it
        Run c = cur;
        c.activeMs = cur.activeAt(lastTick);
        if (cur.inSegment) c.countedTick = lastTick;
        c.reason = EndReason::Manual;
        runs.push_back(c);
    }
    for (auto& r : runs) o += runLine("RUN", r);
    if (parts & kSummary) {
        o += joinTab({"SUMMARY", S(summaryStart), S(summarySkipId)});
        for (auto& r : summaryArchive) o += runLine("ARCH", r);
    }
    if (parts & kCampaign)
        for (auto& z : zones)
            o += joinTab({"ZONE", S(z.id), z.areaId, z.name, S(z.level), S(z.seed), S(z.town ? 1 : 0), z.firstEnter,
                      z.lastEnter, S(z.activeAt(lastTick)), S(z.entries), S(z.loadSec), S(z.inSegment ? lastTick : z.leftTick),
                      S(z.closed ? 1 : 0)});
    return o;
}

void Tracker::deserialize(const std::string& data, int parts) {
    if (parts == kAll) {
        reset();
    } else {  // only the given parts; the others and the live state stay
        if (parts & kMaps) {
            history.clear();
            generalStart = 0;
            nextId = 1;
        }
        if (parts & (kMaps | kSummary)) {
            summaryStart = 0;
            summarySkipId = -1;
        }
        if (parts & kSummary) summaryArchive.clear();
        if (parts & kCampaign) {
            zones.clear();
            curZone = -1;
            campaignStartTime.clear();
            cmpClock = Clock::Running;
        }
    }
    bool haveCampaignRec = false;
    std::stringstream ss(data);
    std::string line;
    while (std::getline(ss, line)) {
        auto f = splitTab(line);
        if (f[0] == "CAMPAIGN" && f.size() >= 2 && (parts & kCampaign)) {
            campaignStartTime = f[1];
            if (f.size() >= 3) cmpClock = (Clock)std::clamp(atoi(f[2].c_str()), 0, 2);
            haveCampaignRec = true;
        } else if (f[0] == "STATE" && f.size() >= 6) {  // older single files keep the campaign start here too
            if (parts & kMaps) {  // Summary state of older versions, a SUMMARY record (later) overrides it
                generalStart = (size_t)u64(f[1]);
                summaryStart = (size_t)u64(f[2]);
                summarySkipId = atoi(f[3].c_str());
                nextId = std::max(1, atoi(f[5].c_str()));
            }
            if ((parts & kCampaign) && !haveCampaignRec) campaignStartTime = f[4];
        } else if (f[0] == "RUN" && f.size() >= 19 && (parts & kMaps)) {  // v1 files have 4 extra (unused) columns
            Run r = parseRun(f);
            history.push_back(r);
            nextId = std::max(nextId, r.id + 1);
        } else if (f[0] == "SUMMARY" && f.size() >= 3 && (parts & kSummary)) {
            summaryStart = (size_t)u64(f[1]);
            summarySkipId = atoi(f[2].c_str());
        } else if (f[0] == "ARCH" && f.size() >= 19 && (parts & kSummary)) {
            summaryArchive.push_back(parseRun(f));
        } else if (f[0] == "ZONE" && f.size() >= 14 && (parts & kCampaign)) {
            Zone z;
            z.id = atoi(f[1].c_str());
            z.areaId = f[2];
            z.name = f[3];
            z.level = atoi(f[4].c_str());
            z.seed = (uint32_t)u64(f[5]);
            z.town = f[6] == "1";
            z.firstEnter = f[7];
            z.lastEnter = f[8];
            z.activeMs = u64(f[9]);
            z.entries = atoi(f[10].c_str());
            z.loadSec = atof(f[11].c_str());
            z.leftTick = (uint32_t)u64(f[12]);
            z.closed = f[13] == "1";
            zones.push_back(z);
        }
    }
    if (generalStart > history.size()) generalStart = history.size();
    if (summaryStart > history.size()) summaryStart = history.size();

    // Older versions could store the same instance twice (area + seed). Merge such rows into the first one.
    for (size_t i = 0; i < history.size(); ++i)
        for (size_t j = i + 1; j < history.size();) {
            Run& a = history[i];
            const Run& b = history[j];
            if (b.areaId == a.areaId && b.seed == a.seed && a.seed > 1) {
                a.activeMs += b.activeMs;
                a.loadSec += b.loadSec;
                a.entries += b.entries;
                a.hideoutReturns += b.hideoutReturns;
                a.hideoutMs += b.hideoutMs;
                if (!b.endTime.empty()) a.endTime = b.endTime;
                for (auto& x : b.subs) a.subs.push_back(x);
                history.erase(history.begin() + j);
                if (j < generalStart) --generalStart;
                if (j < summaryStart) --summaryStart;
            } else {
                ++j;
            }
        }
}
