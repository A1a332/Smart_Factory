#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Comprehensive diagnostic: how to resolve a UClass (native or mod) in this UE Python build."""
import unreal


def L(msg):
    unreal.log_warning("[DIAG2] " + msg)


def try_form(label, fn):
    try:
        o = fn()
        if o is not None:
            L("%s -> %s" % (label, o.get_path_name() if hasattr(o, "get_path_name") else repr(o)))
            return o
        else:
            L("%s -> None" % label)
            return None
    except Exception as e:
        L("%s EXC %r" % (label, e))
        return None


L("=== hasattr checks ===")
L("hasattr UFGResearchTreeNode: %s" % hasattr(unreal, "UFGResearchTreeNode"))
L("hasattr FGResearchTreeNode: %s" % hasattr(unreal, "FGResearchTreeNode"))
L("hasattr UWTResearchTreeNode: %s" % hasattr(unreal, "UWTResearchTreeNode"))
L("hasattr load_class: %s" % hasattr(unreal, "load_class"))
L("hasattr load_object: %s" % hasattr(unreal, "load_object"))
L("hasattr find_class: %s" % hasattr(unreal, "find_class"))

# Try every path form for the ENGINE class (FactoryGame is definitely loaded).
eng_forms = [
    "/Script/FactoryGame.UFGResearchTreeNode",
    "Class'/Script/FactoryGame.UFGResearchTreeNode'",
    "FactoryGame.UFGResearchTreeNode",
    "/Script/FactoryGame.UWTResearchTreeNode",
    "Class'/Script/Remote_Factory.UWTResearchTreeNode'",
    "/Script/Remote_Factory.UWTResearchTreeNode",
    "Remote_Factory.UWTResearchTreeNode",
]
L("=== load_object forms (engine + mod) ===")
for f in eng_forms:
    try_form("load_object(%s)" % f, lambda ff=f: unreal.load_object(None, ff))

if hasattr(unreal, "load_class"):
    L("=== load_class forms ===")
    for f in eng_forms:
        try_form("load_class(%s)" % f, lambda ff=f: unreal.load_class(None, ff))

# Try the generated Python wrapper for engine classes.
L("=== generated python wrappers ===")
for name in ("UFGResearchTreeNode", "FGResearchTreeNode", "UObject", "Actor"):
    try:
        w = getattr(unreal, name, None)
        L("unreal.%s = %s" % (name, (w.get_path_name() if w and hasattr(w, "get_path_name") else repr(w))))
    except Exception as e:
        L("getattr %s EXC %r" % (name, e))

L("DONE")
