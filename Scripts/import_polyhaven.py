"""
v1.1 map assets: imports the Poly Haven textures and props downloaded by
Scripts/download_polyhaven.py (all CC0, see docs/ASSETS.md) and builds the
materials the v1.1 maps use.

    UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script=Scripts/import_polyhaven.py

Creates under /Game/Environment:
  Materials/M_EnvTriplanar   world-aligned PBR for walls and floors: textures
                             are projected along the three world axes, so a
                             cube scaled to any size is never stretched
  Materials/M_PropPBR        ordinary UV-mapped PBR for the props
  Surfaces/MI_<texture>      one instance per Poly Haven texture set
  Props/<model>/SM_...       props, each with its own MI_Prop_<model>
Idempotent: existing assets are updated in place.
"""

import os
import unreal

ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJECT_DIR, "SourceArt", "Maps", "PolyHaven")
LOG_PATH = os.path.join(PROJECT_DIR, "Saved", "Logs", "import_polyhaven.txt")

ENV = "/Game/Environment"
MAT_DIR = ENV + "/Materials"
SURF_DIR = ENV + "/Surfaces"
TEX_DIR = ENV + "/Textures"
PROP_DIR = ENV + "/Props"

# texture set: world size of one repeat in cm
SURFACES = {
    "concrete_floor_worn_001": 300, "asphalt_02": 400, "corrugated_iron": 250, "metal_plate": 200,
    "rusty_painted_metal": 250, "painted_concrete": 300, "red_brick_03": 220, "concrete_wall_003": 300,
    "cobblestone_floor_08": 250, "plastered_wall_02": 300, "white_plaster_02": 300, "castle_brick_02_red": 250,
    "clay_roof_tiles_02": 250, "weathered_planks": 200, "stone_tiles_02": 200, "yellow_plaster": 300,
}
PROPS = ["Barrel_01", "wooden_crate_01", "wooden_crate_02", "concrete_road_barrier", "cardboard_box_01",
         "utility_box_02", "old_military_crate", "wine_barrel_01", "planter_box_01", "painted_wooden_bench"]

_log = []


def log(msg):
    line = "[CS-PolyHaven] " + str(msg)
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


# ---------------------------------------------------------------------------
# Import
# ---------------------------------------------------------------------------

def import_files(files, dest):
    """files: list of (path, asset name). Returns {name: asset}."""
    ensure_dir(dest)
    tasks = []
    for path, name in files:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", path)
        task.set_editor_property("destination_path", dest)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", False)
        tasks.append(task)
    ASSET_TOOLS.import_asset_tasks(tasks)
    out = {}
    for path, name in files:
        asset = EAL.load_asset(dest + "/" + name)
        if asset:
            out[name] = asset
        else:
            log("  import FAILED: " + path)
    return out


def setup_texture(tex, kind):
    """kind: color | normal | mask"""
    if kind == "normal":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        # Poly Haven ships OpenGL normals (Y up); Unreal expects DirectX (Y down).
        tex.set_editor_property("flip_green_channel", True)
    elif kind == "mask":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    else:
        tex.set_editor_property("srgb", True)
    EAL.save_loaded_asset(tex)


def find(folder, *needles):
    for f in sorted(os.listdir(folder)):
        low = f.lower()
        if all(n in low for n in needles):
            return os.path.join(folder, f)
    return None


# ---------------------------------------------------------------------------
# Material graph helpers
# ---------------------------------------------------------------------------

def node(mat, cls, x, y, **props):
    n = MEL.create_material_expression(mat, cls, x, y)
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def link(a, b, pin="", out=""):
    MEL.connect_material_expressions(a, out, b, pin)


def mask(mat, src, x, y, r=False, g=False, b=False, a=False):
    m = node(mat, unreal.MaterialExpressionComponentMask, x, y, r=r, g=g, b=b, a=a)
    link(src, m)
    return m


def mul(mat, a, b, x, y):
    m = node(mat, unreal.MaterialExpressionMultiply, x, y)
    link(a, m, "A")
    link(b, m, "B")
    return m


def add(mat, a, b, x, y):
    m = node(mat, unreal.MaterialExpressionAdd, x, y)
    link(a, m, "A")
    link(b, m, "B")
    return m


def tex_param(mat, name, uv, x, y, sampler, default_path):
    t = node(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=name, sampler_type=sampler)
    # Engine content is not in the commandlet's asset registry: load the object directly.
    default = unreal.load_object(None, default_path + "." + default_path.split("/")[-1])
    if default:
        t.set_editor_property("texture", default)
    if uv is not None:
        link(uv, t, "UVs")
    return t


# Defaults for the texture parameters must match the sampler type (colour /
# normal map / masks) or the material fails to compile and the Default
# Material is used in game - so they are our own imported textures of each kind.
DEF_COLOR = TEX_DIR + "/asphalt_02/T_asphalt_02_D"
DEF_NORMAL = TEX_DIR + "/asphalt_02/T_asphalt_02_N"
DEF_MASK = TEX_DIR + "/asphalt_02/T_asphalt_02_ARM"


def make_triplanar():
    mat, _ = get_or_create("M_EnvTriplanar", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)

    tile = node(mat, unreal.MaterialExpressionScalarParameter, -2400, 0, parameter_name="TileSize", default_value=300.0)
    tint = node(mat, unreal.MaterialExpressionVectorParameter, -600, -700, parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
    rough_scale = node(mat, unreal.MaterialExpressionScalarParameter, -600, 500, parameter_name="RoughnessScale", default_value=1.0)

    world = node(mat, unreal.MaterialExpressionWorldPosition, -2400, -300)
    scaled = node(mat, unreal.MaterialExpressionDivide, -2200, -300)
    link(world, scaled, "A")
    link(tile, scaled, "B")
    uv_yz = mask(mat, scaled, -2000, -500, g=True, b=True)
    uv_xz = mask(mat, scaled, -2000, -300, r=True, b=True)
    uv_xy = mask(mat, scaled, -2000, -100, r=True, g=True)

    # Blend weights from the world normal: |N|^4, normalised.
    normal = node(mat, unreal.MaterialExpressionVertexNormalWS, -2400, 400)
    absn = node(mat, unreal.MaterialExpressionAbs, -2200, 400)
    link(normal, absn)
    sq = mul(mat, absn, absn, -2050, 400)
    p4 = mul(mat, sq, sq, -1900, 400)
    ones = node(mat, unreal.MaterialExpressionConstant3Vector, -1900, 550, constant=unreal.LinearColor(1, 1, 1, 1))
    total = node(mat, unreal.MaterialExpressionDotProduct, -1750, 500)
    link(p4, total, "A")
    link(ones, total, "B")
    weights = node(mat, unreal.MaterialExpressionDivide, -1600, 420)
    link(p4, weights, "A")
    link(total, weights, "B")
    wx = mask(mat, weights, -1450, 300, r=True)
    wy = mask(mat, weights, -1450, 400, g=True)
    wz = mask(mat, weights, -1450, 500, b=True)

    def triplanar(name, sampler, default, y, channels=None):
        s = [tex_param(mat, name, uv, -1700, y + i * 180, sampler, default) for i, uv in enumerate((uv_yz, uv_xz, uv_xy))]
        parts = []
        for smp, w, i in zip(s, (wx, wy, wz), range(3)):
            src = smp
            if channels:
                src = mask(mat, smp, -1450, y + i * 180, **channels)
            parts.append(mul(mat, src, w, -1250, y + i * 180))
        return add(mat, add(mat, parts[0], parts[1], -1050, y + 60), parts[2], -900, y + 120)

    color = triplanar("Diffuse", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, DEF_COLOR, -900)
    nrm = triplanar("Normal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, DEF_NORMAL, -250, dict(r=True, g=True, b=True))
    arm = triplanar("ARM", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, DEF_MASK, 400, dict(r=True, g=True, b=True))

    tinted = mul(mat, color, tint, -400, -700)
    MEL.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(nrm, "", unreal.MaterialProperty.MP_NORMAL)
    ao = mask(mat, arm, -600, 300, r=True)
    rough = mul(mat, mask(mat, arm, -600, 400, g=True), rough_scale, -400, 400)
    metal = mask(mat, arm, -600, 600, b=True)
    MEL.connect_material_property(ao, "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log("material: M_EnvTriplanar")
    return mat


def make_prop_material():
    mat, _ = get_or_create("M_PropPBR", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    color = tex_param(mat, "Diffuse", None, -700, -300, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, DEF_COLOR)
    nrm = tex_param(mat, "Normal", None, -700, 0, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, DEF_NORMAL)
    rough = tex_param(mat, "Roughness", None, -700, 300, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, DEF_MASK)
    metal = tex_param(mat, "Metallic", None, -700, 600, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, DEF_MASK)
    metal_scale = node(mat, unreal.MaterialExpressionScalarParameter, -700, 850, parameter_name="MetallicScale", default_value=1.0)
    MEL.connect_material_property(color, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(nrm, "RGB", unreal.MaterialProperty.MP_NORMAL)
    MEL.connect_material_property(rough, "R", unreal.MaterialProperty.MP_ROUGHNESS)
    metal_r = mask(mat, metal, -450, 600, r=True)
    MEL.connect_material_property(mul(mat, metal_r, metal_scale, -300, 650), "", unreal.MaterialProperty.MP_METALLIC)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log("material: M_PropPBR")
    return mat


def instance(name, folder, parent, textures, scalars=None):
    mi, _ = get_or_create(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, parent)
    for param, tex in textures.items():
        if tex:
            MEL.set_material_instance_texture_parameter_value(mi, param, tex)
    for param, value in (scalars or {}).items():
        MEL.set_material_instance_scalar_parameter_value(mi, param, value)
    EAL.save_loaded_asset(mi)
    return mi


# ---------------------------------------------------------------------------
# Surfaces and props
# ---------------------------------------------------------------------------

def import_surface_textures():
    out = {}
    for tex_id, tile in SURFACES.items():
        folder = os.path.join(SRC, tex_id)
        if not os.path.isdir(folder):
            log("surface MISSING: " + tex_id)
            continue
        files = [(find(folder, "diffuse"), "T_%s_D" % tex_id), (find(folder, "nor_gl"), "T_%s_N" % tex_id), (find(folder, "arm"), "T_%s_ARM" % tex_id)]
        files = [(p, n) for p, n in files if p]
        imported = import_files(files, TEX_DIR + "/" + tex_id)
        d = imported.get("T_%s_D" % tex_id)
        n = imported.get("T_%s_N" % tex_id)
        a = imported.get("T_%s_ARM" % tex_id)
        if d: setup_texture(d, "color")
        if n: setup_texture(n, "normal")
        if a: setup_texture(a, "mask")
        out[tex_id] = (d, n, a)
    return out


def make_surface_instances(parent, textures):
    for tex_id, (d, n, a) in textures.items():
        tile = SURFACES[tex_id]
        instance("MI_" + tex_id, SURF_DIR, parent, {"Diffuse": d, "Normal": n, "ARM": a}, {"TileSize": float(tile)})
        log("surface: MI_%s (tile %d cm)" % (tex_id, tile))


def import_props(parent):
    for prop in PROPS:
        folder = os.path.join(SRC, prop)
        fbx = find(folder, ".fbx")
        texdir = os.path.join(folder, "textures")
        if not fbx:
            log("prop MISSING: " + prop)
            continue
        dest = PROP_DIR + "/" + prop

        task = unreal.AssetImportTask()
        task.set_editor_property("filename", fbx)
        task.set_editor_property("destination_path", dest)
        task.set_editor_property("destination_name", "SM_" + prop)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", False)
        options = unreal.FbxImportUI()
        options.set_editor_property("import_mesh", True)
        options.set_editor_property("import_materials", False)
        options.set_editor_property("import_textures", False)
        options.set_editor_property("import_as_skeletal", False)
        options.static_mesh_import_data.set_editor_property("combine_meshes", True)
        options.static_mesh_import_data.set_editor_property("auto_generate_collision", True)
        options.static_mesh_import_data.set_editor_property("generate_lightmap_u_vs", False)
        task.set_editor_property("options", options)
        ASSET_TOOLS.import_asset_tasks([task])

        mesh = None
        for path in task.get_editor_property("imported_object_paths"):
            asset = EAL.load_asset(path)
            if isinstance(asset, unreal.StaticMesh):
                mesh = asset
        if not mesh:
            log("prop import FAILED: " + prop)
            continue

        files = []
        for key, kind, needle in (("D", "color", "diff"), ("N", "normal", "nor_gl"), ("R", "mask", "rough"), ("M", "mask", "metal")):
            p = find(texdir, needle) if os.path.isdir(texdir) else None
            if p:
                files.append((p, "T_%s_%s" % (prop, key), kind))
        imported = import_files([(p, n) for p, n, _ in files], dest)
        for p, n, kind in files:
            if n in imported:
                setup_texture(imported[n], kind)
        mi = instance("MI_Prop_" + prop, dest, parent, {
            "Diffuse": imported.get("T_%s_D" % prop), "Normal": imported.get("T_%s_N" % prop),
            "Roughness": imported.get("T_%s_R" % prop), "Metallic": imported.get("T_%s_M" % prop)},
            {"MetallicScale": 1.0 if imported.get("T_%s_M" % prop) else 0.0})
        for i in range(len(mesh.static_materials)):
            mesh.set_material(i, mi)
        EAL.save_loaded_asset(mesh)
        box = mesh.get_bounding_box()
        log("prop: SM_%s size %.0f x %.0f x %.0f cm" % (prop, box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z))


def main():
    textures = import_surface_textures()
    tri = make_triplanar()
    pbr = make_prop_material()
    make_surface_instances(tri, textures)
    import_props(pbr)
    log("done")
    flush_log()


try:
    main()
except Exception as e:
    import traceback
    log("FAILED: %s\n%s" % (e, traceback.format_exc()))
    flush_log()
