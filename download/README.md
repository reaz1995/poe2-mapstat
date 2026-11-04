# Downloads

Files anyone can download and import into Poe2 | MapStat. Contributions are welcome: open a pull request that adds
files here.

## StepsPresets

Leveling steps for the **Steps** tab, one `.yaml` file per preset (zone at the start of a line, its steps under it
indented by 2 spaces; see the README of the app). Import them with **Steps → Import...**: pick one or more files, or a
folder to import every preset in it. Name the file after what it is, e.g. `Act 1-4 fast - Witch.yaml`.

## ZoneNames

Zone and act names by area id (`zones.txt`, `acts.txt`), the same format as `%APPDATA%\Poe2MapStat\tables`. Load
them with **Options → Load zone names...**: their names replace the app's ("unknown" in them does not).

## Layouts

Layout pictures for the **Layouts** tab: zone outlines (`<seed>.png`) and graphs (`graph.png`), sorted into
`Layouts/<Act>/<Zone>/` (maps go into `Layouts/Maps/<Map>/`). Import with **Layouts → Import... → A folder**: pick
this `Layouts` folder (or any part of it) and every picture goes to the zone its folder names. Pictures already there
are skipped.

To contribute, put your pictures into the zone's folder (you find them in `%APPDATA%\Poe2MapStat\layouts`, *Open
folder* on the Layouts tab). Outlines bring their points along when you copy the zone's `Metadata` folder too.

The empty zone folders are made by `tools/make_download_folders.py` from the zone tables in `tables/`.
