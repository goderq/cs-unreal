"""
Stage 6 content bootstrap: audio, mixing, effect materials, weapon and item
presentation, map materials.

Run AFTER bootstrap_content.py and Scripts/generate_audio.py:

    UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script=Scripts/bootstrap_stage6.py

Idempotent: existing assets are updated in place, never duplicated.

Sources (see docs/ASSETS.md):
  * Character meshes / animations, weapon meshes, prototype grid materials:
    Epic Games template packs copied from the engine install (UE EULA).
  * Sounds: synthesised by Scripts/generate_audio.py (project-owned).
  * Effect materials: generated here from engine material expressions.
"""

import os
import unreal

ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
AUDIO_SRC = os.path.join(PROJECT_DIR, "SourceArt", "Audio")
LOG_PATH = os.path.join(PROJECT_DIR, "Saved", "Logs", "bootstrap_stage6.txt")

AUDIO_DIR = "/Game/Audio"
MIX_DIR = AUDIO_DIR + "/Mix"
FX_MAT_DIR = "/Game/FX/Materials"
WEAPONS_DIR = "/Game/Weapons"
ITEMS_DIR = "/Game/Items"
MAP_PATH = "/Game/Maps/Lvl_Warehouse"

_log_lines = []


def log(msg):
    line = "[CS-Stage6] " + str(msg)
    _log_lines.append(line)
    unreal.log(line)


def flush_log():
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
    with open(LOG_PATH, "w") as f:
        f.write("\n".join(_log_lines))


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


# ---------------------------------------------------------------------------
# Mixing
# ---------------------------------------------------------------------------

def make_mix():
    music, _ = get_or_create("SC_Music", MIX_DIR, unreal.SoundClass, unreal.SoundClassFactory())
    effects, _ = get_or_create("SC_Effects", MIX_DIR, unreal.SoundClass, unreal.SoundClassFactory())
    mix, _ = get_or_create("SMX_Volumes", MIX_DIR, unreal.SoundMix, unreal.SoundMixFactory())

    att, _ = get_or_create("ATT_World", MIX_DIR, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
    settings = att.get_editor_property("attenuation")
    try_set(settings, "attenuation_shape_extents", unreal.Vector(400.0, 0.0, 0.0))
    try_set(settings, "falloff_distance", 5000.0)
    try_set(settings, "spatialize", True)
    try_set(settings, "attenuate", True)
    att.set_editor_property("attenuation", settings)

    for a in (music, effects, mix, att):
        EAL.save_loaded_asset(a)
    log("mix: SC_Music, SC_Effects, SMX_Volumes, ATT_World")
    return music, effects


# ---------------------------------------------------------------------------
# Audio import
# ---------------------------------------------------------------------------

def import_audio(music_class, effects_class):
    tasks = []
    for category in sorted(os.listdir(AUDIO_SRC)):
        folder = os.path.join(AUDIO_SRC, category)
        if not os.path.isdir(folder):
            continue
        for file in sorted(os.listdir(folder)):
            if not file.lower().endswith(".wav"):
                continue
            task = unreal.AssetImportTask()
            task.set_editor_property("filename", os.path.join(folder, file))
            task.set_editor_property("destination_path", AUDIO_DIR + "/" + category)
            task.set_editor_property("destination_name", os.path.splitext(file)[0])
            task.set_editor_property("replace_existing", True)
            task.set_editor_property("automated", True)
            task.set_editor_property("save", False)
            tasks.append(task)
    ASSET_TOOLS.import_asset_tasks(tasks)

    count = 0
    for task in tasks:
        for path in task.get_editor_property("imported_object_paths"):
            wave = EAL.load_asset(path)
            if not isinstance(wave, unreal.SoundWave):
                continue
            is_music = "/Music/" in path
            try_set(wave, "sound_class_object", music_class if is_music else effects_class)
            try_set(wave, "looping", is_music)
            EAL.save_loaded_asset(wave)
            count += 1
    log("audio: imported {0} sound waves".format(count))


def sound(category, name):
    return EAL.load_asset("{0}/{1}/{2}".format(AUDIO_DIR, category, name))


# ---------------------------------------------------------------------------
# Effect materials
# ---------------------------------------------------------------------------

def param_vector(mat, name, default, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", default)
    return e


def param_scalar(mat, name, default, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", default)
    return e


def radial_mask(mat, x, y, exponent):
    """1 at the UV centre, 0 at the edge: saturate(1 - 2*|uv - 0.5|) ^ exponent."""
    uv = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, x - 800, y)
    centre = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant2Vector, x - 800, y + 120)
    centre.set_editor_property("r", 0.5)
    centre.set_editor_property("g", 0.5)
    dist = MEL.create_material_expression(mat, unreal.MaterialExpressionDistance, x - 600, y)
    MEL.connect_material_expressions(uv, "", dist, "A")
    MEL.connect_material_expressions(centre, "", dist, "B")
    scale = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, x - 450, y)
    scale.set_editor_property("const_b", 2.0)
    MEL.connect_material_expressions(dist, "", scale, "A")
    inv = MEL.create_material_expression(mat, unreal.MaterialExpressionOneMinus, x - 300, y)
    MEL.connect_material_expressions(scale, "", inv, "")
    sat = MEL.create_material_expression(mat, unreal.MaterialExpressionSaturate, x - 200, y)
    MEL.connect_material_expressions(inv, "", sat, "")
    powr = MEL.create_material_expression(mat, unreal.MaterialExpressionPower, x - 100, y)
    powr.set_editor_property("const_exponent", exponent)
    MEL.connect_material_expressions(sat, "", powr, "Base")
    return powr


def make_fx_material(name, blend, radial=False, fresnel_soft=False):
    mat, created = get_or_create(name, FX_MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if not created:
        log("fx: reuse " + name)
        return mat
    mat.set_editor_property("blend_mode", blend)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)

    color = param_vector(mat, "Color", unreal.LinearColor(1, 1, 1, 1), -700, -200)
    intensity = param_scalar(mat, "Intensity", 1.0, -700, 0)
    opacity = param_scalar(mat, "Opacity", 1.0, -700, 200)

    emissive = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -400, -100)
    MEL.connect_material_expressions(color, "", emissive, "A")
    MEL.connect_material_expressions(intensity, "", emissive, "B")

    mask = None
    if radial:
        mask = radial_mask(mat, -200, 400, 2.0)
    elif fresnel_soft:
        fres = MEL.create_material_expression(mat, unreal.MaterialExpressionFresnel, -500, 400)
        fres.set_editor_property("exponent", 2.5)
        inv = MEL.create_material_expression(mat, unreal.MaterialExpressionOneMinus, -300, 400)
        MEL.connect_material_expressions(fres, "", inv, "")
        mask = inv

    if blend == unreal.BlendMode.BLEND_ADDITIVE:
        out = emissive
        if mask is not None:
            out = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -150, -100)
            MEL.connect_material_expressions(emissive, "", out, "A")
            MEL.connect_material_expressions(mask, "", out, "B")
        MEL.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    else:
        MEL.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        op = opacity
        if mask is not None:
            op = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -150, 200)
            MEL.connect_material_expressions(opacity, "", op, "A")
            MEL.connect_material_expressions(mask, "", op, "B")
        MEL.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log("fx: created " + name)
    return mat


def make_bullet_hole():
    mat, created = get_or_create("M_CS_BulletHole", FX_MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if not created:
        log("fx: reuse M_CS_BulletHole")
        return mat
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)

    base = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, -200)
    base.set_editor_property("constant", unreal.LinearColor(0.015, 0.014, 0.013, 1.0))
    MEL.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 0)
    rough.set_editor_property("r", 0.9)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mask = radial_mask(mat, -150, 200, 1.4)
    MEL.connect_material_property(mask, "", unreal.MaterialProperty.MP_OPACITY)

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log("fx: created M_CS_BulletHole")
    return mat


# ---------------------------------------------------------------------------
# Weapons and items
# ---------------------------------------------------------------------------

PISTOL = "/Game/Weapons/Pistol/Meshes/SKM_Pistol"
RIFLE = "/Game/Weapons/Rifle/Meshes/SKM_Rifle"
LAUNCHER = "/Game/Weapons/GrenadeLauncher/Meshes/SKM_GrenadeLauncher"
S = unreal.CSWeaponStance

# key: (mesh, stance, scale, fire, reload, flash, tracer)
WEAPON_LOOK = {
    "StarterPistol": (PISTOL, S.PISTOL, 1.0, "S_Pistol_Fire", "S_Pistol_Reload", 0.8, (1.0, 0.8, 0.45)),
    "AK47": (RIFLE, S.RIFLE, 1.0, "S_Rifle_Fire", "S_Rifle_Reload", 1.1, (1.0, 0.65, 0.3)),
    "M4": (RIFLE, S.RIFLE, 0.95, "S_Rifle2_Fire", "S_Rifle_Reload", 1.0, (1.0, 0.75, 0.35)),
    "SMG": (RIFLE, S.RIFLE, 0.8, "S_SMG_Fire", "S_Rifle_Reload", 0.8, (1.0, 0.8, 0.4)),
    "Shotgun": (LAUNCHER, S.RIFLE, 0.95, "S_Shotgun_Fire", "S_Shotgun_Reload", 1.6, (1.0, 0.7, 0.35)),
    "Sniper": (RIFLE, S.RIFLE, 1.15, "S_Sniper_Fire", "S_Rifle_Reload", 1.4, (0.9, 0.95, 1.0)),
}


def dress_weapons():
    empty = sound("Weapons", "S_Empty")
    equip = sound("Weapons", "S_Equip")
    for key, (mesh, stance, scale, fire, reload, flash, tracer) in WEAPON_LOOK.items():
        path = "{0}/DA_Weapon_{1}".format(WEAPONS_DIR, key)
        weapon = EAL.load_asset(path)
        if not weapon:
            log("weapons: MISSING " + path)
            continue
        skm = EAL.load_asset(mesh)
        try_set(weapon, "first_person_mesh", skm)
        try_set(weapon, "third_person_mesh", skm)
        try_set(weapon, "stance", stance)
        try_set(weapon, "mesh_scale", scale)
        try_set(weapon, "muzzle_flash_scale", flash)
        try_set(weapon, "tracer_color", unreal.LinearColor(tracer[0], tracer[1], tracer[2], 1.0))
        try_set(weapon, "fire_sound", sound("Weapons", fire))
        try_set(weapon, "reload_sound", sound("Weapons", reload))
        try_set(weapon, "empty_sound", empty)
        try_set(weapon, "equip_sound", equip)
        EAL.save_loaded_asset(weapon)
        log("weapons: dressed " + key)


# item suffix: (static mesh, scale)
ITEM_LOOK = {
    "AK47": ("/Game/Weapons/Rifle/Meshes/SM_Rifle", (1.0, 1.0, 1.0)),
    "M4": ("/Game/Weapons/Rifle/Meshes/SM_Rifle", (0.95, 0.95, 0.95)),
    "SMG": ("/Game/Weapons/Rifle/Meshes/SM_Rifle", (0.8, 0.8, 0.8)),
    "Shotgun": ("/Game/Weapons/GrenadeLauncher/Meshes/SM_GrenadeLauncher", (0.95, 0.95, 0.95)),
    "Sniper": ("/Game/Weapons/Rifle/Meshes/SM_Rifle", (1.15, 1.15, 1.15)),
    "AmmoRifle": ("/Game/LevelPrototyping/Meshes/SM_ChamferCube", (0.22, 0.14, 0.16)),
    "AmmoSMG": ("/Game/LevelPrototyping/Meshes/SM_ChamferCube", (0.2, 0.14, 0.14)),
    "AmmoShells": ("/Game/LevelPrototyping/Meshes/SM_ChamferCube", (0.2, 0.2, 0.14)),
    "AmmoSniper": ("/Game/LevelPrototyping/Meshes/SM_ChamferCube", (0.24, 0.12, 0.12)),
    "Armor": ("/Game/LevelPrototyping/Meshes/SM_ChamferCube", (0.45, 0.35, 0.12)),
    "Medkit": ("/Game/LevelPrototyping/Meshes/SM_ChamferCube", (0.3, 0.26, 0.14)),
}


def dress_items():
    for suffix, (mesh, scale) in ITEM_LOOK.items():
        path = "{0}/DA_Item_{1}".format(ITEMS_DIR, suffix)
        item = EAL.load_asset(path)
        if not item:
            log("items: MISSING " + path)
            continue
        try_set(item, "world_mesh", EAL.load_asset(mesh))
        try_set(item, "world_mesh_scale", unreal.Vector(*scale))
        EAL.save_loaded_asset(item)
    log("items: dressed {0}".format(len(ITEM_LOOK)))


# ---------------------------------------------------------------------------
# Map
# ---------------------------------------------------------------------------

def dress_map():
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    level.load_level(MAP_PATH)

    floor = EAL.load_asset("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray")
    wall = EAL.load_asset("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_TopDark")
    cover = EAL.load_asset("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray_02")

    count = 0
    for actor in actors.get_all_level_actors():
        if not isinstance(actor, unreal.StaticMeshActor):
            continue
        label = actor.get_actor_label()
        mat = floor if label == "Floor" else (wall if label.startswith("Wall") else (cover if label.startswith(("Cover", "Mid")) else None))
        if mat:
            actor.static_mesh_component.set_material(0, mat)
            count += 1
    level.save_current_level()
    log("map: {0} surfaces use prototype grid materials".format(count))


def main():
    log("Stage 6 bootstrap starting")
    music, effects = make_mix()
    import_audio(music, effects)
    make_fx_material("M_CS_FXAdditive", unreal.BlendMode.BLEND_ADDITIVE)
    make_fx_material("M_CS_FXFlash", unreal.BlendMode.BLEND_ADDITIVE, radial=True)
    make_fx_material("M_CS_FXTranslucent", unreal.BlendMode.BLEND_TRANSLUCENT, fresnel_soft=True)
    make_bullet_hole()
    dress_weapons()
    dress_items()
    dress_map()
    EAL.save_directory("/Game", only_if_is_dirty=True, recursive=True)
    log("done")


try:
    main()
finally:
    flush_log()
