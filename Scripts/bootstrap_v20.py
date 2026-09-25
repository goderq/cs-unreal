"""
v2.0 content bootstrap: the five-slot loadout.

  * weapons get CS2-style magazines and reserves (a bought gun comes full);
  * every carried thing is an item with a loadout slot: primary (1), pistol (2),
    knife (3), HE grenade (4), flashbang (5);
  * new: the pistol item, the knife and the flashbang (weapon, item, model);
  * the ammo machine model and the v2.0 stand-in sounds.

Run after Scripts/generate_audio.py:

    UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script=Scripts/bootstrap_v20.py

Idempotent: assets are updated in place. Everything built here is the
project's own (Geometry Script primitives, synthesised sounds) - see
docs/ASSETS.md.
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
LOG_PATH = os.path.join(PROJECT_DIR, "Saved", "Logs", "bootstrap_v20.txt")

AUDIO_DIR = "/Game/Audio/Weapons"
WEAPONS_DIR = "/Game/Weapons"
ITEMS_DIR = "/Game/Items"
KNIFE_DIR = "/Game/Weapons/Knife"
FLASH_DIR = "/Game/Weapons/Flashbang"
PROPS_DIR = "/Game/Environment/Props"
PAINT_BASE = "/Game/Weapons/Materials/M_WeaponPaint"

_log = []


def log(msg):
    line = "[CS-v20] " + str(msg)
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


def data_asset(name, folder, cls):
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    asset, _ = get_or_create(name, folder, cls, factory)
    return asset


# ---------------------------------------------------------------------------
# Audio
# ---------------------------------------------------------------------------

NEW_SOUNDS = ["S_Flashbang_Explode", "S_Flashbang_Ring", "S_Knife_Swing", "S_Knife_HitBody",
              "S_Knife_HitWall", "S_AmmoMachine_Buy"]


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


def sound(name):
    return EAL.load_asset(AUDIO_DIR + "/" + name)


# ---------------------------------------------------------------------------
# Models (Geometry Script)
# ---------------------------------------------------------------------------

def paint(name, folder, rgb, metallic, roughness):
    base = EAL.load_asset(PAINT_BASE)
    mi, _ = get_or_create(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, base)
    MEL.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(srgb(rgb[0]), srgb(rgb[1]), srgb(rgb[2]), 1.0))
    MEL.set_material_instance_scalar_parameter_value(mi, "Metallic", metallic)
    MEL.set_material_instance_scalar_parameter_value(mi, "Roughness", roughness)
    EAL.save_loaded_asset(mi)
    return mi


def glow_material():
    """Unlit orange for the ammo machine screen and sign."""
    mat, created = get_or_create("M_AmmoMachineGlow", PROPS_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if not created:
        MEL.delete_all_material_expressions(mat)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    color = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -500, 0)
    color.set_editor_property("constant", unreal.LinearColor(4.0, 1.6, 0.35, 1.0))
    MEL.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


def options(material_id):
    o = unreal.GeometryScriptPrimitiveOptions()
    o.set_editor_property("material_id", material_id)
    return o


def xform(loc=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1)):
    return unreal.Transform(unreal.Vector(*loc), unreal.Rotator(roll=rot[2], pitch=rot[0], yaw=rot[1]), unreal.Vector(*scale))


CENTER = unreal.GeometryScriptPrimitiveOriginMode.CENTER
BASE = unreal.GeometryScriptPrimitiveOriginMode.BASE


def save_mesh_asset(mesh, name, folder, materials, collision=False):
    path = folder + "/" + name
    ensure_dir(folder)
    created = None
    new_utils = getattr(unreal, "GeometryScript_NewAssetUtils", None)
    if EAL.does_asset_exist(path):
        created = EAL.load_asset(path)
    elif new_utils:
        opts = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        opts.set_editor_property("enable_recompute_normals", False)
        opts.set_editor_property("enable_collision", collision)
        result = new_utils.create_new_static_mesh_asset_from_mesh(mesh, path, opts)
        created = result[0] if isinstance(result, (tuple, list)) else result
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
    if collision:
        # A simple box around the whole cabinet is enough to stand against.
        # The editor subsystem does not exist in a commandlet; Geometry Script does.
        try:
            col = unreal.GeometryScriptCollisionFromMeshOptions()
            col.set_editor_property("method", unreal.GeometryScriptCollisionGenerationMethod.ALIGNED_BOXES)
            col.set_editor_property("max_shape_count", 1)
            unreal.GeometryScript_Collision.set_static_mesh_collision_from_mesh(mesh, created, col)
            log("  collision: box")
        except Exception as e:
            log("  collision: %s" % e)
    EAL.save_loaded_asset(created)
    return created


def build_knife_mesh():
    """A tactical fixed blade along +X: handle at the origin, edge down. Centimetres."""
    mesh = unreal.DynamicMesh()
    blade, handle, guard = 0, 1, 2
    # Handle: slightly flattened, with finger grooves.
    GSP.append_capsule(mesh, options(handle), xform((-5.5, 0, 0), (0, 0, 90), (1.0, 0.72, 1.0)), 1.35, 9.0, 12, 8, 4, CENTER)
    for i in range(3):
        GSP.append_torus(mesh, options(handle), xform((-8.5 + i * 2.6, 0, -0.1), (0, 90, 0), (1, 0.72, 1)),
                         unreal.GeometryScriptRevolveOptions(), 1.25, 0.18, 16, 6, CENTER)
    # Pommel and cross guard.
    GSP.append_cylinder(mesh, options(guard), xform((-10.5, 0, 0), (0, 0, 90)), 1.2, 1.2, 14, 0, True, CENTER)
    GSP.append_box(mesh, options(guard), xform((0.6, 0, 0)), 1.0, 2.4, 4.4, 0, 0, 0, CENTER)
    # Blade: a thin slab for the flat, a wedge-like taper to the point, a spine.
    GSP.append_box(mesh, options(blade), xform((8.0, 0, 0.1)), 14.0, 0.45, 3.0, 0, 0, 0, CENTER)
    GSP.append_cone(mesh, options(blade), xform((15.0, 0, 0.1), (0, 90, 0), (1, 0.16, 1)), 1.55, 0.02, 5.2, 12, 1, True, BASE)
    GSP.append_box(mesh, options(guard), xform((7.0, 0, 1.55)), 11.0, 0.6, 0.35, 0, 0, 0, CENTER)
    return mesh


def build_flashbang_mesh():
    """M84-style: a perforated tube, fuse, spoon and pin ring. Centimetres, Z up."""
    mesh = unreal.DynamicMesh()
    body, dark, steel = 0, 1, 2
    GSP.append_cylinder(mesh, options(body), xform((0, 0, -5.0)), 2.3, 10.0, 24, 1, True, BASE)
    # Rows of vent holes, drawn as dark studs around the tube.
    for row in range(4):
        z = -3.6 + row * 2.2
        for a in range(8):
            theta = 2 * math.pi * (a + 0.5 * (row % 2)) / 8
            GSP.append_cylinder(mesh, options(dark), xform((2.25 * math.cos(theta), 2.25 * math.sin(theta), z),
                                                           (90, math.degrees(theta), 0)), 0.42, 0.25, 10, 0, True, CENTER)
    # End caps.
    GSP.append_cylinder(mesh, options(dark), xform((0, 0, -5.3)), 2.4, 0.4, 24, 0, True, BASE)
    GSP.append_cylinder(mesh, options(dark), xform((0, 0, 5.0)), 2.4, 0.4, 24, 0, True, BASE)
    # Fuse.
    GSP.append_cylinder(mesh, options(dark), xform((0, 0, 5.4)), 1.1, 1.4, 16, 0, True, BASE)
    GSP.append_cylinder(mesh, options(steel), xform((0, 0, 6.8)), 0.6, 0.35, 12, 0, True, BASE)
    # Spoon down the side, pin ring on the other side.
    GSP.append_box(mesh, options(steel), xform((1.3, 0, 7.0)), 3.0, 1.0, 0.22, 0, 0, 0, CENTER)
    GSP.append_box(mesh, options(steel), xform((2.55, 0, 3.2), (-6, 0, 0)), 0.22, 1.0, 7.4, 0, 0, 0, CENTER)
    GSP.append_torus(mesh, options(steel), xform((-0.3, -2.3, 6.2), (0, 0, 90)), unreal.GeometryScriptRevolveOptions(), 1.05, 0.14, 20, 8, CENTER)
    GSP.append_cylinder(mesh, options(steel), xform((-0.3, -1.2, 6.2), (0, 0, 90)), 0.13, 1.4, 8, 0, True, CENTER)
    return mesh


def build_ammo_machine_mesh():
    """A 90 x 110 x 220 cm vending cabinet standing on the floor, front towards +X."""
    mesh = unreal.DynamicMesh()
    cabinet, trim, glass, glow, boxes = 0, 1, 2, 3, 4
    # Plinth and body.
    GSP.append_box(mesh, options(trim), xform((0, 0, 0)), 92.0, 112.0, 10.0, 0, 0, 0, BASE)
    GSP.append_box(mesh, options(cabinet), xform((0, 0, 10)), 88.0, 108.0, 196.0, 0, 0, 0, BASE)
    # Top sign box.
    GSP.append_box(mesh, options(trim), xform((4, 0, 206)), 84.0, 104.0, 18.0, 0, 0, 0, BASE)
    GSP.append_box(mesh, options(glow), xform((46.3, 0, 208)), 0.6, 94.0, 12.0, 0, 0, 0, BASE)
    # Glass front with shelves of ammo boxes behind it.
    GSP.append_box(mesh, options(trim), xform((44.5, -12, 60)), 2.0, 76.0, 128.0, 0, 0, 0, BASE)
    for shelf in range(4):
        z = 68 + shelf * 30
        GSP.append_box(mesh, options(trim), xform((38, -12, z)), 14.0, 70.0, 1.2, 0, 0, 0, BASE)
        for i in range(5):
            GSP.append_box(mesh, options(boxes), xform((38, -40 + i * 14, z + 1.2)), 10.0, 11.0, 9.0 + (i % 2) * 3, 0, 0, 0, BASE)
    GSP.append_box(mesh, options(glass), xform((45.8, -12, 62)), 0.4, 72.0, 124.0, 0, 0, 0, BASE)
    # Control column: screen, buttons, coin slot, delivery tray.
    GSP.append_box(mesh, options(trim), xform((44.5, 38, 60)), 2.0, 26.0, 128.0, 0, 0, 0, BASE)
    GSP.append_box(mesh, options(glow), xform((45.8, 38, 150)), 0.5, 20.0, 14.0, 0, 0, 0, BASE)
    for r in range(3):
        for c in range(3):
            GSP.append_box(mesh, options(cabinet), xform((46.2, 32 + c * 6, 120 - r * 6)), 1.4, 4.5, 4.5, 0, 0, 0, BASE)
    GSP.append_box(mesh, options(cabinet), xform((46.0, 38, 95)), 1.2, 8.0, 1.2, 0, 0, 0, BASE)
    GSP.append_box(mesh, options(trim), xform((40, -12, 22)), 12.0, 60.0, 26.0, 0, 0, 0, BASE)
    GSP.append_box(mesh, options(cabinet), xform((46.2, -12, 26)), 1.0, 54.0, 18.0, 0, 0, 0, BASE)
    return mesh


# ---------------------------------------------------------------------------
# Weapons and items
# ---------------------------------------------------------------------------

# CS2 magazines and reserves: a bought gun always comes with both full.
FIREARMS = {
    # weapon asset key: (magazine, reserve)
    "StarterPistol": (13, 52),
    "AK47": (30, 90),
    "M4": (30, 90),
    "SMG": (30, 120),
    "Shotgun": (8, 32),
    "Sniper": (5, 30),
}

PRIMARY_ITEMS = ["DA_Item_AK47", "DA_Item_M4", "DA_Item_SMG", "DA_Item_Shotgun", "DA_Item_Sniper"]


def update_firearms():
    for key, (mag, reserve) in FIREARMS.items():
        weapon = EAL.load_asset("%s/DA_Weapon_%s" % (WEAPONS_DIR, key))
        if not weapon:
            log("weapons: MISSING DA_Weapon_" + key)
            continue
        try_set(weapon, "kind", unreal.CSWeaponKind.FIREARM)
        try_set(weapon, "magazine_size", mag)
        try_set(weapon, "reserve_ammo", reserve)
        if key == "StarterPistol":
            try_set(weapon, "weapon_id", "pistol")
            try_set(weapon, "display_name", unreal.Text("P2000"))
            try_set(weapon, "description", unreal.Text("Semi-automatic sidearm. Everybody spawns with one; it can be dropped and bought again."))
        EAL.save_loaded_asset(weapon)
        log("weapons: %s %d / %d" % (key, mag, reserve))

    for name in PRIMARY_ITEMS:
        item = EAL.load_asset(ITEMS_DIR + "/" + name)
        if item:
            try_set(item, "loadout_role", unreal.CSLoadoutRole.PRIMARY)
            EAL.save_loaded_asset(item)
    armor = EAL.load_asset(ITEMS_DIR + "/DA_Item_Armor")
    if armor:
        try_set(armor, "loadout_role", unreal.CSLoadoutRole.NONE)
        EAL.save_loaded_asset(armor)


def make_pistol_item():
    item = data_asset("DA_Item_Pistol", ITEMS_DIR, unreal.CSItemDefinition)
    try_set(item, "item_id", "pistol")
    try_set(item, "display_name", unreal.Text("P2000"))
    try_set(item, "description", unreal.Text("Semi-automatic sidearm, 13 rounds. Slot 2."))
    try_set(item, "item_type", unreal.CSItemType.WEAPON)
    try_set(item, "loadout_role", unreal.CSLoadoutRole.PISTOL)
    try_set(item, "weapon", EAL.load_asset(WEAPONS_DIR + "/DA_Weapon_StarterPistol"))
    try_set(item, "placeholder_color", unreal.LinearColor(0.5, 0.5, 0.55, 1.0))
    EAL.save_loaded_asset(item)
    log("items: DA_Item_Pistol")


def make_knife():
    materials = [
        paint("MI_Knife_Blade", KNIFE_DIR, (176, 178, 182), 1.0, 0.22),
        paint("MI_Knife_Handle", KNIFE_DIR, (34, 36, 34), 0.0, 0.7),
        paint("MI_Knife_Guard", KNIFE_DIR, (58, 60, 62), 0.9, 0.4),
    ]
    sm = save_mesh_asset(build_knife_mesh(), "SM_Knife", KNIFE_DIR, materials)

    weapon = data_asset("DA_Weapon_Knife", WEAPONS_DIR, unreal.CSWeaponDefinition)
    try_set(weapon, "weapon_id", "knife")
    try_set(weapon, "display_name", unreal.Text("Knife"))
    try_set(weapon, "kind", unreal.CSWeaponKind.KNIFE)
    try_set(weapon, "stance", unreal.CSWeaponStance.KNIFE)
    try_set(weapon, "range", 110.0)
    try_set(weapon, "melee_range", 110.0)
    try_set(weapon, "melee_damage", 40.0)
    try_set(weapon, "melee_heavy_damage", 65.0)
    try_set(weapon, "backstab_multiplier", 2.5)
    try_set(weapon, "magazine_size", 1)
    try_set(weapon, "reserve_ammo", 0)
    try_set(weapon, "rounds_per_minute", 120.0)
    try_set(weapon, "automatic", False)
    try_set(weapon, "base_damage", 1.0)
    swing = sound("S_Knife_Swing")
    if swing:
        try_set(weapon, "equip_sound", swing)
    EAL.save_loaded_asset(weapon)

    item = data_asset("DA_Item_Knife", ITEMS_DIR, unreal.CSItemDefinition)
    try_set(item, "item_id", "knife")
    try_set(item, "display_name", unreal.Text("Knife"))
    try_set(item, "description", unreal.Text("Left mouse slashes, right mouse stabs. From behind it kills. Slot 3; it cannot be dropped."))
    try_set(item, "item_type", unreal.CSItemType.WEAPON)
    try_set(item, "loadout_role", unreal.CSLoadoutRole.KNIFE)
    try_set(item, "weapon", weapon)
    try_set(item, "world_mesh", sm)
    try_set(item, "placeholder_color", unreal.LinearColor(0.6, 0.62, 0.66, 1.0))
    EAL.save_loaded_asset(item)
    log("knife: SM_Knife, DA_Weapon_Knife, DA_Item_Knife")


def make_grenades():
    # HE: one per player, slot 4.
    he_weapon = EAL.load_asset(WEAPONS_DIR + "/DA_Weapon_Grenade")
    if he_weapon:
        try_set(he_weapon, "kind", unreal.CSWeaponKind.GRENADE)
        try_set(he_weapon, "stance", unreal.CSWeaponStance.GRENADE)
        EAL.save_loaded_asset(he_weapon)
    he_item = EAL.load_asset(ITEMS_DIR + "/DA_Item_Grenade")
    if he_item:
        try_set(he_item, "loadout_role", unreal.CSLoadoutRole.FRAG)
        try_set(he_item, "stackable", True)
        try_set(he_item, "max_stack", 1)
        try_set(he_item, "description", unreal.Text("HE grenade. Slot 4: pull the pin and throw with the left mouse button; it goes off 1.6 s later. Walls stop the blast."))
        EAL.save_loaded_asset(he_item)

    # Flashbang: up to two, slot 5.
    materials = [
        paint("MI_Flash_Body", FLASH_DIR, (72, 76, 70), 0.35, 0.5),
        paint("MI_Flash_Dark", FLASH_DIR, (30, 31, 30), 0.6, 0.45),
        paint("MI_Flash_Steel", FLASH_DIR, (168, 168, 172), 1.0, 0.28),
    ]
    sm = save_mesh_asset(build_flashbang_mesh(), "SM_Flashbang", FLASH_DIR, materials)

    weapon = data_asset("DA_Weapon_Flashbang", WEAPONS_DIR, unreal.CSWeaponDefinition)
    try_set(weapon, "weapon_id", "flashbang")
    try_set(weapon, "display_name", unreal.Text("Flashbang"))
    try_set(weapon, "kind", unreal.CSWeaponKind.GRENADE)
    try_set(weapon, "stance", unreal.CSWeaponStance.GRENADE)
    try_set(weapon, "magazine_size", 1)
    try_set(weapon, "reserve_ammo", 0)
    try_set(weapon, "rounds_per_minute", 60.0)
    try_set(weapon, "automatic", False)
    try_set(weapon, "base_damage", 1.0)
    pin = sound("S_Grenade_Pin")
    if pin:
        try_set(weapon, "equip_sound", pin)
    EAL.save_loaded_asset(weapon)

    item = data_asset("DA_Item_Flashbang", ITEMS_DIR, unreal.CSItemDefinition)
    try_set(item, "item_id", "flashbang")
    try_set(item, "display_name", unreal.Text("Flashbang"))
    try_set(item, "description", unreal.Text("Blinds everyone who looks at it, for up to four and a half seconds. Slot 5, carry up to two."))
    try_set(item, "item_type", unreal.CSItemType.GRENADE)
    try_set(item, "loadout_role", unreal.CSLoadoutRole.FLASH)
    try_set(item, "stackable", True)
    try_set(item, "max_stack", 2)
    try_set(item, "weapon", weapon)
    try_set(item, "world_mesh", sm)
    try_set(item, "placeholder_color", unreal.LinearColor(0.35, 0.37, 0.33, 1.0))
    EAL.save_loaded_asset(item)
    log("flashbang: SM_Flashbang, DA_Weapon_Flashbang, DA_Item_Flashbang")


def make_ammo_machine():
    materials = [
        paint("MI_AmmoMachine_Body", PROPS_DIR, (46, 52, 44), 0.55, 0.5),
        paint("MI_AmmoMachine_Trim", PROPS_DIR, (26, 27, 28), 0.8, 0.35),
        paint("MI_AmmoMachine_Glass", PROPS_DIR, (40, 55, 60), 0.0, 0.05),
        glow_material(),
        paint("MI_AmmoMachine_Boxes", PROPS_DIR, (96, 84, 46), 0.0, 0.8),
    ]
    save_mesh_asset(build_ammo_machine_mesh(), "SM_AmmoMachine", PROPS_DIR, materials, collision=True)
    log("props: SM_AmmoMachine")


MINIMAP_CODE = """
float2 p = (UV - 0.5) * 2.0;
float s = sin(Angle);
float c = cos(Angle);
float2 r = float2(p.x * c - p.y * s, p.x * s + p.y * c);
float2 muv = float2(CenterU, CenterV) + r * Span;
float inside = step(0.0, muv.x) * step(muv.x, 1.0) * step(0.0, muv.y) * step(muv.y, 1.0);
float3 col = Texture2DSample(Map, MapSampler, saturate(muv)).rgb;
col = lerp(float3(0.03, 0.035, 0.045), pow(saturate(col), 0.4545) * 1.05, inside);
float d = length(p);
float ring = saturate(1.0 - abs(d - 0.965) * 55.0);
col = lerp(col, float3(0.85, 0.87, 0.9), ring * 0.75);
float mask = saturate((1.0 - d) * 40.0);
return float4(col, mask * 0.93);
"""


def make_minimap_material():
    """The round, rotating window onto the top-down picture (see UCSMinimap)."""
    mat, created = get_or_create("M_Minimap", "/Game/UI", unreal.Material, unreal.MaterialFactoryNew())
    if not created:
        MEL.delete_all_material_expressions(mat)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

    uv = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -900, -200)
    tex = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, -900, -60)
    tex.set_editor_property("parameter_name", "Map")
    default_tex = unreal.load_object(None, "/Engine/EngineResources/Black.Black")
    if default_tex:
        tex.set_editor_property("texture", default_tex)
    scalars = {}
    for i, (name, value) in enumerate([("CenterU", 0.5), ("CenterV", 0.5), ("Span", 0.25), ("Angle", 0.0)]):
        node = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -900, 80 + i * 90)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        scalars[name] = node

    custom = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, -500, 0)
    custom.set_editor_property("code", MINIMAP_CODE)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property("description", "Minimap")
    inputs = []
    for name in ["UV", "Map", "CenterU", "CenterV", "Span", "Angle"]:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)
    MEL.connect_material_expressions(uv, "", custom, "UV")
    MEL.connect_material_expressions(tex, "", custom, "Map")
    for name, node in scalars.items():
        MEL.connect_material_expressions(node, "", custom, name)

    rgb = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -250, -40)
    rgb.set_editor_property("r", True)
    rgb.set_editor_property("g", True)
    rgb.set_editor_property("b", True)
    rgb.set_editor_property("a", False)
    alpha = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -250, 80)
    for channel in ("r", "g", "b"):
        alpha.set_editor_property(channel, False)
    alpha.set_editor_property("a", True)
    MEL.connect_material_expressions(custom, "", rgb, "")
    MEL.connect_material_expressions(custom, "", alpha, "")
    MEL.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log("ui: M_Minimap")


def main():
    import_sounds()
    update_firearms()
    make_pistol_item()
    make_knife()
    make_grenades()
    make_ammo_machine()
    make_minimap_material()
    log("done")
    flush_log()


try:
    main()
except Exception as e:
    import traceback
    log("FAILED: %s\n%s" % (e, traceback.format_exc()))
    flush_log()
