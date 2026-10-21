# -*- coding: utf-8 -*-
"""Annotate collectibles.json spawns with creature-guard proximity flags.

Sources (first-party, from Remote_FactoryCreatureDumpCommandlet):
  creature_spawns.json  -> { total, matched, pts:[{class,x,y,z}] }
  creature_class_histogram.json -> { ClassName: count }  (every actor class in the map)

The commandlet already applied a first-pass keyword filter (Creature/Fauna/
Spawner/Enemy/Monster/Beast/Hatch/Nest/Alpha/Critter/Arachnid/Stinger) to build
`pts`, but to avoid false positives (e.g. benign "*Nest*" structures) we RE-FILTER
`pts` here against an explicit KEEP whitelist derived by reviewing the histogram.
Reviewing the histogram is mandatory before running: only real creature/guardian
classes should be kept.

Rule: spawn flagged "creatureGuard": true when its HORIZONTAL (XY) distance to the
nearest kept creature point is < CREATURE_RADIUS_M (25 m default -- a warning margin
around a creature's lair/aggro footprint). Tunable.

Coordinates: both collectibles (SCIM-extracted UE world cm) and creature points
(UE GetActorTransform cm) live in the same world-centimetre space, as confirmed by
the gas annotation (SCIM vs UE alignment). Threshold is horizontal only, mirroring
the gas rule.

The C++ loader reads only kind/x/y/z via TryGet*Field, so the extra "creatureGuard"
field is forward-compatible with older DLLs.
"""
import json, math, sys, os, shutil

MOD = "F:/Workbuddy/SMLDev/SMLStarterProject/Mods/Remote_Factory"
CREATURE_SPawNS = f"{MOD}/creature_spawns.json"
COLLECTIBLES = f"{MOD}/collectibles.json"
CREATURE_RADIUS = 2500.0  # 25 m in world cm (horizontal warning radius)

# Whitelist of creature/guardian class-name substrings. Fill in AFTER reviewing
# creature_class_histogram.json. Anything NOT matching is dropped from the guard set.
# Default mirrors the commandlet's first-pass filter but should be tightened to the
# real creature classes observed in the histogram.
KEEP_SUBSTR = [
    "Creature", "Fauna", "Enemy", "Monster", "Beast", "Critter",
    "Arachnid", "Stinger", "Alpha", "Hatchery", "Nest",
]

def class_kept(clsname):
    for s in KEEP_SUBSTR:
        if s.lower() in clsname.lower():
            return True
    return False

# ---- load creature points (filtered by whitelist) ----
cs = json.load(open(CREATURE_SPawNS, encoding="utf-8"))
pts = cs.get("pts", [])
cpts = []
dropped = 0
for p in pts:
    cls = p.get("class", "")
    if class_kept(cls):
        cpts.append((float(p["x"]), float(p["y"])))
    else:
        dropped += 1
print(f"creature pts total={len(pts)} kept={len(cpts)} dropped={dropped}")
assert cpts, "no creature guard points after whitelist filter"

# ---- load + backup collectibles ----
cj = json.load(open(COLLECTIBLES, encoding="utf-8-sig"))
spawns = cj["spawns"]
bak = f"{MOD}/collectibles.pre-creature.json"
if not os.path.exists(bak):
    shutil.copy2(COLLECTIBLES, bak)
    print("backup ->", bak)

R2 = CREATURE_RADIUS * CREATURE_RADIUS
flagged = 0
per_kind = {}
KINDS = {0: "BlueSlug", 1: "YellowSlug", 2: "PurpleSlug",
         3: "MercerSphere", 4: "Somersloop", 5: "DropPod"}
for s in spawns:
    bx, by = s["x"], s["y"]
    best = 1e18
    for cx, cy in cpts:
        dd = (cx - bx) ** 2 + (cy - by) ** 2
        if dd < best:
            best = dd
    s["creatureGuard"] = best < R2
    if s["creatureGuard"]:
        flagged += 1
        per_kind[s["kind"]] = per_kind.get(s["kind"], 0) + 1

assert len(spawns) == 1764, f"spawn count changed! {len(spawns)}"
json.dump(cj, open(COLLECTIBLES, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
print(f"flagged creatureGuard: {flagged} of {len(spawns)}")
print("by kind:", {KINDS[k]: v for k, v in sorted(per_kind.items())})
print("OK")
