"""Creates download/Layouts/<Act>/<Zone>/ for every known zone (tables/acts.txt + tables/zones.txt, and the zone
names), each with a .gitkeep so git keeps the empty folders.

    python tools/make_download_folders.py
"""
import pathlib, re

root = pathlib.Path(__file__).resolve().parent.parent
def table(name):
    out = []
    for line in (root / "tables" / name).read_text(encoding="utf-8").splitlines():
        if line.startswith("#") or "=" not in line: continue
        k, v = (s.strip() for s in line.split("=", 1)); out.append((k, v))
    return out

def act_key(i):
    m = re.match(r"^(?:C_)?([GP]\d+)(?:_|$)", i); return m and m.group(1)

acts = {k: v for k, v in table("acts.txt") if not k.startswith("C_")}
safe = lambda s: re.sub(r'[<>:"/\\|?*]', "_", s).strip(" .")
folders = {}
for zid, zname in table("zones.txt"):
    k = act_key(zid)
    if not k or k not in acts or zname.lower() == "unknown": continue
    a = acts[k] if acts[k].lower() != "unknown" else k
    if not zname.startswith("[DNT") and zname not in folders.setdefault(a, []): folders[a].append(zname)
base = root / "download" / "Layouts"
n = 0
for act, zones in folders.items():
    for z in zones or [None]:
        d = base / safe(act) / safe(z) if z else base / safe(act)
        d.mkdir(parents=True, exist_ok=True)
        (d / ".gitkeep").touch(); n += 1
maps = sorted({n for k, n in table("zones.txt")
               if k.startswith("Map") and n.lower() != "unknown" and not n.startswith("[DNT")})
for m in maps:  # endgame maps (the Layouts tab files them under Maps\<map name>)
    d = base / "Maps" / safe(m)
    d.mkdir(parents=True, exist_ok=True)
    (d / ".gitkeep").touch(); n += 1
print(f"{n} folders under {base}")
