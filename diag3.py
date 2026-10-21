#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Diagnostic: in the FULL editor (not commandlet), are native classes resolvable?"""
import unreal


def L(m):
    unreal.log_warning("[DIAG3] " + m)


L("hasattr UWTResearchTreeNode: %s" % hasattr(unreal, "UWTResearchTreeNode"))
L("hasattr UFGResearchTreeNode: %s" % hasattr(unreal, "UFGResearchTreeNode"))

c = None
try:
    c = unreal.load_object(None, "/Script/Remote_Factory.UWTResearchTreeNode")
except Exception as e:
    L("load_object(mod) exc %r" % e)
L("load_object UWTResearchTreeNode -> %s" % (c.get_path_name() if c else None))

c2 = None
try:
    c2 = unreal.load_class(None, "/Script/Remote_Factory.UWTResearchTreeNode")
except Exception as e:
    L("load_class(mod) exc %r" % e)
L("load_class UWTResearchTreeNode -> %s" % (c2.get_path_name() if c2 else None))

# Sanity: a guaranteed engine class.
eng = None
try:
    eng = unreal.load_object(None, "/Script/Engine.Actor")
except Exception as e:
    L("load_object(Engine.Actor) exc %r" % e)
L("load_object Engine.Actor -> %s" % (eng.get_path_name() if eng else None))

L("DONE")
try:
    unreal.SystemLibrary.quit_editor()
except Exception as e:
    L("quit_editor exc %r (editor will idle; kill process)" % e)
