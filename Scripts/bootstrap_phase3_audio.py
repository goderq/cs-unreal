"""
v2.0 phase 3 (AUDIT C9): sound class tree, attenuation and concurrency per
kind of sound, surface footsteps and the physical materials they key on.

Run AFTER bootstrap_stage6.py and Scripts/generate_audio.py, with the physical
surfaces from Config/DefaultEngine.ini already in place:

    UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script=Scripts/bootstrap_phase3_audio.py

Idempotent: existing assets are updated in place, never duplicated. Sounds that
are already imported are not re-imported (their samples do not change).

Sound classes (volumes multiply down the tree):

    SC_Master
      SC_Music                   <- "music" slider
      SC_Effects                 <- "effects" slider
        SC_Weapons  SC_Footsteps  SC_World  SC_UI

The "master" slider sets the audio device's primary volume (CSSettingsSubsystem).
"""

import os
import unreal

ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
AUDIO_SRC = os.path.join(PROJECT_DIR, "SourceArt", "Audio")
LOG_PATH = os.path.join(PROJECT_DIR, "Saved", "Logs", "bootstrap_phase3_audio.txt")

AUDIO_DIR = "/Game/Audio"
MIX_DIR = AUDIO_DIR + "/Mix"
PHYS_DIR = "/Game/Environment/Physics"
SURF = "/Game/Environment/Surfaces/MI_"
PROPS = "/Game/Environment/Props/"

# Must match [/Script/Engine.PhysicsSettings] in DefaultEngine.ini and
# CSSurface in Source/CSFusion/Audio/CSAudio.h.
SURFACES = {
    "Metal": unreal.PhysicalSurface.SURFACE_TYPE1,
    "Wood": unreal.PhysicalSurface.SURFACE_TYPE2,
    "Dirt": unreal.PhysicalSurface.SURFACE_TYPE3,
}

# Materials the maps use, by what they sound like underfoot and under fire.
# Everything else (concrete, brick, plaster, asphalt, tiles) stays the default
# surface, which plays the stone footsteps.
SURFACE_MATERIALS = {
    "Metal": [SURF + "corrugated_iron", SURF + "metal_plate", SURF + "rusty_painted_metal",
              PROPS + "Barrel_01/MI_Prop_Barrel_01", PROPS + "utility_box_02/MI_Prop_utility_box_02"],
    "Wood": [SURF + "weathered_planks",
             PROPS + "wooden_crate_01/MI_Prop_wooden_crate_01", PROPS + "wooden_crate_02/MI_Prop_wooden_crate_02",
             PROPS + "old_military_crate/MI_Prop_old_military_crate",
             PROPS + "painted_wooden_bench/MI_Prop_painted_wooden_bench",
             PROPS + "wine_barrel_01/MI_Prop_wine_barrel_01", PROPS + "planter_box_01/MI_Prop_planter_box_01"],
    "Dirt": [],  # no dirt ground on the current maps yet (phase 4 maps)
}

_log_lines = []


def log(msg):
    line = "[CS-Phase3Audio] " + str(msg)
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
        return EAL.load_asset(path)
    ensure_dir(folder)
    return ASSET_TOOLS.create_asset(name, folder, cls, factory)


def try_set(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as e:
        log("  cannot set {0}.{1}: {2}".format(obj.get_name(), prop, e))
        return False


# ---------------------------------------------------------------------------
# Sound classes
# ---------------------------------------------------------------------------

def sound_class(name):
    return get_or_create(name, MIX_DIR, unreal.SoundClass, unreal.SoundClassFactory())


def attach(parent, children):
    for child in children:
        try_set(child, "parent_class", parent)
    try_set(parent, "child_classes", children)


def make_classes():
    master = sound_class("SC_Master")
    music = sound_class("SC_Music")
    effects = sound_class("SC_Effects")
    groups = {name: sound_class("SC_" + name) for name in ("Weapons", "Footsteps", "World", "UI")}
    attach(master, [music, effects])
    attach(effects, [groups["Weapons"], groups["Footsteps"], groups["World"], groups["UI"]])
    # UI clicks sit a little under gameplay; footsteps stay at full level (they
    # are information, not decoration).
    props = groups["UI"].get_editor_property("properties")
    props.set_editor_property("volume", 0.8)
    groups["UI"].set_editor_property("properties", props)
    for c in [master, music, effects] + list(groups.values()):
        EAL.save_loaded_asset(c)
    log("classes: SC_Master > SC_Music, SC_Effects > SC_Weapons, SC_Footsteps, SC_World, SC_UI")
    return music, groups


def class_for(path, music, groups):
    name = path.rsplit("/", 1)[-1]
    if "/Music/" in path:
        return music
    if "/UI/" in path or "/Feedback/" in path:
        return groups["UI"]
    if "/Player/" in path:
        return groups["Footsteps"] if name.startswith("S_Footstep") or name.startswith("S_Land") else groups["World"]
    if "/Weapons/" in path:  # /Game/Audio/Weapons and /Game/Weapons/...
        return groups["Weapons"]
    return groups["World"]


# ---------------------------------------------------------------------------
# Import of new sounds and class assignment
# ---------------------------------------------------------------------------

def import_new_sounds():
    tasks = []
    for category in sorted(os.listdir(AUDIO_SRC)):
        folder = os.path.join(AUDIO_SRC, category)
        if not os.path.isdir(folder):
            continue
        for file in sorted(os.listdir(folder)):
            name, ext = os.path.splitext(file)
            dest = AUDIO_DIR + "/" + category
            if ext.lower() != ".wav" or EAL.does_asset_exist(dest + "/" + name):
                continue
            task = unreal.AssetImportTask()
            task.set_editor_property("filename", os.path.join(folder, file))
            task.set_editor_property("destination_path", dest)
            task.set_editor_property("destination_name", name)
            task.set_editor_property("replace_existing", False)
            task.set_editor_property("automated", True)
            task.set_editor_property("save", False)
            tasks.append(task)
    if tasks:
        ASSET_TOOLS.import_asset_tasks(tasks)
    log("audio: imported {0} new sound waves".format(len(tasks)))


def assign_classes(music, groups):
    counts = {}
    for root in (AUDIO_DIR, "/Game/Weapons"):
        for path in EAL.list_assets(root, recursive=True, include_folder=False):
            if "/Mix/" in path:
                continue
            data = EAL.find_asset_data(path)
            if not data.is_valid() or str(data.asset_class_path.asset_name) not in ("SoundWave", "SoundCue"):
                continue
            sound = EAL.load_asset(path)
            target = class_for(path, music, groups)
            if sound.get_editor_property("sound_class_object") != target:
                try_set(sound, "sound_class_object", target)
                EAL.save_loaded_asset(sound)
            counts[target.get_name()] = counts.get(target.get_name(), 0) + 1
    log("classes assigned: " + ", ".join("{0}={1}".format(k, v) for k, v in sorted(counts.items())))


# ---------------------------------------------------------------------------
# Attenuation and concurrency
# ---------------------------------------------------------------------------

def make_attenuation(name, inner, falloff, occlusion_lpf, occlusion_volume, air_lpf):
    att = get_or_create(name, MIX_DIR, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
    s = att.get_editor_property("attenuation")
    try_set(s, "attenuate", True)
    try_set(s, "spatialize", True)
    try_set(s, "distance_algorithm", unreal.AttenuationDistanceModel.NATURAL_SOUND)
    try_set(s, "attenuation_shape_extents", unreal.Vector(inner, 0.0, 0.0))
    try_set(s, "falloff_distance", falloff)
    # Behind a wall: duller and quieter. The engine traces from the listener to
    # each playing sound (async, on the AudioOcclusion channel that characters
    # ignore, simple collision).
    try_set(s, "enable_occlusion", True)
    try_set(s, "occlusion_trace_channel", unreal.CollisionChannel.ECC_AUDIO_OCCLUSION)  # ECC_GameTraceChannel1
    try_set(s, "occlusion_low_pass_filter_frequency", occlusion_lpf)
    try_set(s, "occlusion_volume_attenuation", occlusion_volume)
    try_set(s, "occlusion_interpolation_time", 0.1)
    try_set(s, "use_complex_collision_for_occlusion", False)
    # Air: distant sounds lose their top end.
    try_set(s, "attenuate_with_lpf", True)
    try_set(s, "lpf_radius_min", inner)
    try_set(s, "lpf_radius_max", inner + falloff)
    try_set(s, "lpf_frequency_at_min", 20000.0)
    try_set(s, "lpf_frequency_at_max", air_lpf)
    att.set_editor_property("attenuation", s)
    EAL.save_loaded_asset(att)
    return att


def make_concurrency(name, max_count, rule, retrigger=0.0):
    con = get_or_create(name, MIX_DIR, unreal.SoundConcurrency, unreal.SoundConcurrencyFactory())
    s = con.get_editor_property("concurrency")
    try_set(s, "max_count", max_count)
    try_set(s, "limit_to_owner", False)
    try_set(s, "resolution_rule", rule)
    try_set(s, "retrigger_time", retrigger)
    con.set_editor_property("concurrency", s)
    EAL.save_loaded_asset(con)
    return con


def make_mixing():
    # name, inner radius, falloff, occluded LPF Hz, occluded volume, LPF at max distance
    make_attenuation("ATT_Weapon", 600.0, 9000.0, 1200.0, 0.45, 2500.0)
    make_attenuation("ATT_Footstep", 250.0, 2300.0, 900.0, 0.35, 3000.0)
    make_attenuation("ATT_Impact", 200.0, 2800.0, 1500.0, 0.4, 3500.0)
    make_attenuation("ATT_Explosion", 1000.0, 12000.0, 800.0, 0.5, 1500.0)
    rules = unreal.MaxConcurrentResolutionRule
    make_concurrency("SCON_Weapon", 16, rules.STOP_QUIETEST)
    make_concurrency("SCON_Footstep", 12, rules.STOP_FARTHEST_THEN_OLDEST)
    make_concurrency("SCON_Impact", 10, rules.STOP_OLDEST, 0.02)
    make_concurrency("SCON_Explosion", 4, rules.STOP_OLDEST)
    log("mixing: ATT_Weapon/Footstep/Impact/Explosion (occlusion on), SCON_Weapon/Footstep/Impact/Explosion")


# ---------------------------------------------------------------------------
# Physical materials
# ---------------------------------------------------------------------------

def make_surfaces():
    for name, surface in SURFACES.items():
        pm = get_or_create("PM_" + name, PHYS_DIR, unreal.PhysicalMaterial, unreal.PhysicalMaterialFactoryNew())
        try_set(pm, "surface_type", surface)
        EAL.save_loaded_asset(pm)
        for path in SURFACE_MATERIALS[name]:
            mi = EAL.load_asset(path)
            if mi is None:
                log("  MISSING material " + path)
                continue
            # Without the override flag an instance returns its parent's
            # physical material and PhysMaterial is ignored.
            if mi.get_editor_property("phys_material") != pm or not mi.get_editor_property("override_phys_material"):
                try_set(mi, "override_phys_material", True)
                try_set(mi, "phys_material", pm)
                EAL.save_loaded_asset(mi)
        log("surface {0}: PM_{0}, {1} materials".format(name, len(SURFACE_MATERIALS[name])))


def main():
    try:
        music, groups = make_classes()
        import_new_sounds()
        assign_classes(music, groups)
        make_mixing()
        make_surfaces()
        log("done")
    finally:
        flush_log()


main()
