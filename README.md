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
- **Logouts and game restarts** - back in the same map, zone or hideout within 15 minutes, the time away counts as if
  you never left (also when pulling back old data). After a longer break it is not counted and a hideout stay starts
  at 0.
- **Summary per map type** - played, total, average and best time per map; it keeps counting across Maps tab resets,
  restores and imports until you reset it yourself.
- **Sub-areas** - Abyssal Depths (with faction: Amanamu / Kurgal / Ulaman), Lightless Void, Dark Domain and
  Expedition boss arenas count towards the map they were opened from *and* get their own row in Summary.
- **Campaign** - every zone instance has its own timer, grouped by act, with act and total campaign time and load
  times. The campaign timer can be paused, stopped and restarted with buttons or hotkeys (F9 / F10 / F11 by default).
- **Experience** - character level (updated from the log on level up), experience to the next level, the level
  penalty for the current area, expected exp per monster and the best area level to level up in.
- **Overlays** - a resizable timer overlay (with optional Safe / Best experience lines) and a command bar
  (`/hideout`, `/exit`). They follow the game window, are shown only while the game is open and not minimized, and
  snap to each other, to the game and to the screen edges.
- **Pull back old data** - rebuild past map sessions from `LatestClient.txt` or the full `Client.txt` (last 24 h,
  7 days, 30 days, everything or a custom date range).
- **Nothing gets lost** - sessions are saved every 10 seconds; every reset starts a new file and the old one can be
  restored. Every list exports to CSV.
- **Lives in the tray** - starts hidden, X and minimize hide it, left-click the tray icon to open it.

## The tabs

| Maps: Session | Maps: Summary | Campaign | Experience |
|---|---|---|---|
| ![Maps](docs/general.png) | ![Summary](docs/summary.png) | ![Campaign](docs/campaign.png) | ![Experience](docs/experience.png) |

**Maps** has two sub-tabs.

*Session* - the current map (timer, area level, seed, hideout returns, sub-areas) and every map of
the session with load time, map time and entries. Right-click the *Map* header to filter: All / Maps / Expedition /
Special.

*Summary* - one row per map type: played, total, average and best time, area level range. Hover the *Area Lvl*
cell to see the plays per area level. Runs that were generated but never entered are not counted. Summary stands on
its own: Reset Session, Restore map session and Load past Map session events on the Maps tab only add maps to it, each map
instance once. Only **Reset Summary** clears it.

**Campaign** - a *Campaign* pill with the whole run (header shows the campaign time and the time with loading screens, the list shows all zones), then one pill per act (selected automatically), one row per zone instance with its time, entries, load time
and average load per entry. The same zone with the same seed re-entered within 1 h continues its row; towns keep
adding up. Under the big zone timer a smaller one shows the zone time including its loading screens; all other times
(lists, totals, overlay) leave loading screens out.

Campaign timer control (buttons next to the timer, or hotkeys set in Options):

| Action | Default key | What it does |
|---|---|---|
| Stop | F9 | ends the run: all times freeze and no zones are tracked until a new run starts |
| Pause / resume | F10 | no time is counted while paused; zones you enter are still listed |
| Start | F11 | only after Stop: starts a new run from zero (the stopped run stays restorable) |

Hotkeys are off by default (*Enable hotkeys* in Options). They work while the game or the app is in front, and the key still reaches the game.

**Experience** - type the character name and level once. The level then follows the log
(`<name> (<class>) is now level 31`). The tab shows:

| Row | Meaning |
|---|---|
| Exp to next level | experience from the current level to the next one |
| Safe range | areas within `3 + level / 16` levels give full experience |
| Monster base exp | Experience base stat for the area level (poe2db monster table); each monster multiplies it by its own factor |
| Effectiveness | *Tablet effectiveness %* + *Map effectiveness %*: every 2% gives 1% more exp (200% = x2), applied to the monster base exp, e.g. `200% | base exp 100,000 -> 200,000` |
| Experience gain | *Tablet exp gain %* (increased experience gain), applied to the exp left after the level penalty, e.g. `50% | exp after penalties 30 -> 45` |
| Penalty multiplier | `((L + 5) / (L + 5 + ED^2.5))^1.5`, ED = levels outside the safe range |
| Expected exp | base exp x effectiveness x penalty x experience gain, and how that compares to a monster of your own level |
| Best area | the area level with the most exp per monster for your level, or a range like `89-99` when several levels give the same, same format |

The area level follows the current map or zone; type another one to compare.

All lists: drag a column header left / right to change the column order, right-click it to sort (ascending /
descending / default order) or hide / unhide columns,
type in the search box next to the first column title to filter by name or Area ID (Esc clears). Order, sorting and
hidden columns are remembered; Export to CSV uses the same order.

## Sessions, reset and restore

Everything is saved in `%APPDATA%\Poe2MapStat\sessions\` every 10 seconds and when the app closes, as three files:

| File | Content | Reset with | Restore with |
|---|---|---|---|
| `MapSession <start date>.tsv` | maps of the Maps tab | **Reset Session** | Maps → **Restore map session...** |
| `CampaignSession <start date>.tsv` | campaign zones | **Reset Campaign Run** / **Start** after Stop | Campaign → **Restore campaign session...** |
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

## Overlays

**MapTimerOverlay** - `lvl  area  timer` as header plus the last 0-10 areas (Options → *Recent records*). In the
hideout the hideout is the header and the paused map is the first record. In a sub-area (Abyssal Depths, Lightless
Void, ...) the header shows the sub-area with its own timer; back in the map it shows the map time again, which
includes the time of its sub-areas. In campaign zones a footer shows the
current act and the whole campaign, e.g. `Act 1: 12:40` and `Campaign: 1:05:12`. Zones whose level follows your character level
(Act 2 wilds, Act 4 islands, Interludes) show their range in small print after the level, e.g. `53 (47-52)`.

With *Show safe area level and exp bonus* on (Experience tab), two lines appear under the header (not in hideouts and
campaign towns):

| Line | Meaning |
|---|---|
| `Area: 64 (56-64)  Lvl: 60` | the current area level (moved here from the header, red when it is outside the safe range of 3 + level / 16 levels around you, with the level range in small print for zones that follow your level) and your character level |
| `Best: 67` | the area level with the most exp per monster for your level, or a range when several levels give the same |
| `Exp: +29.82%` | on both lines: expected exp per monster compared to a monster of your own level (green = more, red = less) |

The example is a level 60 character in an Interlude zone of level 64 (range 56-64). On a wide overlay the captions are longer (`Curr. area:`, `Your lvl:`, `Best area:`, `Exp bonus:`). Drag to move, drag the edges to resize (text scales), double-click
to open the app, right-click for the menu.

![Timer overlay with the experience lines (level 60 in an area 64)](docs/overlay-exp.png)

**CommandBarOverlay** - *Hideout* and *Exit* buttons that type `/hideout` or `/exit` into the game chat.

Both overlays stick to the game window: each keeps its distance to the nearest corner of the game, so they follow
when you move the game, resize it or put it on another monitor. While dragging they snap to each other, to the game
edges and to the screen edges.

Timer colours: purple = running, cyan = loading, orange = paused / hideout. Campaign: yellow = paused, red = stopped.

## Options (⚙)

- `LatestClient.txt` path - found automatically on start (running game, Steam libraries, standalone install),
  otherwise you are asked to pick it.
- Always on top, start hidden in tray, overlays on/off, number of overlay records. Defaults: not on top, start hidden
  in tray, MapTimerOverlay on with 4 records, CommandBarOverlay off, hotkeys off, check for updates on start.
- **Hotkeys** for the campaign timer: *Enable hotkeys* turns them on or off; click a key, press the new one (Esc cancels, Delete removes it).
- **Theme** - built-in themes (Light, Nord) with *Default (Dracula)* at the bottom.
- **Updates** (Release builds) - check on start; the **Check for updates** button next to ⚙ updates and restarts.

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

The version lives in the `VERSION` file (e.g. `1.1.0`); CMake reads it for the app.

1. Change `VERSION` to the new version.
2. Commit and push to `main`.

`.github/workflows/release.yml` then builds the Release exe on GitHub (llvm-mingw on Linux, `GITHUB_REPO` set to this
repository) and publishes the release `v1.1.0` with `Poe2MapStat.exe` attached and notes generated from the commits.
A push that doesn't change the version does nothing. It can also be started by hand under *Actions → Release → Run
workflow*.

Installed copies find it on start (or with *Check for updates*), download it over HTTPS, check it (exe header, size,
SHA-256 digest from GitHub), replace themselves and restart. The repository must be public.

## Project structure

```
src/core/      tracker, no Windows dependencies (also built on Linux by tools/replay.cpp)
  log_line          parse one log line
  tracker           event dispatch: area changes, loading screens, scenes, level ups, disconnects
  map_tracker       map runs, segments of play time, hideout returns, Maps/Summary resets
  campaign_tracker  campaign zones, acts, Reset Campaign Run, pause / stop
  experience        experience table, safe range, area and penalty multipliers
  area_names        area id -> display name (learned from the log, kept in areas.txt)
  tracker_store     saving / loading MapSession, CampaignSession and SummarySession files
src/shared/    theme parser (YAML subset), GitHub release updater
src/app/       Windows UI
  app.h             shared declarations (state, types, functions of each module)
  main              entry point, window procedure, 0.5 s heartbeat
  log_reader        reading LatestClient.txt, finding it
  sessions          session files, resets, restore
  page_general / page_summary / page_campaign / page_experience   the four tabs
  hotkeys           campaign timer hotkeys (low-level keyboard hook)
  lists / widgets / layout                       list views, owner-drawn controls, page layout
  overlay_timer / overlay_cmdbar / overlay_common
  import            Load past Map session events (+ custom range dialog)
  theme_ui, tray, csv_export, update_ui, util, state
themes/        built-in themes (baked into the exe)
cmake/         build helpers
VERSION        app version; a new version pushed to main is released by .github/workflows/release.yml
```

## Data and privacy

`%APPDATA%\Poe2MapStat\` contains `settings.ini`, `areas.txt` and `sessions\`. Nothing leaves your PC; the only network
access is the optional update check against the GitHub repository the exe was built for. The game logs contain your
character name, chat and IP addresses - they are git-ignored and never uploaded.

## License

[PolyForm Noncommercial 1.0.0](LICENSE) - free to use, share and modify for any noncommercial purpose:

- **Credit the creator** - anyone sharing the app or a modified version must pass on the `Required Notice` lines
  from [LICENSE](LICENSE): "Poe2 | MapStat by Rez - [github.com/reaz1995](https://github.com/reaz1995)".
- **No monetizing** - selling it or using it in a paid product or service needs written permission from the creator.
  Playing with it is personal use.

Path of Exile 2 is a trademark of Grinding Gear Games; this project is not affiliated with or endorsed by them.
