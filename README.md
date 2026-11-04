# Poe2 | MapStat

by [Rez](https://github.com/reaz1995)

[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-ffdd00?style=for-the-badge&logo=buy-me-a-coffee&logoColor=black)](https://buymeacoffee.com/reaz1995)

A small Windows companion for **Path of Exile 2** that times your maps and campaign zones and tracks your experience
by reading the game's log file (`LatestClient.txt`). No game memory reading, no input automation (except the optional
chat-command buttons and hotkeys), nothing sent anywhere - it only reads the log the game writes anyway.

![MapTimerOverlay](docs/overlay.gif)

*The timer overlay: a map is running (purple), you go to the hideout (the hideout becomes the header, the paused map
moves to the records, orange), back into the same map (the timer continues), then a new map starts.*

### In game

| In a map | In the hideout | Campaign |
|---|---|---|
| ![Map](docs/ingame-map.png) | ![Hideout](docs/ingame-hideout.png) | ![Campaign](docs/ingame-campaign.png) |
| timer, area level and exp lines, last maps below | the map you came from waits in the records | zone timer, zone level range, act and campaign time in the footer |

---

## What it does

- **Times every map run** - maps, logbooks, Vaal Ruins, pinnacle arenas, unique maps. Only time spent *inside* the
  map counts; loading screens and hideout visits are tracked separately.
- **Pauses and resumes** - leaving a map pauses it, re-entering the same instance (same area + seed) continues the
  same row, also after a game crash, a relog or an app restart. Generating a new map finishes the previous one.
- **Picks up where you are** - opened while you are already in a map, zone or hideout, it starts tracking right away.
  If you entered less than 15 minutes ago that time counts; after a longer break the current stay starts at 0.
- **Campaign** - one timer per zone instance, grouped by act, with act and campaign time and load times.
- **Experience** - level, experience to the next level, level penalty, best area level, and every level up with the
  time it took and the exp per hour.
- **Layouts** - your own screenshots of zone and map layouts (none come with the app); pieces of the map overlay are
  joined into one outline per instance, and the outlines of several runs make a rough graph of the zone.
- **Steps** - your leveling notes per zone, on an overlay that follows you through the campaign, with hotkeys to tick
  steps off; a full campaign walkthrough is ready to import.
- **Overlays** - timer, command bar, graph / outline and steps overlays; they follow the game window, are left out
  of screenshots and let clicks through to the game.
- **Nothing gets lost** - sessions are saved every 10 seconds; every reset starts a new file and the old one can be
  restored. Every list exports to CSV.
- **Lives in the tray** - starts hidden, X and minimize hide it, left-click the tray icon to open it.

## The tabs

| Maps: Summary | Experience |
|---|---|
| ![Summary](docs/summary.png) | ![Experience](docs/experience.png) |

*(These two are from an earlier version; the other tabs have their screenshots in their sections.)*

| Tab | For |
|---|---|
| [Maps](#maps) | the maps of the session, and a summary per map type |
| [Campaign](#campaign) | the campaign run zone by zone, and all saved runs side by side |
| [Experience](#experience) | the experience of your level and area, and every level up |
| [Layouts](#layouts) | your screenshots of zone and map layouts, outlines and graphs |
| [Steps](#steps) | your leveling notes per zone, for the StepsOverlay |
| [Options (⚙)](#options-) | log file, overlays, hotkeys, theme |

All lists work the same way: drag a column header to move the column, right-click a header to sort (ascending /
descending / default) or to hide and show columns, and type in the search box next to the first column title to
filter by name or area id (Esc clears it). Order, sorting and hidden columns are remembered, and **Export to CSV**
saves the list as it is shown.

### Maps

Two pills at the top: **Session** (the maps of this session) and **Summary** (one row per map type).

#### Session

![Maps tab, Session: a map paused in the hideout, the maps of the session below](docs/maps-session.png)

The top shows the map you are in (timer, area level, seed, hideout returns, sub-areas); the list under it every map
of the session with its load time, map time and entries. Right-click the *Map* header to filter: All / Maps / Expedition /
Special. Sub-areas (Abyssal Depths with their faction, Lightless Void, Dark Domain, Expedition boss arenas) count
towards the map they were opened from.

| Button | What it does |
|---|---|
| Reset Session | starts a new map session (the old one stays restorable) |
| Export to CSV | saves the list as it is shown |
| Restore map session... | loads an older map session file |
| Load past Map session events... | rebuilds the list from the game's logs, see [Pulling back old data](#pulling-back-old-data) |

#### Summary

One row per map type: plays, total, average and best time, and the area level range (hover the *Area
Lvl* cell for the plays per level). Maps that were generated but never entered are not counted. Summary keeps its own
history: resetting, restoring or loading past events only adds maps to it, each instance once. Only **Reset Summary**
clears it.

### Campaign

Two pills at the top: **Session** (the run you are playing) and **Summary** (every saved run).

#### Session

![Campaign tab, Session: the zone you are in with its timer, the act pills and the zones of the act](docs/campaign-session.jpg)

The big timer is the time of the zone you are in; the smaller one under it includes the loading screens. All other
times leave loading screens out. Right-click the timer for the run's controls:

| Timer menu | What it does |
|---|---|
| Pause / Resume | stops counting time; zones you enter are still listed (greyed out once the run is stopped) |
| Stop | ends the run: the times freeze and nothing is tracked until a new run starts (Save and Clear) |
| Generate Act Summary | adds the Act Summary pill below for a run still going |
| Save and Clear | keeps the run in the saved runs and starts a new one from zero (asks first) |

Under **ZONES** are the pills: *Campaign* for the whole run, then one per act in the order you played them, picked
automatically as you move on. Each zone instance is one row: area level, area id, seed, first and last entered,
entries, time, load time and average load. Coming back to the same instance within an hour continues its row.

Once the run is stopped an **Act Summary** pill is added: one row per act with its time, start and end, loading time,
number of zones (towns not counted) and average per zone, and a row for the whole campaign. **Export to CSV**, under
the pills, saves the pill shown.

The hotkeys for Stop, Pause / resume and Start new run are set in Options (none at first). They work while the game
or the app is in front, and the key still reaches the game.

#### Summary

![Campaign tab, Summary: the saved runs with ★ on the best times, a hovered act showing its zones, two runs compared; on the right the overlay with the Time Comparison AddOn](docs/campaign-summary.jpg)

Two lists: **Saved runs** on top and the **Comparison** of the selected runs under it. Hover a list's title for a
short how-to. *Include loading time* (under the pills) adds the loading screens to every time on this page.

**Saved runs** - every run, newest first, with one column per act and the campaign time; the run being played is
green. A ★ marks the best time of all runs for each act (finished acts only: another act came after it, or the run
was stopped) and for the campaign (stopped runs only).

| Action | What it does |
|---|---|
| Click / **Ctrl + click** | selects a run / adds or removes one from the comparison (the selection is remembered) |
| Double-click | loads the run (the one being played is saved first) |
| Hover an act's time | its zones and their times (towns left out; a zone played more than once shows its fastest instance) |
| Right-click → **Pin for overlay comparison** | the overlay compares with this run (📌 before its name); *Unpin* takes it off |
| Right-click → **Rename** | a name of your own (empty = back to the date) |
| Right-click → **Remove** | the run's file goes to the Recycle Bin; with several runs selected, all of them |
| **Import from file...** | adds a campaign run file (from another PC or a backup); a file with another name gets a new one and keeps its old name as the run's name |
| **Export to CSV** | saves the list |

A removed run leaves the comparison, and if it was pinned the overlay goes back to comparing with the best times.

**Comparison** - the first selected run is the base and shows its times; each other selected run shows the
difference to it per act and for the campaign: `+15s`, `-25s`, `+2m03s` (green = faster, red = slower). Hover an act:
the base shows its zone times, the other runs each zone's difference to the base (a zone only one of the two has
counts with its whole time). **Export to CSV** saves this list.

Drag a column header to move it; both lists keep the same order.

### Experience

Type your character name and level once; after that the level follows the log on every level up. The area level
follows the map or zone you are in, or type one to compare. Tablet and map effectiveness and tablet experience gain
go in the second row.

| Row | Meaning |
|---|---|
| Required exp to level up | experience from the current level to the next one |
| Safe range | areas within `3 + level / 16` levels give full experience |
| Monster base exp | experience base for the area level; each monster multiplies it by its own factor |
| Effectiveness | *Tablet effectiveness %* + *Map effectiveness %*: every 2% gives 1% more experience |
| Penalty multiplier | `((L + 5) / (L + 5 + ED^2.5))^1.5`, ED = levels outside the safe range |
| Experience gain | *Tablet exp gain %*, applied after the level penalty |
| Expected exp | base x effectiveness x penalty x gain, compared to a monster of your own level |
| Best area | the area level with the most experience per monster for your level |
| Last level up | when, where and how long ago |
| Time between level ups | how long the last level took |
| Exp per hour | the experience of that level over the time it took |

**Level ups** lists every level up of the character, with two pills: **Campaign** (acts and interludes) and **Maps**
(everything after). Columns: zone, area level, level before and after, time to level, exp/h and when. When you type a
name, earlier level ups are read from `Client.txt` in the background; new ones are added as they happen. Exp/h is only
given for a level gained wholly in one zone (no town visit, zone change or relog during it); the others show `-`.
**Export to CSV** saves the pill shown. Kept in `%APPDATA%\Poe2MapStat\levelups\<name>.tsv`.

The experience lines on the timer overlay are the *Experience AddOn* in Options (see [Overlays](#overlays)).

### Layouts

Screenshots of zone and map layouts, to remember where the exit or the boss was. **No layouts come with the app:**
you take them yourself as you play, or import a set someone else has already made (shared ones go into
[download/Layouts](download/Layouts)).

On the left there is one group per act and interlude, plus Maps, with the zones of the open group. Click a zone to see
its pictures, newest first. It works like Explorer: click selects, double-click (or Enter) opens the picture, F2
renames, Delete moves it to the Recycle Bin, right-click adds *Show in folder*. Pictures dragged onto the tab are
copied into the zone. Changes made in Explorer show up by themselves.

| Button | What it does |
|---|---|
| Import... | *Pictures into <zone>*, or *A folder*: every picture goes to the zone its folders name (`<Act>\<Zone>\`, a zone id like `G1_3\`, or `Maps\<map>\`); pictures already there are skipped |
| Make graph | builds a graph from the zone's outlines (see below) |
| Open folder | opens the zone's folder |

**Taking screenshots.** Set the *Layout screenshot* hotkey in Options, then press it in a zone or map. The game window
is saved to the zone's folder and a tray notification confirms it. The parts of the screen that never move are cut
or blanked (top bar, sides, HUD, minimap corner, your character) so they can't make two pictures look alike. With
*Select the area* on (Options, Hotkeys) the Windows snipping tool opens instead.

**Outlines.** The map overlay (Tab) rarely fits on one screen, so take it piece by piece: open the overlay, press the
hotkey, walk on, press it again. Every screenshot that shows the overlay becomes part of the instance's outline,
`<seed>.png`: the walls on black, with the labelled places (checkpoints, waypoints, exits) ringed. Pieces are matched
where their walls overlap and joined in the background, also across zoom levels; a piece that overlaps nothing yet is
kept until one does. The original screenshots stay in `pieces\<seed>`. Tip: use the same overlay zoom every run, a bit
zoomed in, so the labels are sharp.

**Graphs.** *Make graph* lines up the outlines of the selected zone (one per run) on their entrances, turning back
runs the game rotated or mirrored, and marks where each place was: entrances that never move get a ring, places
inside get numbered spots with a legend (`4/4 runs · spots 1, 3, 4`), checkpoints are small dots. It is saved as
`graph.png`, then `graph_1.png` and so on. **A graph is a rough guide, not an exact map:** all outlines of a zone are
merged into one picture, so when a zone has several quite different layouts their places end up mixed together
instead of getting a graph per layout.

| A new run, the map overlay still mostly unexplored | Further in: the uncovered map lines up with the graph |
|---|---|
| ![The graph overlay showing the zone's graph at the start of a run](docs/graph-overlay.jpg) | ![The uncovered map overlay matching the graph's layout](docs/graph-overlay-map.jpg) |

Acts and zones are named by the zone tables (see [Zone and act names](#zone-and-act-names)). A zone of an act that
has no name yet is filed under its key (`G5\G5_1`) and moved once the tables name it.

### Steps

Your leveling notes, shown on the StepsOverlay for the zone you are in. The first preset, **Guide**, explains every
rule with a working example; it is built into the app and read only (Save As makes an editable copy).

```
+ Act 1
Clearfell
  Kill Beira of the Rotten Pack
    Reward: cold resistance
  (op) Open the chest by the river
  Take the waypoint
  Notes
    Beira hits hard, bring a flask
Clearfell Encampment -> Clearfell
  Back from town: head for the Grelwood
+
Notes
  Check the vendors for movement speed boots
    every town
```

| Written as | Means |
|---|---|
| a line at the start | a zone (a `:` after it is allowed); its steps go under it, 2 spaces (Tab) in |
| 4 spaces under a step | a detail of that step (a boss, a reward, a hint); lines starting with `Reward` show in gold |
| `(op)` before a step | optional, shown a shade darker |
| `From -> Zone` | steps for the zone only when you came from `From`; otherwise the plain zone steps are shown |
| `Notes` under a zone | notes for that zone, shown as the overlay's footer |
| `Notes` at the start of a line | notes for every zone that has none of its own |
| `+ Header` ... `+` | a section: double-click the header line to fold it into `----- Header`, again to unfold |
| `#` | a comment |

Zone names are suggested as you type them (Up / Down, Enter or Tab to take one, Esc to close). The text saves itself
a second after you stop typing. A zone can be listed more than once; its steps add up.

| Button | What it does |
|---|---|
| Import... | preset files (`.yaml`, `.yml`, `.txt`) or a whole folder; a preset with the same text is skipped |
| Save | saves now (it also saves by itself) |
| Save As... | a new preset in the app (progress goes along), or a copy anywhere else to share |
| Reset progress | marks all steps of the preset as not done |

Right-click a preset for *Delete* and *Show in folder*. Presets live in `%APPDATA%\Poe2MapStat\steps\<name>.yaml`,
their progress in `steps\progress\<name>.txt` (editing other lines keeps it). Under the editor: the preset, its
steps, how many are done, and done / total in the zone you are in. Shared presets, including a full campaign
walkthrough: [download/StepsPresets](download/StepsPresets).

### Options (⚙)

At the top: the log file (`LatestClient.txt`, found by itself on start: running game, Steam libraries, standalone
install), *Always on top*, *Start hidden in tray*, *Check for updates on start* and *Exit app*. Under them three pills
(the chosen one is remembered) and **Load zone names...**, which reads `zones.txt` / `acts.txt` files such as
[download/ZoneNames](download/ZoneNames): their names replace the app's ("unknown" in a file never does) and ids the
app doesn't know yet are added.

**Overlays** - *General*: transparency of all overlays (slider, 0-100%), click-through, *Hide overlays in
screenshots*. Then one row per overlay with its own options on the right:

| Overlay | Options | Default |
|---|---|---|
| MapTimerOverlay | recent records (0-10), *Experience AddOn* (area level, exp bonus, required exp, exp/h), *Time Comparison AddOn* (zone and act times against the pinned or best run) | on, 4, off, off |
| CommandBarOverlay | | off |
| Graph / outline overlay | *Only graphs* | off |
| StepsOverlay | steps shown (1-10), *Show notes* | off, 5, on |

General defaults: transparency 10%, click-through on, hidden in screenshots on.

**Hotkeys** - Stop, Pause / resume, Start new run, Layout screenshot (with *Select the area* for the snipping tool),
Complete step, Undo step. Click one and press the key; Esc cancels, Delete or right-click → *Remove* clears it. None
are set at first.

**Theme** - Default (Dracula), Dark Cyan, Light or Nord, and the app version. *Check for updates* next to ⚙ updates
the app and restarts it (Release builds).

## Overlays

All overlays follow the game window (each keeps its distance to the nearest corner of the game) and are shown while
the game is open and not minimized. They sit right above the game window rather than always on top, so a browser or
this app in front of the game covers them. They snap to each other, to the game and to the screen edges while
dragging. Drag to move, drag the edges to resize, right-click for the menu.

- **Left out of screenshots** (*Hide overlays in screenshots* in Options, on by default; Windows 10 2004 or later):
  the layout screenshot, the snipping tool and other capture tools see the game under them, so they can stay open.
  Turned off (or on older Windows) screenshots show them, except the app's own layout screenshots: for those they
  are hidden for a moment (while the snipping tool opens).
- **Click-through** (Options, on by default): clicks go through the overlays to the game. Hold **Alt** to use them
  (move, resize, click the buttons, right-click menu; a menu opened with Alt opens once Alt is let go).

**MapTimerOverlay** - `lvl  area  timer` as header plus the last 0-10 areas. In the hideout the hideout is the header
and the paused map is the first record. In a sub-area the header shows the sub-area with its own timer; back in the
map it shows the map time again, which includes the time of its sub-areas. In campaign zones a footer shows the
current act and the whole campaign, e.g. `Act 1: 12:40` and `Campaign: 1:05:12`. Zones whose level follows your
character level show their range in small print after the level, e.g. `53 (47-52)`. Double-click opens the app.

With the *Experience AddOn* on (Options), these lines appear under the header (not in hideouts and towns):

| Line | Meaning |
|---|---|
| `Area: 64 (56-64)  Lvl: 60` | the current area level (red when it is outside the safe range, with the level range in small print for zones that follow your level) and your character level |
| `Best: 67` | the area level with the most exp per monster for your level |
| `Exp: +29.82%` | on both lines: expected exp per monster compared to a monster of your own level (green = more, red = less) |
| `Required exp: 1.23M   In Between: 4.56M` | experience to the next level, and the exp per hour of the last level gained in one zone |
| `Total: 3.21M` (campaign) | exp per hour of the whole campaign run: the experience of its level ups over the campaign time |

On a narrow overlay the captions are shorter (`Area:`, `Lvl:`, `Best:`, `Exp:`, `Req:`).

![Timer overlay in a map with the Experience AddOn: a level 12 character in an area 65](docs/overlay-map-exp.jpg)

Timer colours: purple = running, cyan = loading, orange = paused / hideout. Campaign: yellow = paused, red = stopped.

With the *Time Comparison AddOn* on (Options), a campaign zone's timer has the difference to the pinned run (Campaign →
Summary, right-click → Pin) next to it, or with nothing pinned to the best time of all saved runs in that zone:
`(-10s)` in green while you are ahead, `(+5s)` in red once you are behind. Towns are not compared. The footer then also
shows the act you just finished with its difference, e.g. `Act 1: 15:12 (+3m15s)` above `Act 2: 04:10`, and the
current act's time turns red with its difference once it is slower than the act's time to beat.

| In town: the act just finished, 27 minutes ahead | In a zone: StepsOverlay with its notes, timer with the Experience AddOn |
|---|---|
| ![MapTimerOverlay with the previous act's difference, StepsOverlay in town](docs/overlay-town-steps.jpg) | ![StepsOverlay and MapTimerOverlay in Clearfell](docs/overlay-zone-steps.jpg) |

**CommandBarOverlay** - *Hideout* and *Exit* buttons that type `/hideout` or `/exit` into the game chat.

**Graph / outline overlay** - the graphs and outlines of the zone you are in: graphs first (newest first), then the
outlines. The arrows on the left and right page through them; the counter at the bottom says which one is shown
(`2/5 · outline`). After a layout screenshot it shows that instance's outline as it is now. With *Only graphs*
(Options) it shows graphs only. Double-click opens the picture in the Windows photo viewer. On its first start it is
placed under the MapTimerOverlay. Pictures: [Layouts → Graphs](#layouts).

**StepsOverlay** - the steps of the zone you are in (Steps tab): the last one done (green) and the next ones to do,
as many as set in Options, each with its details in small print; optional steps are a shade darker. The zone and
done / total are on top, the notes sit at the bottom as a footer when *Show notes* is on. The *Complete step* hotkey
marks the next step done, *Undo step* the last one. Click a line (Alt + click with click-through on) to copy it,
handy for search regexes; the hovered line is underlined. Double-click the zone name or empty space to open the app
on the Steps tab. The height follows the steps; drag the top or bottom edge for a fixed height (right-click → *Fit
height to steps* goes back), the sides for the width. On its first start it is placed right of the MapTimerOverlay.

## Zone and act names

Every name of a zone or an act (Campaign tab and its act pills, Layouts folders, overlays, steps, level ups) comes from
two tables, `key = name` per line:

| Table | Key | Example |
|---|---|---|
| `tables/zones.txt` | area id | `G1_2 = Clearfell`, `MapAugury = Augury` |
| `tables/acts.txt` | start of the area ids (`G1_2` -> `G1`, `C_G1_2` -> `C_G1`, `P1_3` -> `P1`) | `G1 = Act 1`, `P2 = Interlude 2` |

`unknown` = not known yet: a zone shows the name the game's log gave (or its id), an act shows its key (`G5`). The
repo's tables are baked into the exe; the app keeps an editable copy in `%APPDATA%\Poe2MapStat\tables`. Names typed
there are used at once (layout folders are renamed); `unknown` there is filled in by a newer app version; area ids the
game shows that no table has are added as `unknown`.

`zones.txt` is made from the game files: export `data/worldareas.datc64` as JSON (Poe2 | GgpkTool, Dat viewer) and run
`python tools/zones_from_worldareas.py worldareas.json` (fills both tables, keeps names the export lacks).

## Sessions, reset and restore

Everything is saved in `%APPDATA%\Poe2MapStat\sessions\` every 10 seconds and when the app closes, as three files:

| File | Content | Reset with | Restore with |
|---|---|---|---|
| `MapSession <start date>.tsv` | maps of the Maps tab | **Reset Session** | Maps → **Restore map session...** |
| `CampaignSession <start date>.tsv` | campaign zones | timer menu → **Save and Clear** | Campaign → Summary, double-click the run |
| `SummarySession <start date>.tsv` | what Summary counts | **Reset Summary** | - |

A reset never deletes anything: the current file keeps the state from before the reset and the app continues in a new
file named with the current date. *Restore* loads any older file of that kind (the current one is saved first), so a
reset can always be undone. Map and campaign data are independent - restoring one never touches the other. Restored
maps are added to Summary.

## Pulling back old data

Maps → **Load past Map session events...** rebuilds the Maps list from the game's logs and adds those maps
to Summary (Campaign is left alone):

| Option | Reads |
|---|---|
| This game session | `LatestClient.txt` (the current game session) |
| Last 24 hours / 7 days / 30 days | `Client.txt` - the game's log of **all** sessions |
| Everything | the whole `Client.txt` |
| Custom range... | pick *From* / *To* in a calendar and any `Client.txt` (e.g. a backup) |

`Client.txt` can be hundreds of MB: it is read line by line on a background thread (the button shows the progress)
and for date ranges the app jumps straight to the start date, so only that part is read. The result goes into a new
`MapSession` file - the previous data stays restorable. Older game versions (0.4 era) without loading-screen lines
are supported too.

## Export to CSV

Every tab has **Export to CSV**: it saves exactly what the list shows (current sorting, filter and search, hidden
columns left out) as a CSV file that opens in Excel / LibreOffice / Google Sheets.

## Install

Download `Poe2MapStat.exe` from the [latest release](https://github.com/reaz1995/poe2-mapstat/releases/latest) and run it - no installer, no extra files. On the first start the
window opens; after that the app starts in the tray (icon next to the clock). Only one copy runs at a time.

Windows may show *"Windows protected your PC"* for an unsigned exe from the internet: *More info → Run anyway*.

---

## Build (WSL + llvm-mingw)

```bash
cp .env.example .env            # once: LLVM_MINGW_DIR, DEPLOY_DIR, GITHUB_REPO
cmake -S . -B build             # Release by default; -DCMAKE_BUILD_TYPE=Debug for development
cmake --build build -j          # -> build/Poe2MapStat.exe, copied to DEPLOY_DIR
```

`.env` (git-ignored) holds machine paths and the GitHub repository used for self-update:

| Key | Meaning |
|---|---|
| `LLVM_MINGW_DIR` | root of [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases) |
| `DEPLOY_DIR` | folder the exe is copied to after each build (empty = no copy) |
| `GITHUB_REPO` | `owner/repo` for self-update - baked in **only for Release builds**; empty = no updater |

Themes: every `themes/*.yaml` is baked into the exe at build time (`cmake/embed_themes.cmake`). To add one, drop a
`.yaml` into `themes/` and rebuild - see `themes/dracula.yaml` for what each colour is used for.

Linux test tool for the tracker (prints all runs of a log):

```bash
g++ -std=c++20 -O2 -Isrc/core tools/replay.cpp src/core/*.cpp -o replay && ./replay LatestClient.txt
```

### Releasing an update

The version lives in the `VERSION` file (e.g. `1.1.7`); CMake reads it for the app.

1. Change `VERSION` to the new version.
2. Overwrite `CHANGES.md` with what changed in this version (one `- ` line per change).
3. Commit and push to `main`.

`.github/workflows/release.yml` then builds the Release exe on GitHub (llvm-mingw on Linux, `GITHUB_REPO` set to this
repository) and publishes the release `v1.1.7` with `Poe2MapStat.exe` attached and `CHANGES.md` as the release notes (the app shows them when it offers the update; without
`CHANGES.md` GitHub generates notes from the commits).
A push that doesn't change the version does nothing. It can also be started by hand under *Actions → Release → Run
workflow*.

Installed copies find it on start (or with *Check for updates*), download it over HTTPS, check it (exe header, size,
SHA-256 digest from GitHub), replace themselves and restart. The repository must be public.

## Project structure

```
src/core/      tracker, experience, layout outlines and points graph; no Windows dependencies (also built on Linux)
  log_line          parse one log line
  tracker           event dispatch: area changes, loading screens, scenes, level ups, disconnects
  map_tracker       map runs, segments of play time, hideout returns, Maps/Summary resets
  campaign_tracker  campaign zones, acts, clear, pause / stop
  experience        experience table, safe range, area and penalty multipliers
  levelups          level ups of a character: read from the log, kept per character
  steps             Steps tab text: zones and steps, which to show, done / undo
  area_tables       zone / act name tables (key = name), act key of an area id
  outline / outline_graph   layout outlines (joining screenshots, labels) and the points graph
  area_names        area id -> display name (learned from the log, kept in areas.txt)
  tracker_store     saving / loading MapSession, CampaignSession and SummarySession files
src/shared/    theme parser (YAML subset), GitHub release updater
src/app/       Windows UI
  app.h             shared declarations (state, types, functions of each module)
  main              entry point, window procedure, 0.5 s heartbeat
  log_reader        reading LatestClient.txt, finding it
  sessions          session files, resets, restore
  page_general / page_summary / page_campaign / page_experience / page_layouts / page_steps   the tabs
  hotkeys           campaign timer, layout screenshot and steps hotkeys (low-level keyboard hook)
  lists / widgets / layout                       list views, owner-drawn controls, page layout
  overlay_timer / overlay_cmdbar / overlay_graph (graph / outline overlay) / overlay_steps / overlay_common
  import            Load past Map session events (+ custom range dialog)
  area_tables_ui    loads / saves the name tables (%APPDATA%\Poe2MapStat\tables), follows edits
  theme_ui, tray, csv_export, update_ui, util, state
themes/        built-in themes (baked into the exe)
tables/        zone and act names (baked into the exe; tools/zones_from_worldareas.py makes them from the game files)
download/      files to share: StepsPresets, Layouts/<Act>/<Zone> (tools/make_download_folders.py makes the folders)
cmake/         build helpers
VERSION        app version; a new version pushed to main is released by .github/workflows/release.yml
CHANGES.md     release notes of the current version (overwritten for each release)
```

## Data and privacy

`%APPDATA%\Poe2MapStat\` contains `settings.ini`, `areas.txt`, `sessions\`, `layouts\` and `levelups\`. Nothing leaves your PC; the only network
access is the optional update check against the GitHub repository the exe was built for. The game logs contain your
character name, chat and IP addresses - they are git-ignored and never uploaded.

## License

[PolyForm Noncommercial 1.0.0](LICENSE) - free to use, share and modify for any noncommercial purpose:

- **Credit the creator** - anyone sharing the app or a modified version must pass on the `Required Notice` lines
  from [LICENSE](LICENSE): "Poe2 | MapStat by Rez - [github.com/reaz1995](https://github.com/reaz1995)".
- **No monetizing** - selling it or using it in a paid product or service needs written permission from the creator.
  Playing with it is personal use.

Path of Exile 2 is a trademark of Grinding Gear Games; this project is not affiliated with or endorsed by them.
