"""
v2.0 phase 4: replacement weapon models from Fab, turned into the project's
weapon convention (barrel along +X, top along +Z, as the Quaternius models
and Weapons/CSWeaponPresentation.h expect).

Sources (Fab Standard License - usable in the game, NOT in this public
repository; each owner adds them locally, see docs/ASSETS.md):
  * FPS Weapon Bundle (Deadghost Interactive, Epic Permanent Collection):
    copy its Content/FPS_Weapon_Bundle folder into this project's Content.

Output: /Game/Weapons/Fab/SM_<Name> (git-ignored, like the source pack).
Materials are the pack's own. Idempotent: the meshes are rewritten each run.

    UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script=Scripts/import_fab_weapons.py
"""

import os
import unreal

EAL = unreal.EditorAssetLibrary
GS_ASSET = unreal.GeometryScript_AssetUtils
GS_XFORM = unreal.GeometryScript_MeshTransforms

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
LOG_PATH = os.path.join(PROJECT_DIR, "Saved", "Logs", "import_fab_weapons.txt")
OUT_DIR = "/Game/Weapons/Fab"
BUNDLE = "/Game/FPS_Weapon_Bundle/Weapons/Meshes"

# output name, source mesh, rotations (applied in order) that put the barrel on
# +X and the top on +Z. Python Rotator(roll, pitch, yaw).
MODELS = [
    ("AK47", BUNDLE + "/Ka47/SM_KA47", [unreal.Rotator(0.0, 0.0, -90.0)]),
    ("M4", BUNDLE + "/AR4/SM_AR4", [unreal.Rotator(0.0, 0.0, -90.0)]),
    # The stockless variant: the folded wire stock blocks the sight picture.
    ("SMG", BUNDLE + "/SMG11/SM_SMG11_Nostock_X", []),
    # The knife stands on its point along -Z with the blade's width on Y:
    # pitch the point onto +X, then roll the width onto Z.
    ("Knife", BUNDLE + "/M9_Knife/SM_M9_Knife", [unreal.Rotator(0.0, 90.0, 0.0), unreal.Rotator(90.0, 0.0, 0.0)]),
]

FAB_SRC = os.path.join(PROJECT_DIR, "SourceArt", "_fab")
IMPORT_DIR = OUT_DIR + "/Source"

# Single models downloaded from Fab as FBX (git-ignored SourceArt/_fab):
#   G-17 (DJHaski, Fab Standard), M870 (Wilbruh, Fab Standard),
#   AWP (HexmireLive, CC BY 4.0; tiling textures from ambientCG, CC0).
FBX_SOURCES = [
    ("Pistol", os.path.join("semi-auto-pistol-g-17", "source", "SemiAuto Pistol G-17.fbx")),
    ("Shotgun", os.path.join("pump_action_shotgun", "source", "Shotgun.fbx")),
    ("Sniper", os.path.join("AWP_Sniper_Rifle", "AWP_Sniper_Rifle.fbx")),
]

# Turns for the imported FBX models (same convention as MODELS).
FBX_TURNS = [
    ("Pistol", []),
    ("Shotgun", [unreal.Rotator(0.0, 0.0, 90.0)]),
    ("Sniper", [unreal.Rotator(0.0, 0.0, 90.0)]),
]

_log = []


MEL = unreal.MaterialEditingLibrary
MAT_DIR = OUT_DIR + "/Materials"
TEX_DIR = OUT_DIR + "/Textures"
MASTER = MAT_DIR + "/M_FabWeapon"

# Texture sets per imported model: slot name -> (folder, base color, normal,
# roughness, metalness or None, metallic when there is no map, tiling).
# AWP slots were matched to its parts from the section profiles: the stock is
# "Material", barrel and receiver "Material_002", the scope "Material_003",
# small fittings "Material_001".
MATERIALS = {
    "Pistol": {
        "aiStandardSurface1": ("semi-auto-pistol-g-17/textures", "G17_lambert10_BaseColor.png", "G17_lambert10_Normal.png",
                               "G17_lambert10_Roughness.png", "G17_lambert10_Metalness.png", 0.0, 1.0),
    },
    "Shotgun": {
        "sg_mat": ("pump_action_shotgun/textures", "Shotgun_Albedo.png", "Shotgun_Normal.png",
                   "Shotgun_Roughness.png", "Shotgun_Metalness.png", 0.0, 1.0),
    },
    "Sniper": {
        "Material": ("AWP_Sniper_Rifle/Wood_Textures", "Wood030_2K-PNG_Color.png", "Wood030_2K-PNG_NormalGL.png",
                     "Wood030_2K-PNG_Roughness.png", None, 0.0, 1.0),
        "Material_002": ("AWP_Sniper_Rifle/Metal_Textures", "Metal055A_2K-PNG_Color.png", "Metal055A_2K-PNG_NormalGL.png",
                         "Metal055A_2K-PNG_Roughness.png", "Metal055A_2K-PNG_Metalness.png", 1.0, 1.0, (0.09, 0.09, 0.1)),
        "Material_003": ("AWP_Sniper_Rifle/Metal_Textures", "Metal055A_2K-PNG_Color.png", "Metal055A_2K-PNG_NormalGL.png",
                         "Metal055A_2K-PNG_Roughness.png", "Metal055A_2K-PNG_Metalness.png", 1.0, 1.0, (0.09, 0.09, 0.1)),
        "Material_001": ("AWP_Sniper_Rifle/Porcelain_Textures", "Porcelain003_2K-PNG_Color.png", "Porcelain003_2K-PNG_NormalGL.png",
                         "Porcelain003_2K-PNG_Roughness.png", None, 0.0, 1.0),
    },
}


def import_texture(folder, file, kind):
    """kind: color / normal / mask. Returns the texture asset."""
    name = "T_" + os.path.splitext(file)[0].replace("-", "_").replace(" ", "_")
    dest = TEX_DIR + "/" + folder.split("/")[0]
    path = dest + "/" + name
    if not EAL.does_asset_exist(path):
        task = unreal.AssetImportTask()
        task.filename = os.path.join(FAB_SRC, folder.replace("/", os.sep), file)
        task.destination_path = dest
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.save = False
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = EAL.load_asset(path)
    if tex is None:
        log("MISSING texture " + file)
        return None
    if kind == "normal":
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
        # ambientCG ships OpenGL normals (green up); UE expects DirectX.
        tex.set_editor_property("flip_green_channel", "NormalGL" in file)
    elif kind == "mask":
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        tex.set_editor_property("srgb", False)
    EAL.save_loaded_asset(tex)
    return tex


def make_master():
    """PBR master: texture maps x a tint, UV tiling, metallic when there is no map."""
    if EAL.does_asset_exist(MASTER):
        mat = EAL.load_asset(MASTER)
        MEL.delete_all_material_expressions(mat)
    else:
        if not EAL.does_directory_exist(MAT_DIR):
            EAL.make_directory(MAT_DIR)
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_FabWeapon", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())

    def expr(cls, x, y):
        return MEL.create_material_expression(mat, cls, x, y)

    uv = expr(unreal.MaterialExpressionTextureCoordinate, -1100, 0)
    tiling = expr(unreal.MaterialExpressionScalarParameter, -1100, 150)
    tiling.set_editor_property("parameter_name", "Tiling")
    tiling.set_editor_property("default_value", 1.0)
    uvs = expr(unreal.MaterialExpressionMultiply, -900, 50)
    MEL.connect_material_expressions(uv, "", uvs, "A")
    MEL.connect_material_expressions(tiling, "", uvs, "B")

    def texture(param, default, sampler, y):
        t = expr(unreal.MaterialExpressionTextureSampleParameter2D, -650, y)
        t.set_editor_property("parameter_name", param)
        t.set_editor_property("texture", unreal.load_asset(default))
        t.set_editor_property("sampler_type", sampler)
        MEL.connect_material_expressions(uvs, "", t, "UVs")
        return t

    color = texture("BaseColor", "/Engine/EngineResources/WhiteSquareTexture", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -400)
    normal = texture("Normal", "/Engine/EngineMaterials/DefaultNormal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -100)
    # Mask samplers need mask textures as defaults; the pistol set provides them.
    default_rough = import_texture("semi-auto-pistol-g-17/textures", "G17_lambert10_Roughness.png", "mask")
    default_metal = import_texture("semi-auto-pistol-g-17/textures", "G17_lambert10_Metalness.png", "mask")
    rough = texture("Roughness", default_rough.get_path_name(), unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 200)
    metal = texture("Metalness", default_metal.get_path_name(), unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, 500)

    tint = expr(unreal.MaterialExpressionVectorParameter, -400, -550)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    base = expr(unreal.MaterialExpressionMultiply, -200, -400)
    MEL.connect_material_expressions(color, "RGB", base, "A")
    MEL.connect_material_expressions(tint, "", base, "B")
    MEL.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
    MEL.connect_material_property(rough, "R", unreal.MaterialProperty.MP_ROUGHNESS)

    # Metallic = map x UseMetalMap, never below MetallicFloor (sets without a map
    # switch the map off and give the value as the floor).
    floor = expr(unreal.MaterialExpressionScalarParameter, -400, 650)
    floor.set_editor_property("parameter_name", "MetallicFloor")
    floor.set_editor_property("default_value", 0.0)
    metallic = expr(unreal.MaterialExpressionMax, -200, 550)
    use_map = expr(unreal.MaterialExpressionScalarParameter, -400, 450)
    use_map.set_editor_property("parameter_name", "UseMetalMap")
    use_map.set_editor_property("default_value", 1.0)
    gated = expr(unreal.MaterialExpressionMultiply, -300, 500)
    MEL.connect_material_expressions(metal, "R", gated, "A")
    MEL.connect_material_expressions(use_map, "", gated, "B")
    MEL.connect_material_expressions(gated, "", metallic, "A")
    MEL.connect_material_expressions(floor, "", metallic, "B")
    MEL.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


def make_instance(model, slot, spec, master):
    folder, color, normal, rough, metal, metallic_floor, tiling = spec[:7]
    # Optional 8th entry: a tint on the base colour (the AWP's bare steel texture
    # reads as chrome; the rifle is blued/black).
    tint = spec[7] if len(spec) > 7 else (1.0, 1.0, 1.0)
    name = "MI_%s_%s" % (model, slot)
    path = MAT_DIR + "/" + name
    if EAL.does_asset_exist(path):
        mi = EAL.load_asset(path)
    else:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, master)
    MEL.set_material_instance_texture_parameter_value(mi, "BaseColor", import_texture(folder, color, "color"))
    MEL.set_material_instance_texture_parameter_value(mi, "Normal", import_texture(folder, normal, "normal"))
    MEL.set_material_instance_texture_parameter_value(mi, "Roughness", import_texture(folder, rough, "mask"))
    if metal:
        MEL.set_material_instance_texture_parameter_value(mi, "Metalness", import_texture(folder, metal, "mask"))
    MEL.set_material_instance_scalar_parameter_value(mi, "UseMetalMap", 1.0 if metal else 0.0)
    MEL.set_material_instance_scalar_parameter_value(mi, "MetallicFloor", metallic_floor)
    MEL.set_material_instance_scalar_parameter_value(mi, "Tiling", tiling)
    MEL.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
    EAL.save_loaded_asset(mi)
    return mi


def apply_materials(model, master):
    """Puts the model's material instances on the imported source mesh's slots."""
    src = EAL.load_asset(IMPORT_DIR + "/SM_Src_" + model)
    if src is None:
        return
    specs = MATERIALS.get(model, {})
    for i, slot in enumerate(src.get_editor_property("static_materials")):
        slot_name = str(slot.get_editor_property("material_slot_name"))
        if slot_name in specs:
            src.set_material(i, make_instance(model, slot_name, specs[slot_name], master))
        else:
            log("  %s: no texture set for slot %s" % (model, slot_name))
    EAL.save_loaded_asset(src)


def import_fbx(name, relative):
    """Imports one FBX as a single static mesh, without its materials."""
    path = os.path.join(FAB_SRC, relative)
    if not os.path.exists(path):
        log("MISSING " + path)
        return None
    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_as_skeletal", False)
    options.set_editor_property("import_animations", False)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    options.static_mesh_import_data.set_editor_property("combine_meshes", True)
    options.static_mesh_import_data.set_editor_property("generate_lightmap_u_vs", False)
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = IMPORT_DIR
    task.destination_name = "SM_Src_" + name
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = options
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    mesh = EAL.load_asset(IMPORT_DIR + "/SM_Src_" + name)
    if mesh:
        box = mesh.get_bounding_box()
        slots = [str(m.get_editor_property("material_slot_name")) for m in mesh.get_editor_property("static_materials")]
        log("imported %s: x[%.1f..%.1f] y[%.1f..%.1f] z[%.1f..%.1f], slots %s" % (
            name, box.min.x, box.max.x, box.min.y, box.max.y, box.min.z, box.max.z, slots))
    return mesh


def log(msg):
    _log.append(str(msg))
    unreal.log("[CS-FabWeapons] " + str(msg))


def convert(name, src_path, rotations):
    src = EAL.load_asset(src_path)
    if src is None:
        log("MISSING " + src_path + " (is Content/FPS_Weapon_Bundle copied in?)")
        return None
    mesh = unreal.DynamicMesh()
    read = unreal.GeometryScriptCopyMeshFromAssetOptions()
    lod = unreal.GeometryScriptMeshReadLOD()
    GS_ASSET.copy_mesh_from_static_mesh(src, mesh, read, lod)
    for rotation in rotations:
        GS_XFORM.transform_mesh(mesh, unreal.Transform(unreal.Vector(0, 0, 0), rotation, unreal.Vector(1, 1, 1)))

    materials = [m.get_editor_property("material_interface") for m in src.get_editor_property("static_materials")]
    path = OUT_DIR + "/SM_" + name
    if not EAL.does_directory_exist(OUT_DIR):
        EAL.make_directory(OUT_DIR)
    if EAL.does_asset_exist(path):
        dst = EAL.load_asset(path)
    else:
        opts = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        opts.set_editor_property("enable_recompute_normals", False)
        opts.set_editor_property("enable_collision", False)
        result = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(mesh, path, opts)
        dst = result[0] if isinstance(result, (tuple, list)) else result
    write = unreal.GeometryScriptCopyMeshToAssetOptions()
    write.set_editor_property("replace_materials", True)
    write.set_editor_property("new_materials", materials)
    write.set_editor_property("enable_recompute_normals", False)
    write.set_editor_property("enable_recompute_tangents", False)
    GS_ASSET.copy_mesh_to_static_mesh(mesh, dst, write, unreal.GeometryScriptMeshWriteLOD())
    for i, m in enumerate(materials):
        dst.set_material(i, m)
    EAL.save_loaded_asset(dst)
    box = dst.get_bounding_box()
    log("%s <- %s: x[%.0f..%.0f] y[%.0f..%.0f] z[%.0f..%.0f], %d material(s)" % (
        path, src_path, box.min.x, box.max.x, box.min.y, box.max.y, box.min.z, box.max.z, len(materials)))
    return dst


def main():
    try:
        for name, src, rots in MODELS:
            convert(name, src, rots)
        for name, relative in FBX_SOURCES:
            import_fbx(name, relative)
        master = make_master()
        for name, _ in FBX_SOURCES:
            apply_materials(name, master)
        for name, rots in FBX_TURNS:
            convert(name, IMPORT_DIR + "/SM_Src_" + name, rots)
        log("done")
    finally:
        os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
        with open(LOG_PATH, "w") as f:
            f.write("\n".join(_log))


main()
