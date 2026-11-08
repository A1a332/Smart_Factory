# Headless (UnrealEditor-Cmd -run=pythonscript) import of the round-hole FBX
# into the SmartFactory mod's Content, overriding the vanilla
# /Game/Factory/Buildable/Factory/CentralStorage/Mesh/SM_CentralStorage_01.
#
# Run via: import_roundhole.bat (SYSTEM scheduled task).
import unreal, os, traceback

FBX = r"F:/Workbuddy工作间/WorkBuddy/2026-10-30-23-25-26/centralstorage_blender/SM_CentralStorage_01_round.fbx"
DEST_PATH = "/Game/Factory/Buildable/Factory/CentralStorage/Mesh"
ASSET_NAME = "SM_CentralStorage_01"
LOG = r"F:/Workbuddy/SMLDev/import_roundhole.log"
MARKER = r"F:/Workbuddy/SMLDev/import_roundhole.done"

MOD_ROOT = r"F:/SML_Source/Smart_Factory"
PROJ_CONTENT = r"F:/Workbuddy/SMLDev/SMLStarterProject/Content"


def log(s):
    with open(LOG, "a", encoding="utf-8") as f:
        f.write(s + "\n")


def find_uasset(root, name):
    out = []
    for cur, dirs, files in os.walk(root):
        for fn in files:
            if fn == name + ".uasset":
                out.append(os.path.join(cur, fn))
    return out


try:
    log("=== import start ===")
    if not os.path.exists(FBX):
        raise RuntimeError("FBX not found: " + FBX)

    # 1) Capture vanilla mesh materials BEFORE we shadow the path.
    vanilla = unreal.EditorAssetLibrary.load_asset(DEST_PATH + "/" + ASSET_NAME)
    vmats = []
    if vanilla is not None and vanilla.get_class().get_name() == "StaticMesh":
        vmats = vanilla.get_editor_property("static_materials")
        log("vanilla loaded: slots=%d" % len(vmats))
    else:
        log("vanilla NOT loaded as StaticMesh (type=%s)" % (
            vanilla.get_class().get_name() if vanilla else "None"))

    # 2) Import the round-hole FBX.
    at = unreal.AssetToolsHelpers.get_asset_tools()
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", FBX)
    task.set_editor_property("destination_path", DEST_PATH)
    task.set_editor_property("destination_name", ASSET_NAME)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    at.import_asset_tasks([task])
    obj = task.get_editor_property("imported_object")
    if obj is None:
        objs = task.get_editor_property("imported_objects")
        obj = objs[0] if objs and len(objs) > 0 else None
    if obj is None:
        raise RuntimeError("imported_object None; result=%s" % task.get_editor_property("result"))
    mesh = obj
    log("imported: %s (%s)" % (mesh.get_path_name(), mesh.get_class().get_name()))

    # 3) Apply vanilla materials slot-by-slot (so it isn't gray/missing).
    smats = mesh.get_editor_property("static_materials")
    log("mesh_slots=%d" % len(smats))
    applied = 0
    for i in range(len(smats)):
        if i < len(vmats):
            mi = vmats[i].get_editor_property("material_interface")
            if mi is not None:
                mesh.set_material(i, mi)
                applied += 1
    if applied:
        mesh.post_edit_change()
        unreal.EditorAssetLibrary.save_asset(mesh.get_path_name())
        log("materials applied=%d + saved" % applied)
    else:
        log("no vanilla materials applied (mesh keeps imported materials)")

    # 4) Geometry sanity.
    try:
        log("LODs=%d verts_lod0=%d" % (mesh.get_num_lods(), mesh.get_num_vertices(0)))
    except Exception as e:
        log("geom_err: %s" % e)

    # 5) Where did it physically land?
    mod_found = find_uasset(MOD_ROOT, ASSET_NAME)
    proj_found = find_uasset(PROJ_CONTENT, ASSET_NAME)
    log("mod_uasset_found=%s" % mod_found)
    log("proj_uasset_found=%s" % proj_found)

    with open(MARKER, "w", encoding="utf-8") as f:
        f.write("IMPORT_OK\n")
    log("IMPORT_OK")
except Exception as e:
    log("IMPORT_FAIL: %s" % traceback.format_exc())
    with open(MARKER, "w", encoding="utf-8") as f:
        f.write("IMPORT_FAIL\n")
