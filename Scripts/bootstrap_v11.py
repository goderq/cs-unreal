"""
v1.1 content bootstrap: real weapon sounds, grenade sounds, the spawn
protection "ghost" material and the frag grenade (model, weapon stand-in,
item).

Run AFTER Scripts/generate_audio.py and Scripts/prepare_weapon_sounds.py:

    UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script=Scripts/bootstrap_v11.py

Idempotent: assets are updated in place.

Sources (docs/ASSETS.md):
  * S_Real_*_Fire: The Free Firearm Sound Library (CC0), cut by prepare_weapon_sounds.py.
  * S_Grenade_*: synthesised by generate_audio.py (project-owned).
  * SM_FragGrenade: built here from Geometry Script primitives (project-owned).
"""

import math
import os
import unreal

ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
GSP = unreal.GeometryScript_Primitives

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
AUDIO_SRC = os.path.join(PROJECT_DIR, "SourceArt", "Audio", "Weapons")
LOG_PATH = os.path.join(PROJECT_DIR, "Saved", "Logs", "bootstrap_v11.txt")

AUDIO_DIR = "/Game/Audio/Weapons"
FX_DIR = "/Game/FX"
GRENADE_DIR = "/Game/Weapons/Grenade"
WEAPONS_DIR = "/Game/Weapons"
ITEMS_DIR = "/Game/Items"
PAINT_BASE = "/Game/Weapons/Materials/M_WeaponPaint"

_log = []


def log(msg):
    line = "[CS-v11] " + str(msg)
    _log.append(line)
    unreal.log(line)


def flush_log():
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
    with open(LOG_PATH, "w") as f:
        f.write("\n".join(_log))


def ensure_dir(path):
    if not EAL.does_directory_exist(path):
        EAL.make_directory(path)


def get_or_create(name, folder, cls, factory):
    path = folder + "/" + name
    if EAL.does_asset_exist(path):
        return EAL.load_asset(path), False
    ensure_dir(folder)
    return ASSET_TOOLS.create_asset(name, folder, cls, factory), True


def try_set(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as e:
        log("  cannot set {0}.{1}: {2}".format(obj.get_name(), prop, e))
        return False


def srgb(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


# ---------------------------------------------------------------------------
# Audio
# ---------------------------------------------------------------------------

NEW_SOUNDS = ["S_Real_Pistol_Fire", "S_Real_AK47_Fire", "S_Real_M4_Fire", "S_Real_SMG_Fire",
              "S_Real_Shotgun_Fire", "S_Real_Sniper_Fire", "S_Grenade_Explode", "S_Grenade_Bounce", "S_Grenade_Pin"]


def import_sounds():
    effects = EAL.load_asset("/Game/Audio/Mix/SC_Effects")
    tasks = []
    for name in NEW_SOUNDS:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", os.path.join(AUDIO_SRC, name + ".wav"))
        task.set_editor_property("destination_path", AUDIO_DIR)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", False)
        tasks.append(task)
    ASSET_TOOLS.import_asset_tasks(tasks)
    for name in NEW_SOUNDS:
        wave = EAL.load_asset(AUDIO_DIR + "/" + name)
        if not wave:
            log("audio: MISSING " + name)
            continue
        if effects:
            try_set(wave, "sound_class_object", effects)
        try_set(wave, "looping", False)
        EAL.save_loaded_asset(wave)
    log("audio: imported %d sounds" % len(NEW_SOUNDS))


FIRE_SOUNDS = {
    "StarterPistol": "S_Real_Pistol_Fire",
    "AK47": "S_Real_AK47_Fire",
    "M4": "S_Real_M4_Fire",
    "SMG": "S_Real_SMG_Fire",
    "Shotgun": "S_Real_Shotgun_Fire",
    "Sniper": "S_Real_Sniper_Fire",
}


def assign_fire_sounds():
    for key, sound_name in FIRE_SOUNDS.items():
        weapon = EAL.load_asset("%s/DA_Weapon_%s" % (WEAPONS_DIR, key))
        sound = EAL.load_asset(AUDIO_DIR + "/" + sound_name)
        if weapon and sound:
            try_set(weapon, "fire_sound", sound)
            EAL.save_loaded_asset(weapon)
            log("weapons: %s fires %s" % (key, sound_name))
        else:
            log("weapons: MISSING %s or %s" % (key, sound_name))


# ---------------------------------------------------------------------------
# Spawn protection material
# ---------------------------------------------------------------------------

def make_ghost_material():
    mat, created = get_or_create("M_SpawnGhost", FX_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if not created:
        MEL.delete_all_material_expressions(mat)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", False)
    for flag in ("used_with_skeletal_mesh", "used_with_static_lighting"):
        try_set(mat, flag, True)

    fade = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -900, 300)
    fade.set_editor_property("parameter_name", "Fade")
    fade.set_editor_property("default_value", 1.0)

    tint = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -900, -200)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.25, 0.62, 1.0, 1.0))

    fres = MEL.create_material_expression(mat, unreal.MaterialExpressionFresnel, -900, 50)
    fres.set_editor_property("exponent", 2.2)
    fres.set_editor_property("base_reflect_fraction", 0.05)

    # Emissive: a soft body glow with bright edges.
    glow = MEL.create_material_expression(mat, unreal.MaterialExpressionConstantBiasScale, -650, 50)
    glow.set_editor_property("bias", 0.12)
    glow.set_editor_property("scale", 2.2)
    MEL.connect_material_expressions(fres, "", glow, "")
    emissive = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -400, -100)
    MEL.connect_material_expressions(tint, "", emissive, "A")
    MEL.connect_material_expressions(glow, "", emissive, "B")
    MEL.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # Opacity: mostly see-through, edges more solid, all of it scaled by Fade.
    edge = MEL.create_material_expression(mat, unreal.MaterialExpressionConstantBiasScale, -650, 250)
    edge.set_editor_property("bias", 0.1)
    edge.set_editor_property("scale", 0.7)
    MEL.connect_material_expressions(fres, "", edge, "")
    opacity = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -400, 250)
    MEL.connect_material_expressions(edge, "", opacity, "A")
    MEL.connect_material_expressions(fade, "", opacity, "B")
    MEL.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log("fx: M_SpawnGhost")


# ---------------------------------------------------------------------------
# Frag grenade model
# ---------------------------------------------------------------------------

def paint(name, rgb, metallic, roughness):
    base = EAL.load_asset(PAINT_BASE)
    mi, _ = get_or_create(name, GRENADE_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, base)
    MEL.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(srgb(rgb[0]), srgb(rgb[1]), srgb(rgb[2]), 1.0))
    MEL.set_material_instance_scalar_parameter_value(mi, "Metallic", metallic)
    MEL.set_material_instance_scalar_parameter_value(mi, "Roughness", roughness)
    EAL.save_loaded_asset(mi)
    return mi


def options(material_id):
    o = unreal.GeometryScriptPrimitiveOptions()
    o.set_editor_property("material_id", material_id)
    return o


def xform(loc=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1)):
    return unreal.Transform(unreal.Vector(*loc), unreal.Rotator(roll=rot[2], pitch=rot[0], yaw=rot[1]), unreal.Vector(*scale))


CENTER = unreal.GeometryScriptPrimitiveOriginMode.CENTER
BASE = unreal.GeometryScriptPrimitiveOriginMode.BASE


def build_grenade_mesh():
    """An M67-style frag: segmented olive body, fuse, spoon and pin ring. Centimetres, Z up."""
    mesh = unreal.DynamicMesh()
    body, dark, steel = 0, 1, 2
    R = 2.9
    SZ = 1.2  # body is a little taller than wide

    GSP.append_sphere_lat_long(mesh, options(body), xform(scale=(1, 1, SZ)), R, 12, 20, CENTER)

    # "Pineapple" segments: raised blocks over the body, five rings of ten.
    rings = 5
    around = 10
    for r in range(rings):
        phi = math.radians(-52 + r * 26)
        for a in range(around):
            theta = 2 * math.pi * (a + 0.5 * (r % 2)) / around
            p = (R * math.cos(phi) * math.cos(theta), R * math.cos(phi) * math.sin(theta), SZ * R * math.sin(phi))
            n = unreal.Vector(p[0], p[1], p[2] / (SZ * SZ))
            n.normalize()
            rot = unreal.MathLibrary.make_rot_from_x(n)
            loc = (p[0] - n.x * 0.1, p[1] - n.y * 0.1, p[2] - n.z * 0.1)
            GSP.append_box(mesh, options(body), unreal.Transform(unreal.Vector(*loc), rot, unreal.Vector(1, 1, 1)),
                           0.55, 1.45, 1.35, 0, 0, 0, CENTER)

    top = SZ * R
    # Fuse: neck, body and cap.
    GSP.append_cylinder(mesh, options(dark), xform((0, 0, top - 0.4)), 1.25, 1.1, 16, 0, True, BASE)
    GSP.append_cylinder(mesh, options(dark), xform((0, 0, top + 0.7)), 0.95, 1.3, 16, 0, True, BASE)
    GSP.append_cylinder(mesh, options(steel), xform((0, 0, top + 2.0)), 0.6, 0.35, 12, 0, True, BASE)

    # Spoon (safety lever): over the fuse, then down the side of the body.
    GSP.append_box(mesh, options(steel), xform((1.3, 0, top + 2.1)), 3.0, 1.0, 0.22, 0, 0, 0, CENTER)
    GSP.append_box(mesh, options(steel), xform((2.95, 0, top - 1.3), (-10, 0, 0)), 0.22, 1.0, 6.4, 0, 0, 0, CENTER)

    # Pull ring and pin on the other side of the fuse.
    revolve = unreal.GeometryScriptRevolveOptions()
    GSP.append_torus(mesh, options(steel), xform((-0.3, -2.3, top + 1.2), (0, 0, 90)), revolve, 1.05, 0.14, 20, 8, CENTER)
    GSP.append_cylinder(mesh, options(steel), xform((-0.3, -1.2, top + 1.2), (0, 0, 90)), 0.13, 1.4, 8, 0, True, CENTER)

    return mesh


def save_mesh_asset(mesh, name, folder, materials):
    path = folder + "/" + name
    ensure_dir(folder)
    created = None
    new_utils = getattr(unreal, "GeometryScript_NewAssetUtils", None)
    if EAL.does_asset_exist(path):
        created = EAL.load_asset(path)
    elif new_utils:
        opts = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        opts.set_editor_property("enable_recompute_normals", False)
        opts.set_editor_property("enable_collision", False)
        result = new_utils.create_new_static_mesh_asset_from_mesh(mesh, path, opts)
        created = result[0] if isinstance(result, (tuple, list)) else result
    if created is None:
        created = ASSET_TOOLS.create_asset(name, folder, unreal.StaticMesh, unreal.StaticMeshFactoryNew() if hasattr(unreal, "StaticMeshFactoryNew") else None)

    copy_opts = unreal.GeometryScriptCopyMeshToAssetOptions()
    copy_opts.set_editor_property("replace_materials", True)
    copy_opts.set_editor_property("new_materials", materials)
    copy_opts.set_editor_property("enable_recompute_normals", False)
    copy_opts.set_editor_property("enable_recompute_tangents", True)
    target = unreal.GeometryScriptMeshWriteLOD()
    unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(mesh, created, copy_opts, target)
    for i, m in enumerate(materials):
        try:
            created.set_material(i, m)
        except Exception as e:
            log("  material slot %d: %s" % (i, e))
    EAL.save_loaded_asset(created)
    return created


def make_grenade():
    materials = [
        paint("MI_Grenade_Body", (78, 90, 52), 0.25, 0.55),
        paint("MI_Grenade_Fuse", (46, 47, 44), 0.8, 0.45),
        paint("MI_Grenade_Steel", (168, 168, 172), 1.0, 0.28),
    ]
    mesh = build_grenade_mesh()
    sm = save_mesh_asset(mesh, "SM_FragGrenade", GRENADE_DIR, materials)
    log("grenade: SM_FragGrenade (%d triangles)" % mesh.get_triangle_count() if hasattr(mesh, "get_triangle_count") else "grenade: SM_FragGrenade")

    # Weapon stand-in: drives the model in hand and the throw rate on the client.
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.CSWeaponDefinition)
    weapon, _ = get_or_create("DA_Weapon_Grenade", WEAPONS_DIR, unreal.CSWeaponDefinition, factory)
    try_set(weapon, "weapon_id", "grenade")
    try_set(weapon, "display_name", unreal.Text("HE Grenade"))
    try_set(weapon, "is_starter_weapon", False)
    try_set(weapon, "magazine_size", 1)
    try_set(weapon, "rounds_per_minute", 60.0)
    try_set(weapon, "automatic", False)
    try_set(weapon, "base_damage", 0.0)
    try_set(weapon, "stance", unreal.CSWeaponStance.PISTOL)
    equip = EAL.load_asset(AUDIO_DIR + "/S_Grenade_Pin")
    if equip:
        try_set(weapon, "equip_sound", equip)
    EAL.save_loaded_asset(weapon)

    item = EAL.load_asset(ITEMS_DIR + "/DA_Item_Grenade")
    if item:
        try_set(item, "display_name", unreal.Text("HE Grenade"))
        try_set(item, "description", unreal.Text("Fragmentation grenade. Select it and press fire to throw; it explodes two seconds later. Walls stop the blast."))
        try_set(item, "weapon", weapon)
        try_set(item, "world_mesh", sm)
        try_set(item, "world_mesh_scale", unreal.Vector(1.0, 1.0, 1.0))
        try_set(item, "max_stack", 2)
        try_set(item, "placeholder_color", unreal.LinearColor(0.3, 0.36, 0.18, 1.0))
        EAL.save_loaded_asset(item)
        log("grenade: DA_Item_Grenade updated")
    else:
        log("grenade: MISSING DA_Item_Grenade")


def main():
    import_sounds()
    assign_fire_sounds()
    make_ghost_material()
    make_grenade()
    log("done")
    flush_log()


try:
    main()
except Exception as e:
    import traceback
    log("FAILED: %s\n%s" % (e, traceback.format_exc()))
    flush_log()
