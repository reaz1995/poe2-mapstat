#include "steps.h"

#include <algorithm>
#include <map>
#include <sstream>

namespace steps {

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// "(op) text" -> optional, "text"
static bool takeOptional(std::string& t) {
    if (t.size() >= 4 && lower(t.substr(0, 4)) == "(op)") {
        t = trim(t.substr(4));
        return true;
    }
    return false;
}

static std::string unbullet(std::string t) {  // "- step" / "* step"
    if (t.size() > 1 && (t[0] == '-' || t[0] == '*') && t[1] == ' ') t = trim(t.substr(2));
    return t;
}

static bool isNotes(const std::string& t) {
    std::string l = lower(t);
    if (!l.empty() && l.back() == ':') l = trim(l.substr(0, l.size() - 1));
    return l == "notes";
}

Doc parseDoc(const std::string& text) {
    struct Line {
        int indent;  // spaces, a tab = 2
        std::string s;
    };
    std::vector<Line> lines;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) {
        std::string t = trim(l);
        if (t.empty() || t[0] == '#') continue;  // blank lines and comments
        if (l[0] == '+') continue;               // "+ Header" / "+": a section of the editor (folding)
        int indent = 0;
        for (char c : l) {
            if (c == ' ') indent += 1;
            else if (c == '\t') indent += 2;
            else break;
        }
        lines.push_back({indent, t});
    }

    Doc doc;
    std::map<std::string, int> seen;  // same zone + text again: a second step
    std::string zone, from;
    bool global = false;  // in the "Notes" section
    int notesAt = -1;     // indent of a zone's "Notes" line: deeper lines are its notes
    int noteBase = -1;    // indent of the first note: deeper is a sub note
    int last = -1;        // the step details go under (in this zone)
    for (const Line& ln : lines) {
        std::string t = ln.s;
        if (ln.indent == 0) {  // a zone, or "Notes"
            last = -1;
            notesAt = noteBase = -1;
            global = isNotes(t);
            if (!global) {
                if (t.back() == ':') t = trim(t.substr(0, t.size() - 1));
                size_t arrow = t.find("->");
                from = arrow == std::string::npos ? "" : trim(t.substr(0, arrow));
                zone = arrow == std::string::npos ? t : trim(t.substr(arrow + 2));
            }
            continue;
        }
        if (global || (notesAt >= 0 && ln.indent > notesAt)) {  // a note
            std::string n = unbullet(t);
            if (n.empty()) continue;
            if (noteBase < 0) noteBase = ln.indent;
            Note note{n, ln.indent >= noteBase + 2};
            if (global) doc.notes.push_back(note);
            else doc.zoneNotes.push_back({zone, note});
            continue;
        }
        notesAt = noteBase = -1;
        if (zone.empty()) continue;  // indented lines before any zone
        if (isNotes(t)) {  // this zone's notes follow
            notesAt = ln.indent;
            last = -1;
            continue;
        }
        if (ln.indent >= 4 && last >= 0) {  // a detail of the step above
            std::string n = unbullet(t);
            if (!n.empty()) doc.steps[last].sub.push_back(n);
            continue;
        }
        t = unbullet(t);
        Step s;
        s.optional = takeOptional(t);
        if (t.empty()) continue;
        s.zone = zone;
        s.from = from;
        s.text = t;
        std::string base = (from.empty() ? "" : lower(from) + " -> ") + lower(zone) + '\x1f' + t;
        s.key = base + '\x1f' + std::to_string(seen[base]++);
        doc.steps.push_back(std::move(s));
        last = (int)doc.steps.size() - 1;
    }
    return doc;
}

std::vector<Note> notesFor(const Doc& doc, const std::string& zone) {
    std::vector<Note> r;
    const std::string z = lower(trim(zone));
    for (auto& [zn, n] : doc.zoneNotes)
        if (!z.empty() && lower(zn) == z) r.push_back(n);
    if (r.empty()) r = doc.notes;  // a zone with notes of its own shows only those
    return r;
}

std::vector<int> forZone(const std::vector<Step>& all, const std::string& zone, bool* zoneSteps,
                         const std::string& from) {
    std::vector<int> r, plain;
    const std::string z = lower(trim(zone)), f = lower(trim(from));
    if (!z.empty())
        for (int i = 0; i < (int)all.size(); ++i) {
            if (lower(all[i].zone) != z) continue;
            if (all[i].from.empty()) plain.push_back(i);
            else if (!f.empty() && lower(all[i].from) == f) r.push_back(i);
        }
    if (r.empty()) r = plain;
    if (zoneSteps) *zoneSteps = !r.empty();
    return r;
}

std::vector<int> shown(const std::vector<Step>& all, const std::vector<int>& list, const std::set<std::string>& done,
                       int n) {
    if (n <= 0 || list.empty()) return {};
    std::vector<int> pick;  // positions in list
    int first = -1;  // the first step not done
    for (int p = 0; p < (int)list.size() && first < 0; ++p)
        if (!done.count(all[list[p]].key)) first = p;
    if (first < 0) {  // all done: the last one
        pick.push_back((int)list.size() - 1);
    } else {
        if (first > 0 && n > 1) pick.push_back(first - 1);  // the done step right before it
        for (int p = first; p < (int)list.size() && (int)pick.size() < n; ++p)
            if (!done.count(all[list[p]].key)) pick.push_back(p);  // steps done out of order are left out
    }
    for (int& p : pick) p = list[p];
    return pick;
}

void Progress::set(const Step& s, bool on) {
    if (on == isDone(s)) return;
    if (on) {
        done.insert(s.key);
        order.push_back(s.key);
    } else {
        done.erase(s.key);
        order.erase(std::remove(order.begin(), order.end(), s.key), order.end());
    }
}

bool Progress::completeNext(const std::vector<Step>& all, const std::vector<int>& list) {
    for (int i : list)
        if (!isDone(all[i])) {
            set(all[i], true);
            return true;
        }
    return false;
}

bool Progress::undoLast(const std::vector<Step>& all, const std::vector<int>& list) {
    // the step of this list that was done last
    for (auto it = order.rbegin(); it != order.rend(); ++it)
        for (int i : list)
            if (all[i].key == *it) {
                set(all[i], false);
                return true;
            }
    return false;
}

std::string Progress::save() const {
    std::string s;
    for (auto& k : order) {
        std::string line = k;
        std::replace(line.begin(), line.end(), '\x1f', '\t');
        s += line + '\n';
    }
    return s;
}

void Progress::load(const std::string& text) {
    order.clear();
    done.clear();
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (l.empty()) continue;
        std::replace(l.begin(), l.end(), '\t', '\x1f');
        if (done.insert(l).second) order.push_back(l);
    }
}

}  // namespace steps
