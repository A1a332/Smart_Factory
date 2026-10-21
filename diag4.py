#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Diagnostic: can we make FactoryGame/Remote_Factory classes resolvable in Python?"""
import unreal


def L(m):
    unreal.log_warning("[DIAG4] " + m)


# 1) Try loading the module packages first.
for pkg in ("/Script/Remote_Factory", "/Script/FactoryGame"):
    try:
        p = unreal.load_package(pkg)
        L("load_package(%s) -> %s" % (pkg, (p.get_path_name() if p else None)))
    except Exception as e:
        L("load_package(%s) exc %r" % (pkg, e))

# 2) Retry after package load.
for f in ("/Script/Remote_Factory.UWTResearchTreeNode", "/Script/FactoryGame.UFGResearchTreeNode"):
    try:
        o = unreal.load_object(None, f)
        L("load_object(%s) -> %s" % (f, (o.get_path_name() if o else None)))
    except Exception as e:
        L("load_object(%s) exc %r" % (f, e))

# 3) Try the CDO instance path (an Object, not a Class).
for f in ("/Script/Remote_Factory.Default__UWTResearchTreeNode",
          "/Script/Remote_Factory.Default__URemote_FactoryEquipment"):
    try:
        o = unreal.load_object(None, f)
        L("load_object(CDO %s) -> %s" % (f, (o.get_path_name() if o else None)))
        if o is not None:
            L("  -> get_class() = %s" % o.get_class().get_path_name())
    except Exception as e:
        L("load_object(CDO %s) exc %r" % (f, e))

# 4) Try regenerating the Python stub (if any API exists).
for name in ("reload_python_stub", "regenerate_python_stub", "generate_python_stub"):
    L("hasattr %s: %s" % (name, hasattr(unreal, name)))

L("DONE")
try:
    unreal.SystemLibrary.quit_editor()
except Exception as e:
    L("quit exc %r" % e)
