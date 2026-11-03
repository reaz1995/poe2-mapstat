- New Layouts tab: gallery of zone and map layout screenshots, grouped by Act 1-4, Interlude 1-3 and Maps;
  click selects, double-click opens, F2 renames, Delete removes; drag pictures onto the tab to copy them in
- New hotkey Layout screenshot: saves the game window (without HUD, minimap corner and character) to the zone
  you are in; Options can switch it to selecting an area with the Windows snipping tool
- Screenshots of the map overlay become outline pictures; pieces of the same instance are joined where they overlap,
  points of interest get a ring (yellow = exit, blue = checkpoint / waypoint)
- Make graph (Layouts tab): where the named points of a zone were across runs, and which ones stay in place;
  runs are lined up on the entrances, places inside are grouped into the spots they swap between; labels are
  matched by their text and drawn sharp, checkpoints are small dots
- New GraphOverlay: the graph of the zone you are in, arrows to page through several
- New LayoutOverlay: after a layout screenshot, the outline of the instance as it stands now (gone in another
  instance)
- Overlays are left out of screenshots (Windows 10 2004+), so the layout screenshot no longer blacks them out
- Click-through overlays (Options, on by default): clicks reach the game; hold Alt to move or click an overlay
  (a right-click menu opened with Alt stays open when Alt is let go)
- Hotkeys: no Enable checkbox any more, no keys set at first (pick them in Options); right-click a key to remove it
- Screenshots of a zoomed map overlay join outlines made at another zoom
- Outlines: labels are found by the black edge around their letters (Waypoint, checkpoints touching a line or
  icon are no longer missed), drawn sharp from one screenshot, and less ground texture is left around them;
  Make graph remakes older outlines from their screenshots
- Graph: labels found several times in a run besides checkpoints (Obelisk of Rust) are places of their own;
  turned and mirrored layouts are recognized from their map walls and lined up with the others; runs are
  fitted onto the reference map; checkpoints that 2+ runs share get a ring
- One outline per instance: of pieces that do not overlap yet, the larger outline is kept (pieces stay saved)
- Graph: runs are also lined up on the other named places they share with the reference; a run whose map
  matches the reference in no way is left out
- Outlines: an icon counts only right under its label (no more rings and smears from orange glows)
- Outlines: big names the game writes over things in the world (a checkpoint's "CHECKPOINT") are left out
- Layouts tab follows changes made in Explorer (pictures added, removed or renamed)
- Fixed: overlays staying behind the game after switching to another window and back
