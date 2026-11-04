- New Layouts tab: gallery of zone and map layout screenshots by act, interlude and Maps (new acts get their group
  from their area ids); click selects, double-click opens, F2 renames, Delete removes; drag pictures in to copy them;
  follows changes made in Explorer
- New hotkey Layout screenshot: saves the game window (without HUD, minimap corner and character) to the zone you
  are in, or an area you select with the Windows snipping tool
- Screenshots of the map overlay become one outline per instance: pieces are joined where they overlap (also at
  another overlay zoom), labels and icons are found and drawn sharp, loose bits of lit ground and the big names the
  game writes over things in the world are left out
- Make graph (Layouts tab): where the named points of a zone were across runs. Runs are lined up on the entrances
  (turned and mirrored layouts too), places inside are grouped into the spots they swap between, places that come
  several times (Obelisk of Rust) and checkpoints are shown too
- New graph / outline overlay: the graphs and outlines of the zone you are in, arrows to page through them; a layout
  screenshot shows its outline; Options can limit it to graphs
- Experience tab: last level up, time between level ups and exp per hour, and a list of all level ups (zone, area
  level, level before / after, time to level up, exp/h), with Campaign and Maps modes; earlier level ups are read from the log for the name typed
- New Steps tab: your leveling steps as a YAML-like list (zones and the steps under them), zone names suggested as
  you type; new StepsOverlay with the steps of the zone you are in (done ones green), marked with the new Complete
  step / Undo step hotkeys, done/total in its corner, double-click opens the Steps tab, number of steps set in Options
- StepsOverlay: top and bottom edges resize it too (a fixed height; right-click → Fit height to steps)
- Campaign: Summary pill once the run is stopped (or right-click the timer → Generate Summary): per act its time,
  start / end, loading time, zones and average time per zone; the Start button after Stop is now Reset
- Steps: the preset is saved by itself 1 s after you stop typing
- Steps: `From -> Zone` headers, steps shown only when coming from that zone (else the plain zone steps)
- Options: pills Overlays (General, then one row per overlay with its options), Hotkeys, Theme; Show notes moved
  there from the Steps tab
- Steps: Guide preset, always first and read only, with all the rules of the steps text (the tip above the editor
  is gone); the editor has a thin scroll bar in the theme's colours
- Options: Load zone names... (zones.txt / acts.txt, e.g. from download/ZoneNames); the Guide preset is built into
  the app (no file)
- Options: the MapTimerOverlay experience lines checkbox moved there from the Experience tab
- Options: Check for updates on start next to Always on top; Layouts: the tip about the screenshot key is gone
- Steps editor: `+ Header` ... `+` sections fold into one line on double-click
- StepsOverlay: the notes are a footer at the bottom of the window, always shown in full like the header (the
  window is never smaller than both); the steps fill the space between
- StepsOverlay: click a line (Alt + click with click-through) to copy it, the hovered line is underlined
- StepsOverlay: done steps are left out except the last one before the next step; reward lines in muted gold
- Steps: a zone's own Notes are shown instead of the Notes for every zone
- Steps: details under a step (indented by 4, smaller on the overlay), optional steps with `(op)` (a shade darker),
  `Notes` for a zone or for every zone shown under a line on the StepsOverlay (*Show notes* in the Steps tab)
- Steps presets: several step lists, picked on the left of the Steps tab; New, Import (files or a whole folder),
  Save, Save As; the chosen one is on the StepsOverlay
- Zone and act names come from tables (made from the game files, editable in %APPDATA%\Poe2MapStat\tables): the
  Campaign tab, act pills, layout folders, overlays and steps use them; acts not known yet show their key (G5) and are
  renamed everywhere once the table names them
- Layouts: Import pictures into a zone, or a whole folder into the zones its folders name
- Overlays sit right above the game window instead of always on top: other windows (this app too) cover them
- download/ folder in the repo: shared steps presets (a full campaign walkthrough: Acts 1-4 and the Interludes, acts
  foldable, quests and rewards under their steps) and
  layouts, ready to import
- Layout outlines are saved as `<seed>.png`, graphs as `graph.png`, `graph_1.png` ...; pictures are listed newest
  first by file date; names of older versions are renamed
- MapTimerOverlay: third experience line with the experience required for the next level and exp/h
- MapTimerOverlay: exp/h labelled In Between; in the campaign a Total line with the exp/h of the whole run
- Campaign: Export to CSV saves the pill shown (whole run, one act or Summary), the file named after it
- Experience: Export to CSV of the level ups shown (Campaign or Maps pill)
- Exp/h only for levels gained wholly in one zone (no town visit, zone change or relog during the level)
- Campaign zones the game has not named yet show their area id
- Overlays are left out of screenshots (Windows 10 2004+; Options: Hide overlays in screenshots); layout screenshots
  hide them in any case
- Click-through overlays (Options, on by default): clicks reach the game; hold Alt to move or click an overlay
- Zone shown right after a teleport is the new one (the Steps / graph overlays showed the previous zone until its
  loading screen ended)
- Options: overlay transparency is a slider (0-100%, 1% steps); overlay settings grouped into General and Overlays
- Hotkeys: always on, no keys set at first (pick them in Options); right-click a key to remove it
- Fixed: overlays staying behind the game after switching to another window and back
