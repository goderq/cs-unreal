"""
v1.0 weapon models: imports the Quaternius "Ultimate Gun Pack" meshes (CC0,
see docs/ASSETS.md) from SourceArt/Weapons/Quaternius as static meshes into
/Game/Weapons/Quaternius, one distinct model per weapon, and logs each mesh's
bounds so the grip / sight offsets in Config/DefaultGame.ini can be checked.

    UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script=Scripts/import_weapons.py

Idempotent: re-importing replaces the assets in place.
"""

import os
import unreal

EAL = unreal.EditorAssetLibrary
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJECT_DIR, "SourceArt", "Weapons", "Quaternius")
DEST = "/Game/Weapons/Quaternius"
LOG_PATH = os.path.join(PROJECT_DIR, "Saved", "Logs", "import_weapons.txt")

# The models the game uses. The others in SourceArt stay available for swaps.
MODELS = [
    "Pistol_1",
    "AssaultRifle_3",
    "AssaultRifle_4",
    "AssaultRifle_5",
    "AssaultRifle_2",
    "AssaultRifle2_1",
    "SubmachineGun_1",
    "Shotgun_2",
    "SniperRifle_1",
    "SniperRifle_4",
]

_lines = []


def log(msg):
    _lines.append(str(msg))
    unreal.log("[CS-Weapons] " + str(msg))


def import_model(name):
    task = unreal.AssetImportTask()
    task.filename = os.path.join(SRC, name + ".fbx")
    task.destination_path = DEST + "/" + name
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return list(task.imported_object_paths)


def describe(folder):
    for path in EAL.list_assets(folder, recursive=True, include_folder=False):
        asset = EAL.load_asset(path)
        if isinstance(asset, unreal.StaticMesh):
            box = asset.get_bounding_box()
            size = box.max - box.min
            mats = [m.material_interface.get_name() if m.material_interface else "None"
                    for m in asset.static_materials]
            log("MESH %s min=(%.1f,%.1f,%.1f) max=(%.1f,%.1f,%.1f) size=(%.1f,%.1f,%.1f) mats=%d %s" % (
                path, box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z,
                size.x, size.y, size.z, len(mats), mats))
        else:
            log("ASSET %s (%s)" % (path, type(asset).__name__))


# Paint for the pack's material slots, by slot name. The imported materials
# use the engine's legacy FBX Phong parent, which renders these colours far
# too dark (and lives in an engine plugin); a small PBR material of our own
# replaces it. Colours follow the pack's preview image; sRGB 0-255.
PALETTE = {
    #            colour            metallic roughness
    "Wood":      ((178, 112, 66),  0.0, 0.65),
    "DarkWood":  ((104, 64, 40),   0.0, 0.7),
    "Metal":     ((152, 152, 160), 0.8, 0.38),
    "LightMetal": ((188, 188, 196), 0.85, 0.32),
    "DarkMetal": ((72, 74, 80),    0.7, 0.42),
    "Black":     ((30, 30, 33),    0.2, 0.6),
    "Black2":    ((44, 44, 48),    0.2, 0.55),
    "Grey":      ((118, 118, 114), 0.1, 0.7),
    "Green":     ((98, 112, 74),   0.0, 0.72),
    "Glass":     ((36, 52, 70),    0.3, 0.08),
    "Main":      ((46, 47, 50),    0.3, 0.55),
    "MainDark":  ((26, 26, 28),    0.3, 0.6),
    "MainLight": ((84, 84, 88),    0.4, 0.5),
}
MAT_DIR = "/Game/Weapons/Materials"
MEL = unreal.MaterialEditingLibrary


def srgb_to_linear(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def make_paint_materials():
    if not EAL.does_directory_exist(MAT_DIR):
        EAL.make_directory(MAT_DIR)
    base_path = MAT_DIR + "/M_WeaponPaint"
    if EAL.does_asset_exist(base_path):
        base = EAL.load_asset(base_path)
    else:
        base = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_WeaponPaint", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
        color = MEL.create_material_expression(base, unreal.MaterialExpressionVectorParameter, -500, -200)
        color.set_editor_property("parameter_name", "Color")
        color.set_editor_property("default_value", unreal.LinearColor(0.2, 0.2, 0.2, 1))
        metal = MEL.create_material_expression(base, unreal.MaterialExpressionScalarParameter, -500, 0)
        metal.set_editor_property("parameter_name", "Metallic")
        rough = MEL.create_material_expression(base, unreal.MaterialExpressionScalarParameter, -500, 150)
        rough.set_editor_property("parameter_name", "Roughness")
        rough.set_editor_property("default_value", 0.5)
        MEL.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
        MEL.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
        MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
        MEL.recompile_material(base)
        EAL.save_loaded_asset(base)

    paints = {}
    for name, (rgb, metallic, roughness) in PALETTE.items():
        path = MAT_DIR + "/MI_Paint_" + name
        if EAL.does_asset_exist(path):
            mi = EAL.load_asset(path)
        else:
            mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                "MI_Paint_" + name, MAT_DIR, unreal.MaterialInstanceConstant,
                unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, base)
        MEL.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(
            srgb_to_linear(rgb[0]), srgb_to_linear(rgb[1]), srgb_to_linear(rgb[2]), 1.0))
        MEL.set_material_instance_scalar_parameter_value(mi, "Metallic", metallic)
        MEL.set_material_instance_scalar_parameter_value(mi, "Roughness", roughness)
        EAL.save_loaded_asset(mi)
        paints[name] = mi
    return paints


def repaint(folder, paints):
    for path in EAL.list_assets(folder, recursive=True, include_folder=False):
        mesh = EAL.load_asset(path)
        if not isinstance(mesh, unreal.StaticMesh):
            continue
        for i, slot in enumerate(mesh.static_materials):
            name = str(slot.material_slot_name)
            paint = paints.get(name, paints["Grey"])
            mesh.set_material(i, paint)
            log("  %s slot %d %s -> %s" % (mesh.get_name(), i, name, paint.get_name()))
        EAL.save_loaded_asset(mesh)


def main():
    for name in MODELS:
        paths = import_model(name)
        log("imported %s -> %d object(s)" % (name, len(paths)))
        describe(DEST + "/" + name)
    paints = make_paint_materials()
    repaint(DEST, paints)
    # The importer's own material instances are now unused; they point at an
    # engine-plugin parent, so remove them rather than cook them.
    for path in EAL.list_assets(DEST, recursive=True, include_folder=False):
        if isinstance(EAL.load_asset(path), unreal.MaterialInstanceConstant):
            EAL.delete_asset(path)
            log("  removed imported material " + path)
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
    with open(LOG_PATH, "w") as f:
        f.write("\n".join(_lines))


main()
