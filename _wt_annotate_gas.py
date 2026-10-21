# -*- coding: utf-8 -*-
"""Annotate collectibles.json spawns with poison-gas proximity flags.

Sources (SCIM mapData, first-party level-extracted):
  options[5].options[] layerId == "sporeFlowers"  -> 651 BP_SporeFlower markers
  options[5].options[] layerId == "pillars"       -> 831 BP_GasPillar markers
Both carry x/y/z in the same UE world-centimetre space as our spawns.

Rule: spawn flagged "gas": true when its HORIZONTAL (XY) distance to the nearest
gas source is < GAS_RADIUS_M (20 m -- spore flower clouds / gas pillar columns are
~15 m; 20 m gives a safe warning margin). Threshold is a display heuristic, tunable.

The C++ loader (Remote_FactoryCollectibles.cpp) reads only kind/x/y/z via
TryGet*Field, so the extra "gas" field is forward-compatible with older DLLs.
"""
import json, math, sys, os, shutil

SCIM = "C:/Users/Administrator/AppData/Local/Temp/scim_mapdata.json"
MOD  = "F:/Workbuddy/SMLDev/SMLStarterProject/Mods/Remote_Factory"
GAS_RADIUS = 2000.0  # 20 m in world cm

d = json.load(open(SCIM, encoding="utf-8-sig"))
gas_pts = []
for grp in d["options"][5]["options"]:
    for sub in grp.get("options", []):
        if sub.get("layerId") in ("sporeFlowers", "pillars"):
            for m in sub.get("markers", []):
                gas_pts.append((float(m["x"]), float(m["y"])))
print("gas sources:", len(gas_pts))
assert gas_pts, "no gas sources parsed"

cj = json.load(open(f"{MOD}/collectibles.json", encoding="utf-8-sig"))
spawns = cj["spawns"]
old_cats = dict(cj["categories"])

# 备份
bak = f"{MOD}/collectibles.pre-gas.json"
if not os.path.exists(bak):
    shutil.copy2(f"{MOD}/collectibles.json", bak)
    print("backup ->", bak)

flagged = 0
per_kind = {}
for s in spawns:
    best = 1e18
    bx, by = s["x"], s["y"]
    for gx, gy in gas_pts:
        dd = (gx-bx)**2 + (gy-by)**2
        if dd < best:
            best = dd
    s["gas"] = best < GAS_RADIUS * GAS_RADIUS
    if s["gas"]:
        flagged += 1
        per_kind[s["kind"]] = per_kind.get(s["kind"], 0) + 1

assert len(spawns) == 1764, f"spawn count changed! {len(spawns)}"
new_cats = {}
for s in spawns:
    pass  # categories 块保持原样（计数未变）

json.dump(cj, open(f"{MOD}/collectibles.json", "w", encoding="utf-8"), ensure_ascii=False, indent=1)
print("flagged gas:", flagged, "of", len(spawns))
KINDS = {0:"BlueSlug",1:"YellowSlug",2:"PurpleSlug",3:"MercerSphere",4:"Somersloop",5:"DropPod"}
print("by kind:", {KINDS[k]: v for k, v in sorted(per_kind.items())})
print("categories unchanged:", old_cats == cj["categories"])
print("OK")
