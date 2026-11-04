// Leveling steps (Steps tab): a YAML-like list, zones at the start of a line, their steps under them indented
//
//   Clearfell
//     Kill Beira of the Rotten Pack
//       Reward: cold resistance          <- 4 spaces (2 tabs): a detail of the step above
//     (op) Find the Merchant's chest    <- "(op)": optional
//     Notes                             <- notes of this zone (instead of the ones for every zone)
//       Beira hits hard, bring a flask
//   Mud Burrow:
//     - Kill the Devourer
//   The Grim Tangle -> Clearfell        <- only when coming from The Grim Tangle (else the plain Clearfell steps)
//     Take the waypoint to town
//   Notes                               <- notes shown in every zone
//     Check vendors for movement speed boots
//       every town                      <- deeper: smaller, further in
//
// "+ Header" ... "+" at the start of a line: a section the editor can fold, ignored here.
// Indents: a tab counts as 2 spaces. Every line at the start is a zone (an optional ':' at the end), except
// "Notes". Under a zone: 1-3 = a step, 4 or more = a detail of the step above (rewards, hints).
// Steps are kept done / not done by key (zone + text + which repeat of the same text), so editing other lines
// does not lose them.
#pragma once

#include <utility>
#include <set>
#include <string>
#include <vector>

namespace steps {

struct Step {
    std::string zone;  // as written
    std::string from;  // "From -> Zone": only when the zone before was this one ("" = any)
    std::string text;  // without "(op)"
    std::string key;  // zone \x1f text \x1f n
    bool optional = false;  // written with the "(op)" prefix
    std::vector<std::string> sub;  // details: lines indented 4+ under it
};

struct Note {
    std::string text;
    bool sub = false;  // indented deeper than the first note
};

struct Doc {
    std::vector<Step> steps;
    std::vector<Note> notes;  // the "Notes" section: every zone
    std::vector<std::pair<std::string, Note>> zoneNotes;  // (zone as written, note) from "Notes" under a zone
};

Doc parseDoc(const std::string& text);
inline std::vector<Step> parse(const std::string& text) { return parseDoc(text).steps; }

// The notes for a zone: its own, or (when it has none) the ones for every zone
std::vector<Note> notesFor(const Doc& doc, const std::string& zone);

std::string lower(std::string s);

// Steps for the zone the player is in (zone names compare without case), coming from `from`: its "from -> zone" steps
// when there are some for that zone before, else its plain ones. Indices into all.
std::vector<int> forZone(const std::vector<Step>& all, const std::string& zone, bool* zoneSteps = nullptr,
                         const std::string& from = "");

// Which of list to show, at most n, in list order: the last done step before the next one to do, then the steps
// not done yet (done ones are left out). All done: the last one.
std::vector<int> shown(const std::vector<Step>& all, const std::vector<int>& list, const std::set<std::string>& done,
                       int n);

// Completion state: keys of done steps in the order they were done.
struct Progress {
    std::vector<std::string> order;
    std::set<std::string> done;
    bool isDone(const Step& s) const { return done.count(s.key) != 0; }
    void set(const Step& s, bool on);
    bool completeNext(const std::vector<Step>& all, const std::vector<int>& list);  // first not done in list
    bool undoLast(const std::vector<Step>& all, const std::vector<int>& list);      // last done in list
    std::string save() const;
    void load(const std::string& text);
};

}  // namespace steps
