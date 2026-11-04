"""Rewrites tables/zones.txt and tables/acts.txt from the game's WorldAreas table.

Export data/worldareas.datc64 as JSON (Poe2 | GgpkTool: Dat viewer -> Export JSON), then:
    python tools/zones_from_worldareas.py worldareas.json
zones.txt: every area, the campaign first in the game's order, then the rest (maps, hideouts, ...).
acts.txt: act names from the acts' world maps (G1_WorldMap = Act 1). Names already in the tables are kept when the
export has none; keys only the tables have stay too (G5 = unknown until the game has it).
"""
import json, re, sys, pathlib

root = pathlib.Path(__file__).resolve().parent.parent / "tables"
rows = json.load(open(sys.argv[1], encoding="utf-8"))

def read(name):
    header, entries = [], {}
    for line in (root / name).read_text(encoding="utf-8").splitlines():
        if line.startswith("#"): header.append(line)
        elif "=" in line:
            k, v = (s.strip() for s in line.split("=", 1)); entries[k] = v
    return header, entries

def write(name, header, entries):
    (root / name).write_text("\n".join(header + [f"{k} = {v}" for k, v in entries.items()]) + "\n", encoding="utf-8")

campaign = re.compile(r"^(C_)?[GP]\d+_")
zh, old_z = read("zones.txt")
zones = {}
for r in rows:  # the game's order: campaign zones first, act by act
    i, n = r.get("Id") or "", (r.get("Name") or "").strip()
    if campaign.match(i) and not i.endswith("_WorldMap"): zones[i] = n or old_z.get(i, "unknown")
for r in rows:
    i, n = r.get("Id") or "", (r.get("Name") or "").strip()
    if i and not campaign.match(i): zones[i] = n or old_z.get(i, "unknown")
for k, v in old_z.items(): zones.setdefault(k, v)
write("zones.txt", zh, zones)

ah, old_a = read("acts.txt")
acts = dict(old_a)
for r in rows:
    m = re.match(r"^(C_)?([GP]\d+)_WorldMap$", r.get("Id") or "")
    if m and (r.get("Name") or "").strip() and m.group(2).startswith("G"):
        key = (m.group(1) or "") + m.group(2)
        if key in acts or acts.get(key, "unknown").lower() == "unknown": acts[key] = r["Name"].strip()
write("acts.txt", ah, acts)
print(f"{len(zones)} zones, {len(acts)} acts written to {root}")
