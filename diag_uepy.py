#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""One-shot diagnostic: discover the UE Python API surface we need for the BP node asset."""
import unreal

def try_load(path):
    try:
        o = unreal.load_object(None, path)
        print("[DIAG] load_object(%s) -> %s" % (path, o.get_path_name() if o else None))
        return o
    except Exception as e:
        print("[DIAG] load_object(%s) EXC %r" % (path, e))
        return None

print("[DIAG] has load_class:", hasattr(unreal, "load_class"))
print("[DIAG] has find_class:", hasattr(unreal, "find_class"))
print("[DIAG] Field* names:", [x for x in dir(unreal) if "Field" in x][:20])
print("[DIAG] ClassProp* names:", [x for x in dir(unreal) if "Class" in x and "Prop" in x][:20])
print("[DIAG] ObjectProp* names:", [x for x in dir(unreal) if "Object" in x and "Prop" in x][:20])
print("[DIAG] load_module?:", [x for x in dir(unreal) if "module" in x.lower()][:20])
print("[DIAG] Modules attr?:", hasattr(unreal, "Modules"))

# Engine classes (should be available regardless of our mod)
try_load("/Script/FactoryGame.UFGResearchTreeNode")
try_load("/Script/FactoryGame.UFGSchematic")
# Our mod class (only if the Remote_Factory module is loaded)
try_load("/Script/Remote_Factory.UWTResearchTreeNode")
try_load("/Script/Remote_Factory.UWTUpgradeResearchStorage_L01")

# FieldType introspection
if hasattr(unreal, "FieldType"):
    ft = unreal.FieldType()
    print("[DIAG] FieldType props:", [p for p in dir(ft) if not p.startswith("__")][:20])
    try:
        print("[DIAG] FieldType.type current:", ft.get_editor_property("type"))
    except Exception as e:
        print("[DIAG] FieldType.type get EXC %r" % e)

print("[DIAG] DONE")
