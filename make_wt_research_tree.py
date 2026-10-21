#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Create the Wireless Terminal MAM research-tree node Blueprint asset.

WHY
---
Satisfactory 1.0's MAM reads each research node's schematic via
`UFGResearchTreeNode::GetNodeSchematic()`, which is a non-virtual
BlueprintImplementableEvent. A pure-C++ node instance therefore always returns
null (the base thunk does ProcessEvent with no Blueprint graph). The node MUST
be a Blueprint whose GetNodeSchematic graph returns the schematic.

DESIGN
------
We create ONE generic Blueprint node class `BP_WT_ResearchNode` (child of the
C++ `UWTResearchTreeNode`). It implements GetNodeSchematic() to return its
inherited `mNodeSchematic` property. The C++ side (`WT_BuildResearchTrees` /
`ConfigureTree`) instantiates this BP class 60 times (20 per track) and sets
`mNodeSchematic` on each instance at runtime. So a single BP asset wires all
three trees -- no per-node Blueprint needed.

RUN (headless, in the FactoryGame editor)
----------------------------------------
UnrealEditor-Cmd.exe <project>.uproject -ExecutePythonScript=<this file> -unattended -nullrhi
or from the editor Output Log:  py "<path-to-this-file>"

ASSET LOCATION
--------------
Mod content mounts at /Remote_Factory/, so the asset lands in
Mods/Remote_Factory/Content/Research/BP_WT_ResearchNode.uasset and is cooked
by Alpakit into the mod. The C++ LoadClass path is
/Remote_Factory/Research/BP_WT_ResearchNode.BP_WT_ResearchNode_C
"""
import unreal

MOD_MOUNT = "/Remote_Factory"
NODE_DIR = MOD_MOUNT + "/Research"
NODE_NAME = "BP_WT_ResearchNode"
NODE_PATH = NODE_DIR + "/" + NODE_NAME
PARENT_MOD = "Remote_Factory"
PARENT_CLS = "UWTResearchTreeNode"


def log(msg):
    unreal.log("[WT-ResearchNode] " + msg)


def warn(msg):
    unreal.log_warning("[WT-ResearchNode] " + msg)


def err(msg):
    unreal.log_error("[WT-ResearchNode] " + msg)


def resolve_parent():
    """Resolve the C++ parent UClass. Returns (class, how_resolved)."""
    # 1) load_class -- the canonical Python entry point for class objects.
    if hasattr(unreal, "load_class"):
        for form in (
            "/Script/%s.%s" % (PARENT_MOD, PARENT_CLS),
            "Class'/Script/%s.%s'" % (PARENT_MOD, PARENT_CLS),
        ):
            try:
                c = unreal.load_class(None, form)
                if c is not None:
                    return c, "load_class(%s)" % form
            except Exception as e:
                warn("load_class(%s) exc: %r" % (form, e))
    # 2) load_object -- fallback for native classes.
    for form in (
        "/Script/%s.%s" % (PARENT_MOD, PARENT_CLS),
        "Class'/Script/%s.%s'" % (PARENT_MOD, PARENT_CLS),
    ):
        try:
            c = unreal.load_object(None, form)
            if c is not None:
                return c, "load_object(%s)" % form
        except Exception as e:
            warn("load_object(%s) exc: %r" % (form, e))
    # 3) StaticClass via the generated python class? Not available for native mod classes reliably.
    return None, "NONE"


def find_pin(node, direction, want_exec):
    """Return the first pin of `node` with the given direction and exec-ness."""
    for p in node.get_pins():
        if p.direction != direction:
            continue
        try:
            is_exec = (p.get_pin_type() == "exec")
        except Exception:
            is_exec = ("exec" in p.get_name().lower())
        if is_exec == want_exec:
            return p
    return None


def make_variable_get(graph, owner_class, var_name):
    var_get = graph.add_node(unreal.K2Node_VariableGet)
    ref = unreal.MemberReference()
    ref.set_editor_property("member_parent", owner_class)
    ref.set_editor_property("member_name", var_name)
    var_get.set_editor_property("variable_reference", ref)
    return var_get


def main():
    asset_lib = unreal.EditorAssetLibrary
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    # --- idempotent + clean: delete any stale/broken asset, then regenerate ---
    existing = asset_lib.load_asset(NODE_PATH)
    if existing is not None:
        log("stale asset found at %s; deleting for clean regeneration." % NODE_PATH)
        asset_lib.delete_asset(NODE_PATH)

    if not asset_lib.does_directory_exist(NODE_DIR):
        asset_lib.make_directory(NODE_DIR)

    # --- resolve the C++ parent class -----------------------------------------
    parent, how = resolve_parent()
    if parent is None:
        err("parent class %s.%s not found. The Remote_Factory module is not "
            "loaded in this headless editor run. Aborting." % (PARENT_MOD, PARENT_CLS))
        return
    log("parent class resolved (%s): %s" % (how, parent.get_path_name()))

    # --- create the Blueprint -------------------------------------------------
    factory = unreal.BlueprintFactory()
    factory.parent_class = parent
    bp = asset_tools.create_asset(NODE_NAME, NODE_DIR, unreal.Blueprint, factory)
    if bp is None:
        err("create_asset returned None (bad mount path %s?)" % NODE_DIR)
        return
    log("created Blueprint %s" % NODE_PATH)

    # --- implement GetNodeSchematic() -> return mNodeSchematic ----------------
    func_name = "GetNodeSchematic"
    graph = unreal.BlueprintEditorLibrary.get_or_create_function_graph(bp, func_name)
    if graph is None:
        graph = unreal.BlueprintEditorLibrary.create_function_graph(bp, func_name)
    if graph is None:
        err("could not create function graph for %s" % func_name)
        return

    # The override of a BlueprintImplementableEvent comes with an entry Event node.
    event_node = None
    for n in graph.get_all_nodes():
        if isinstance(n, unreal.K2Node_Event):
            event_node = n
            break
    if event_node is None:
        event_node = graph.add_node(unreal.K2Node_Event)
        ev_ref = unreal.MemberReference()
        ev_ref.set_editor_property("member_parent", parent)
        ev_ref.set_editor_property("member_name", func_name)
        try:
            event_node.set_editor_property("event_reference", ev_ref)
        except Exception as e:
            warn("could not set event reference: %s" % e)

    log("event node pins: " + ", ".join(p.get_name() for p in event_node.get_pins()))

    var_get = make_variable_get(graph, parent, "mNodeSchematic")
    log("variable-get pins: " + ", ".join(p.get_name() for p in var_get.get_pins()))

    var_out = find_pin(var_get, unreal.EdGraphPinDirection.EGPD_Output, False)
    ret_pin = find_pin(event_node, unreal.EdGraphPinDirection.EGPD_Output, False)
    if var_out is None or ret_pin is None:
        err("wiring failed: var_out=%s ret_pin=%s" % (var_out, ret_pin))
        return

    var_out.make_link_to(ret_pin)
    log("wired %s -> %s" % (var_out.get_name(), ret_pin.get_name()))

    # --- compile + save -------------------------------------------------------
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    if not asset_lib.save_asset(NODE_PATH):
        err("save failed for %s" % NODE_PATH)
        return
    log("OK: %s compiled and saved. GetNodeSchematic() now returns mNodeSchematic." % NODE_PATH)


if __name__ == "__main__":
    main()
