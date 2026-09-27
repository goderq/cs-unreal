"""
v2.1 map rework: imports the Poly Haven assets (Scripts/maps/download_assets.py)
and the project's own generated textures (Scripts/maps/make_textures.py), and
builds the material system the new maps use (docs/MAPS_REWORK.md 2.2).

    UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script=Scripts/maps/import_assets.py

Everything goes under /Game/Environment/V21:
  Textures/<set>/T_<set>_D|N|ARM   surface textures (2K or 1K, as downloaded)
  Textures/Generated/...           decal atlas, trim sheet, signs, macro noise
  Materials/M_CS_Surface           UV-mapped PBR (kit meshes carry UVs in metres)
                                   + tint, roughness, normal strength, world-space
                                   macro variation and grime (vertex colour R)
  Materials/M_CS_Terrain           4 layers blended by vertex colour (R, G, B over
                                   the base), sharpened by the macro noise
  Materials/M_CS_Decal             deferred decal, one cell of T_DecalAtlas
  Materials/M_CS_Atlas             UV-mapped cell of an atlas (signs)
  Materials/M_CS_Glass             opaque glass: reflective, optional glow
  Materials/M_CS_Prop              PBR for the Poly Haven props (D/N/R/M/Alpha)
  Surfaces/MI_<name>               surface variants (Concrete_A, _Dirty, ...)
  Props/<model>/SM_*               props; Nanite above 2k triangles (opaque), 3 LODs (masked), simple
                                   collision, textures capped at 1K (small: 512)
Idempotent: assets are updated in place.
"""

import os
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC_PH = os.path.join(PROJECT, "SourceArt", "_download", "polyhaven_v21")
SRC_GEN = os.path.join(PROJECT, "SourceArt", "Maps", "Generated")
LOG_PATH = os.path.join(PROJECT, "Saved", "Logs", "import_assets_v21.txt")

ROOT = "/Game/Environment/V21"
TEX = ROOT + "/Textures"
MAT = ROOT + "/Materials"
SURF = ROOT + "/Surfaces"
PROPS = ROOT + "/Props"
PHYS = "/Game/Environment/Physics"

# Poly Haven texture sets: world size of one repeat (cm).
SETS = {
    "road_damaged": 400, "concrete_floor_damaged_01": 300, "gravel_ground_01": 250, "dry_ground_01": 300,
    "leafy_grass": 250, "factory_wall": 300, "container_side": 240, "patterned_cobblestone": 200,
    "pavement_03": 200, "worn_cracked_plaster": 300, "red_plaster_weathered": 300, "roof_tiles_14": 200,
    "concrete_floor_painted": 400, "asphalt_04": 400, "exterior_wall_cladding": 300, "blue_metal_plate": 200,
    "box_profile_metal_sheet": 200, "rusty_metal_grid": 150, "painted_metal_shutter": 200, "concrete_block_wall": 250,
    "blue_plaster_weathered": 300, "yellow_plaster_02": 300, "damaged_plaster": 300, "mixed_brick_wall": 250,
    "large_sandstone_blocks_01": 250, "park_dirt": 300, "brick_pavement": 200, "anti_slip_concrete": 250,
    "concrete_pavers_02": 200, "corrugated_iron_02": 200, "metal_grate_rusty": 150, "concrete_wall_006": 300,
    "grey_roof_tiles": 200, "sparse_grass": 250, "grassy_cobblestone": 200, "grass_concrete_pavement": 250,
    "bark_platanus": 150,
    # v1.1 sets still in use (imported by Scripts/import_polyhaven.py)
}
OLD_SETS = {"asphalt_02": 400, "corrugated_iron": 250, "metal_plate": 200, "rusty_painted_metal": 250,
            "painted_concrete": 300, "red_brick_03": 220, "concrete_wall_003": 300, "cobblestone_floor_08": 250,
            "white_plaster_02": 300, "clay_roof_tiles_02": 250, "weathered_planks": 200, "stone_tiles_02": 200,
            "castle_brick_02_red": 250, "concrete_floor_worn_001": 300}

# Surface variants: name -> (set, tint RGB, roughness scale, normal strength, dirt, physical material)
METAL, WOOD, DIRT = "PM_Metal", "PM_Wood", "PM_Dirt"
VARIANTS = {
    # Concrete and asphalt
    "Concrete_A": ("concrete_floor_damaged_01", (1.0, 1.0, 1.0), 1.0, 1.0, 0.25, None),
    "Concrete_B": ("concrete_floor_worn_001", (0.95, 0.93, 0.9), 1.0, 1.0, 0.2, None),
    "Concrete_Dirty": ("concrete_floor_damaged_01", (0.82, 0.78, 0.72), 1.05, 1.2, 0.7, None),
    "Concrete_Painted": ("concrete_floor_painted", (1.0, 1.0, 1.0), 0.9, 0.8, 0.2, None),
    "Concrete_AntiSlip": ("anti_slip_concrete", (0.95, 0.95, 0.95), 1.0, 1.0, 0.3, None),
    "Concrete_Wall": ("concrete_wall_006", (1.0, 0.98, 0.95), 1.0, 1.0, 0.35, None),
    "Concrete_Wall_Warm": ("concrete_wall_006", (1.05, 0.95, 0.82), 1.0, 1.0, 0.45, None),
    "Concrete_Block": ("concrete_block_wall", (0.98, 0.97, 0.95), 1.0, 1.0, 0.3, None),
    "Concrete_Pavers": ("concrete_pavers_02", (1.0, 1.0, 1.0), 1.0, 1.0, 0.25, None),
    "Asphalt_Old": ("road_damaged", (1.0, 1.0, 1.0), 1.0, 1.0, 0.2, None),
    "Asphalt_New": ("asphalt_04", (1.0, 1.0, 1.0), 1.0, 1.0, 0.15, None),
    "Asphalt_Grass": ("grass_concrete_pavement", (1.0, 1.0, 1.0), 1.0, 1.0, 0.2, DIRT),
    # Ground
    "Ground_Gravel": ("gravel_ground_01", (1.0, 1.0, 1.0), 1.0, 1.0, 0.0, DIRT),
    "Ground_Dirt": ("dry_ground_01", (1.0, 1.0, 1.0), 1.0, 1.0, 0.0, DIRT),
    "Ground_ParkDirt": ("park_dirt", (1.0, 1.0, 1.0), 1.0, 1.0, 0.0, DIRT),
    "Ground_Grass": ("leafy_grass", (1.0, 1.0, 1.0), 1.0, 1.0, 0.0, DIRT),
    "Ground_GrassDry": ("sparse_grass", (1.05, 1.0, 0.85), 1.0, 1.0, 0.0, DIRT),
    # Metal
    "Metal_Cladding": ("factory_wall", (1.0, 1.0, 1.0), 1.0, 1.0, 0.3, METAL),
    "Metal_Cladding_Blue": ("exterior_wall_cladding", (1.0, 1.0, 1.0), 1.0, 1.0, 0.25, METAL),
    "Metal_BoxProfile": ("box_profile_metal_sheet", (1.0, 1.0, 1.0), 1.0, 1.0, 0.3, METAL),
    "Metal_BoxProfile_Red": ("box_profile_metal_sheet", (1.1, 0.55, 0.45), 1.0, 1.0, 0.35, METAL),
    "Metal_Corrugated": ("corrugated_iron_02", (1.0, 1.0, 1.0), 1.0, 1.0, 0.35, METAL),
    "Metal_Container_Red": ("container_side", (1.0, 1.0, 1.0), 1.0, 1.0, 0.3, METAL),
    "Metal_Container_Blue": ("container_side", (0.45, 0.62, 0.95), 1.0, 1.0, 0.3, METAL),
    "Metal_Container_Green": ("container_side", (0.55, 0.85, 0.55), 1.0, 1.0, 0.3, METAL),
    "Metal_Container_Grey": ("container_side", (0.7, 0.7, 0.72), 1.0, 1.0, 0.3, METAL),
    "Metal_Plate_Blue": ("blue_metal_plate", (1.0, 1.0, 1.0), 1.0, 1.0, 0.2, METAL),
    "Metal_Grate": ("metal_grate_rusty", (1.0, 1.0, 1.0), 1.0, 1.0, 0.0, METAL),
    "Metal_Grid": ("rusty_metal_grid", (1.0, 1.0, 1.0), 1.0, 1.0, 0.0, METAL),
    "Metal_Shutter": ("painted_metal_shutter", (1.0, 1.0, 1.0), 1.0, 1.0, 0.25, METAL),
    "Metal_Rusty": ("rusty_painted_metal", (1.0, 1.0, 1.0), 1.0, 1.0, 0.2, METAL),
    "Metal_Plain": ("metal_plate", (1.0, 1.0, 1.0), 1.0, 1.0, 0.1, METAL),
    # Plaster, brick, stone
    "Plaster_Worn": ("worn_cracked_plaster", (1.0, 1.0, 1.0), 1.0, 1.0, 0.35, None),
    "Plaster_Ochre": ("worn_cracked_plaster", (1.08, 0.86, 0.58), 1.0, 1.0, 0.4, None),
    "Plaster_Red": ("red_plaster_weathered", (1.0, 1.0, 1.0), 1.0, 1.0, 0.35, None),
    "Plaster_Pink": ("red_plaster_weathered", (1.15, 0.95, 0.9), 1.0, 1.0, 0.3, None),
    "Plaster_Blue": ("blue_plaster_weathered", (1.0, 1.0, 1.0), 1.0, 1.0, 0.35, None),
    "Plaster_Yellow": ("yellow_plaster_02", (1.0, 1.0, 1.0), 1.0, 1.0, 0.35, None),
    "Plaster_Damaged": ("damaged_plaster", (1.0, 1.0, 1.0), 1.0, 1.0, 0.4, None),
    "Plaster_White": ("white_plaster_02", (1.0, 0.98, 0.94), 1.0, 1.0, 0.3, None),
    "Brick_Mixed": ("mixed_brick_wall", (1.0, 1.0, 1.0), 1.0, 1.0, 0.3, None),
    "Brick_Red": ("red_brick_03", (1.0, 1.0, 1.0), 1.0, 1.0, 0.3, None),
    "Stone_Blocks": ("large_sandstone_blocks_01", (1.0, 1.0, 1.0), 1.0, 1.0, 0.3, None),
    "Stone_Cobble": ("patterned_cobblestone", (1.0, 1.0, 1.0), 1.0, 1.0, 0.2, None),
    "Stone_CobbleGrass": ("grassy_cobblestone", (1.0, 1.0, 1.0), 1.0, 1.0, 0.1, DIRT),
    "Stone_Pavement": ("pavement_03", (1.0, 1.0, 1.0), 1.0, 1.0, 0.2, None),
    "Brick_Pavement": ("brick_pavement", (1.0, 1.0, 1.0), 1.0, 1.0, 0.2, None),
    "Stone_Tiles": ("stone_tiles_02", (1.0, 1.0, 1.0), 1.0, 1.0, 0.2, None),
    # Roofs, wood, bark
    "Roof_Terracotta": ("roof_tiles_14", (1.0, 1.0, 1.0), 1.0, 1.0, 0.3, None),
    "Roof_Clay": ("clay_roof_tiles_02", (1.0, 1.0, 1.0), 1.0, 1.0, 0.3, None),
    "Roof_Grey": ("grey_roof_tiles", (1.0, 1.0, 1.0), 1.0, 1.0, 0.3, None),
    "Wood_Planks": ("weathered_planks", (1.0, 1.0, 1.0), 1.0, 1.0, 0.2, WOOD),
    "Wood_Dark": ("weathered_planks", (0.6, 0.5, 0.42), 1.0, 1.0, 0.2, WOOD),
    "Bark": ("bark_platanus", (1.0, 1.0, 1.0), 1.0, 1.2, 0.0, WOOD),
}

# Poly Haven models. split: import every object as its own mesh (modular kits).
# tex: maximum texture size in game. collide: simple collision (False: none).
MODELS = {
    "modular_industrial_pipes_01": dict(split=True, tex=1024), "modular_airduct_circular_01": dict(split=True, tex=512),
    "modular_chainlink_fence": dict(split=True, tex=1024, masked=True), "portable_generator": dict(tex=512),
    "power_box_01": dict(tex=512), "exterior_aircon_unit": dict(tex=512), "security_light": dict(tex=512),
    "hanging_industrial_lamp": dict(tex=512), "mounted_fluorescent_lights": dict(tex=512), "propane_tank": dict(tex=512),
    "small_lpg_tank": dict(tex=512), "industrial_pastic_container": dict(tex=512), "plastic_crate_03": dict(tex=512),
    "barrel_03": dict(tex=512), "old_tyre": dict(tex=512), "hand_truck": dict(tex=512), "steel_frame_shelves_01": dict(tex=512),
    "worn_metal_rack": dict(tex=512), "ladder_sectioned_01": dict(tex=512), "concrete_road_barrier_02": dict(tex=512),
    "overhead_crane": dict(tex=1024), "old_military_compressor": dict(tex=1024), "modular_fire_escape": dict(split=True, tex=1024),
    "modular_metal_gutter": dict(split=True, tex=512), "street_lamp_01": dict(tex=512), "street_lamp_02": dict(tex=512),
    "metal_trash_can": dict(tex=512), "covered_car": dict(tex=1024), "rollershutter_door": dict(tex=1024),
    "rollershutter_window_01": dict(tex=512), "water_manhole_cover": dict(tex=512, collide=False), "utility_box_01": dict(tex=512),
    "security_camera_01": dict(tex=512, collide=False), "large_iron_gate": dict(tex=1024), "potted_plant_04": dict(tex=512, masked=True),
    "shrub_02": dict(split=True, tex=1024, masked=True, collide=False), "shrub_03": dict(split=True, tex=1024, masked=True, collide=False),
    "shrub_04": dict(split=True, tex=1024, masked=True, collide=False), "weed_plant_02": dict(split=True, tex=512, masked=True, collide=False),
    "fern_02": dict(split=True, tex=512, masked=True, collide=False), "grass_bermuda_01": dict(split=True, tex=512, masked=True, collide=False),
    "dandelion_01": dict(split=True, tex=512, masked=True, collide=False), "tree_stump_01": dict(tex=512), "rock_07": dict(tex=512),
    "rock_09": dict(tex=512), "stone_01": dict(tex=512), "namaqualand_stones_01": dict(split=True, tex=512),
}
NANITE_TRIANGLES = 2000
MASKED_LOD_TRIANGLES = 1000

PROP_PARENTS = {}

_log = []


def log(msg):
    _log.append(str(msg))
    unreal.log_warning("[V21] " + str(msg))


def flush():
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
    with open(LOG_PATH, "w", encoding="utf-8") as f:
        f.write("\n".join(_log))


def ensure_dir(path):
    if not EAL.does_directory_exist(path):
        EAL.make_directory(path)


def get_or_create(name, folder, cls, factory):
    path = folder + "/" + name
    if EAL.does_asset_exist(path):
        return EAL.load_asset(path)
    ensure_dir(folder)
    return AT.create_asset(name, folder, cls, factory)


REIMPORT = bool(os.environ.get("CS_V21_REIMPORT"))


def import_file(path, dest, name):
    # Already imported: keep it (set CS_V21_REIMPORT=1 to import again).
    if not REIMPORT and EAL.does_asset_exist(dest + "/" + name):
        return EAL.load_asset(dest + "/" + name)
    ensure_dir(dest)
    task = unreal.AssetImportTask()
    for k, v in (("filename", path), ("destination_path", dest), ("destination_name", name),
                 ("replace_existing", True), ("automated", True), ("save", False)):
        task.set_editor_property(k, v)
    AT.import_asset_tasks([task])
    return EAL.load_asset(dest + "/" + name)


def setup_texture(tex, kind, max_size=0):
    if kind == "normal":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("flip_green_channel", True)  # Poly Haven: OpenGL normals
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP)
    elif kind in ("mask", "gray"):
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS if kind == "mask"
                                else unreal.TextureCompressionSettings.TC_GRAYSCALE)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD_SPECULAR)
    else:
        tex.set_editor_property("srgb", kind != "linear")
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
    if max_size:
        tex.set_editor_property("max_texture_size", max_size)
    EAL.save_loaded_asset(tex, only_if_is_dirty=False)


def find(folder, *needles):
    if not os.path.isdir(folder):
        return None
    for f in sorted(os.listdir(folder)):
        low = f.lower()
        if all(n in low for n in needles):
            return os.path.join(folder, f)
    return None


# ---------------------------------------------------------------------------
# Material graph helpers
# ---------------------------------------------------------------------------

class Graph:
    def __init__(self, mat):
        self.mat = mat
        self.y = 0

    def node(self, cls, x, y, **props):
        n = MEL.create_material_expression(self.mat, cls, x, y)
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def link(self, a, b, pin="", out=""):
        MEL.connect_material_expressions(a, out, b, pin)

    def scalar(self, name, value, x, y):
        return self.node(unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)

    def vector(self, name, rgb, x, y):
        return self.node(unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name,
                         default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))

    def texture(self, name, uv, x, y, sampler, default):
        t = self.node(unreal.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=name, sampler_type=sampler)
        if default:
            t.set_editor_property("texture", default)
        if uv is not None:
            self.link(uv, t, "UVs")
        return t

    def op(self, cls, a, b, x, y):
        n = self.node(cls, x, y)
        self.link(a, n, "A")
        self.link(b, n, "B")
        return n

    def mul(self, a, b, x, y):
        return self.op(unreal.MaterialExpressionMultiply, a, b, x, y)

    def add(self, a, b, x, y):
        return self.op(unreal.MaterialExpressionAdd, a, b, x, y)

    def lerp(self, a, b, alpha, x, y):
        n = self.node(unreal.MaterialExpressionLinearInterpolate, x, y)
        self.link(a, n, "A")
        self.link(b, n, "B")
        self.link(alpha, n, "Alpha")
        return n

    def mask(self, src, x, y, r=False, g=False, b=False, a=False, out=""):
        m = self.node(unreal.MaterialExpressionComponentMask, x, y, r=r, g=g, b=b, a=a)
        self.link(src, m, "", out)
        return m

    def custom(self, code, inputs, x, y, out_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1):
        c = self.node(unreal.MaterialExpressionCustom, x, y, code=code, output_type=out_type)
        pins = []
        for name, _ in inputs:
            pin = unreal.CustomInput()
            pin.set_editor_property("input_name", name)
            pins.append(pin)
        c.set_editor_property("inputs", pins)
        for name, src in inputs:
            self.link(src, c, name)
        return c

    def out(self, src, prop, pin=""):
        MEL.connect_material_property(src, pin, prop)


SAMPLER_COLOR = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
SAMPLER_NORMAL = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
SAMPLER_MASKS = unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
SAMPLER_LINEAR = unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
SAMPLER_GRAY = unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE


def fresh_material(name):
    mat = get_or_create(name, MAT, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    return mat, Graph(mat)


def finish(mat):
    # Kit pieces and props are drawn as instances (ACSPropCluster) and some props
    # are Nanite: without these flags the game falls back to the default material.
    for flag in ("used_with_instanced_static_meshes", "used_with_nanite"):
        try:
            mat.set_editor_property(flag, True)
        except Exception as e:
            log("usage flag %s on %s: %s" % (flag, mat.get_name(), e))
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat, only_if_is_dirty=False)
    log("material %s" % mat.get_name())


# ---------------------------------------------------------------------------
# Textures
# ---------------------------------------------------------------------------

def import_sets():
    out = {}
    for sid in SETS:
        folder = os.path.join(SRC_PH, sid)
        d, n, a = find(folder, "diffuse"), find(folder, "nor_gl"), find(folder, "arm")
        if not (d and n and a):
            log("texture set MISSING: " + sid)
            continue
        dest = TEX + "/" + sid
        td = import_file(d, dest, "T_%s_D" % sid)
        tn = import_file(n, dest, "T_%s_N" % sid)
        ta = import_file(a, dest, "T_%s_ARM" % sid)
        setup_texture(td, "color")
        setup_texture(tn, "normal")
        setup_texture(ta, "mask")
        out[sid] = (td, tn, ta)
    for sid in OLD_SETS:  # v1.1 textures still used by the new maps
        base = "/Game/Environment/Textures/%s/T_%s_" % (sid, sid)
        t = [EAL.load_asset(base + k) for k in ("D", "N", "ARM")]
        if all(t):
            out[sid] = tuple(t)
    log("texture sets: %d" % len(out))
    return out


def import_generated():
    dest = TEX + "/Generated"
    out = {}
    for name, kind in (("T_DecalAtlas", "color"), ("T_Trim", "color"), ("T_Signs", "color"), ("T_MacroNoise", "linear"),
                       ("T_LeafCluster_D", "color"), ("T_LeafCluster_A", "mask"), ("T_GrassCard_D", "color"), ("T_GrassCard_A", "mask")):
        t = import_file(os.path.join(SRC_GEN, name + ".png"), dest, name)
        setup_texture(t, kind)
        out[name] = t
    return out


# ---------------------------------------------------------------------------
# Materials
# ---------------------------------------------------------------------------

def world_uv(g, scale_param_value, x, y, name="MacroScale"):
    """World XY (+Z mixed in) divided by a scale: UVs for the macro noise."""
    wp = g.node(unreal.MaterialExpressionWorldPosition, x, y)
    xy = g.mask(wp, x + 150, y, r=True, g=True)
    z = g.mask(wp, x + 150, y + 80, b=True)
    s = g.scalar(name, scale_param_value, x + 150, y + 160)
    mixed = g.add(xy, g.mul(z, g.node(unreal.MaterialExpressionConstant, x + 150, y + 240, r=0.37), x + 300, y + 200), x + 450, y + 60)
    return g.op(unreal.MaterialExpressionDivide, mixed, s, x + 600, y + 80)


def make_surface(gen, default_set):
    """M_CS_Surface: UV-mapped PBR with variation. UV0 of the kit meshes is in metres."""
    mat, g = fresh_material("M_CS_Surface")
    d0, n0, a0 = default_set
    uv = g.node(unreal.MaterialExpressionTextureCoordinate, -2600, 0)
    tile = g.scalar("TileSize", 300.0, -2600, 120)
    scale = g.op(unreal.MaterialExpressionDivide, g.node(unreal.MaterialExpressionConstant, -2450, 120, r=100.0), tile, -2300, 120)
    uvs = g.mul(uv, scale, -2150, 40)
    color = g.texture("Diffuse", uvs, -1900, -500, SAMPLER_COLOR, d0)
    nrm = g.texture("Normal", uvs, -1900, -150, SAMPLER_NORMAL, n0)
    arm = g.texture("ARM", uvs, -1900, 200, SAMPLER_MASKS, a0)
    noise = g.texture("MacroNoise", world_uv(g, 900.0, -2800, 600), -1900, 600, SAMPLER_LINEAR, gen["T_MacroNoise"])
    vc = g.node(unreal.MaterialExpressionVertexColor, -1900, 950)

    tint = g.vector("Tint", (1, 1, 1), -1500, -650)
    variation = g.scalar("MacroVariation", 0.18, -1500, -550)
    dirt_amount = g.scalar("DirtAmount", 0.3, -1500, 700)
    dirt_color = g.vector("DirtColor", (0.26, 0.22, 0.17), -1500, 800)
    rough_scale = g.scalar("RoughnessScale", 1.0, -1500, 300)
    normal_strength = g.scalar("NormalStrength", 1.0, -1500, -150)

    code = """
    // Optional desaturation (recolouring a texture through the tint), then the
    // macro variation: +-Variation around the tint, from the large blotches.
    float3 b = lerp(Base, dot(Base, float3(0.3, 0.59, 0.11)).xxx, Desat);
    float3 c = b * Tint * (1.0 + (Noise.r - 0.5) * 2.0 * Variation);
    // Grime: stronger where vertex colour R is low (the kit darkens the feet of walls),
    // broken up by the medium noise.
    float grime = saturate((1.0 - Vc) * 1.4 + (Noise.g - 0.55) * 1.6) * Dirt;
    return lerp(c, DirtColor * Base * 2.2, saturate(grime));
    """
    base = g.custom(code, [("Base", color), ("Tint", tint), ("Noise", noise), ("Variation", variation), ("Vc", g.mask(vc, -1700, 950, r=True)),
                           ("Dirt", dirt_amount), ("DirtColor", dirt_color), ("Desat", g.scalar("Desaturation", 0.0, -1500, -450))],
                    -1100, -500, unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    rough_code = "return saturate(R * Scale + (Noise.b - 0.5) * 0.12 + saturate((1.0 - Vc) + Noise.g - 0.6) * Dirt * 0.25);"
    rough = g.custom(rough_code, [("R", g.mask(arm, -1700, 250, g=True)), ("Scale", rough_scale), ("Noise", noise),
                                  ("Vc", g.mask(vc, -1700, 1000, r=True)), ("Dirt", dirt_amount)], -1100, 250)
    flat = g.node(unreal.MaterialExpressionConstant3Vector, -1500, -50, constant=unreal.LinearColor(0, 0, 1, 1))
    n = g.lerp(flat, g.mask(nrm, -1700, -150, r=True, g=True, b=True), normal_strength, -1100, -150)
    g.out(base, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(n, unreal.MaterialProperty.MP_NORMAL)
    g.out(rough, unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.mask(arm, -1100, 400, b=True), unreal.MaterialProperty.MP_METALLIC)
    g.out(g.mask(arm, -1100, 500, r=True), unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    finish(mat)
    return mat


def make_terrain(gen, default_set):
    """M_CS_Terrain: base layer + 3 layers by vertex colour R, G, B, height-sharpened."""
    mat, g = fresh_material("M_CS_Terrain")
    d0, n0, a0 = default_set
    uv = g.node(unreal.MaterialExpressionTextureCoordinate, -3000, 0)
    noise = g.texture("MacroNoise", world_uv(g, 1200.0, -3200, 1400), -2400, 1400, SAMPLER_LINEAR, gen["T_MacroNoise"])
    vc = g.node(unreal.MaterialExpressionVertexColor, -2400, 1800)
    layers = []
    for i in range(4):
        tile = g.scalar("Tile%d" % i, 300.0, -3000, -900 + i * 600)
        uvs = g.mul(uv, g.op(unreal.MaterialExpressionDivide, g.node(unreal.MaterialExpressionConstant, -2850, -900 + i * 600, r=100.0), tile, -2700, -900 + i * 600), -2550, -900 + i * 600)
        d = g.texture("Diffuse%d" % i, uvs, -2300, -1000 + i * 600, SAMPLER_COLOR, d0)
        n = g.texture("Normal%d" % i, uvs, -2300, -800 + i * 600, SAMPLER_NORMAL, n0)
        a = g.texture("ARM%d" % i, uvs, -2300, -600 + i * 600, SAMPLER_MASKS, a0)
        layers.append((d, n, a))
    sharp = g.scalar("BlendSharpness", 3.0, -1900, 1500)
    variation = g.scalar("MacroVariation", 0.15, -1900, 1600)
    weights = g.custom("""
    // Layer weights over the base: vertex colour, broken up by the noise, sharpened.
    float3 w = saturate((Vc.rgb - 0.5 + (Noise.g - 0.5) * 0.6) * Sharp + 0.5);
    return w;
    """, [("Vc", vc), ("Noise", noise), ("Sharp", sharp)], -1600, 1400, unreal.CustomMaterialOutputType.CMOT_FLOAT3)

    def blend(idx, channels, out_type):
        code = """
        float3 r = L0;
        r = lerp(r, L1, W.r);
        r = lerp(r, L2, W.g);
        r = lerp(r, L3, W.b);
        return r;
        """
        return g.custom(code, [("L%d" % i, channels(layers[i][idx], i)) for i in range(4)] + [("W", weights)],
                        -1200, -800 + idx * 500, out_type)

    base = blend(0, lambda t, i: t, unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    base = g.custom("return B * (1.0 + (N.r - 0.5) * 2.0 * V);", [("B", base), ("N", noise), ("V", variation)], -900, -800,
                    unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    nrm = blend(1, lambda t, i: g.mask(t, -1600, -700 + i * 60, r=True, g=True, b=True), unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    arm = blend(2, lambda t, i: g.mask(t, -1600, -300 + i * 60, r=True, g=True, b=True), unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    g.out(base, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(nrm, unreal.MaterialProperty.MP_NORMAL)
    g.out(g.mask(arm, -900, 0, g=True), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.mask(arm, -900, 100, r=True), unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    finish(mat)
    return mat


def atlas_uv(g, cols, rows, x, y, uv_node=None):
    uv = uv_node or g.node(unreal.MaterialExpressionTextureCoordinate, x, y)
    idx = g.scalar("Cell", 0.0, x, y + 120)
    return g.custom("""
    float col = fmod(Cell, Cols);
    float row = floor(Cell / Cols);
    return (frac(UV) + float2(col, row)) / float2(Cols, Rows);
    """, [("UV", uv), ("Cell", idx), ("Cols", g.node(unreal.MaterialExpressionConstant, x, y + 200, r=float(cols))),
          ("Rows", g.node(unreal.MaterialExpressionConstant, x, y + 280, r=float(rows)))], x + 250, y,
        unreal.CustomMaterialOutputType.CMOT_FLOAT2)


def make_decal(gen):
    mat, g = fresh_material("M_CS_Decal")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    uvs = atlas_uv(g, 4, 4, -1400, 0)
    t = g.texture("Atlas", uvs, -1000, 0, SAMPLER_COLOR, gen["T_DecalAtlas"])
    tint = g.vector("Tint", (1, 1, 1), -1000, -250)
    opacity = g.scalar("Opacity", 1.0, -1000, 300)
    rough = g.scalar("Roughness", 0.8, -1000, 400)
    g.out(g.mul(g.mask(t, -700, -100, r=True, g=True, b=True), tint, -500, -100), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.mul(g.mask(t, -700, 100, r=True, out="A"), opacity, -500, 150), unreal.MaterialProperty.MP_OPACITY)
    g.out(rough, unreal.MaterialProperty.MP_ROUGHNESS)
    finish(mat)
    return mat


def make_atlas(gen):
    """Signs: one cell of T_Signs (2 x 8) on a plain UV-mapped quad."""
    mat, g = fresh_material("M_CS_Atlas")
    uvs = atlas_uv(g, 2, 8, -1400, 0)
    t = g.texture("Atlas", uvs, -1000, 0, SAMPLER_COLOR, gen["T_Signs"])
    glow = g.scalar("Glow", 0.0, -1000, 300)
    g.out(t, unreal.MaterialProperty.MP_BASE_COLOR, "RGB")
    g.out(g.scalar("Roughness", 0.55, -700, 250), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.mul(g.mask(t, -700, 350, r=True, g=True, b=True), glow, -500, 350), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat)
    return mat


def make_glass(gen):
    mat, g = fresh_material("M_CS_Glass")
    noise = g.texture("MacroNoise", world_uv(g, 400.0, -1600, 300), -1000, 300, SAMPLER_LINEAR, gen["T_MacroNoise"])
    tint = g.vector("Tint", (0.05, 0.07, 0.08), -1000, -200)
    glow_color = g.vector("GlowColor", (1.0, 0.72, 0.4), -1000, 0)
    glow = g.scalar("Glow", 0.0, -1000, 100)
    g.out(tint, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.node(unreal.MaterialExpressionConstant, -700, 0, r=1.0), unreal.MaterialProperty.MP_SPECULAR)
    g.out(g.custom("return 0.04 + N.b * 0.12;", [("N", noise)], -700, 300), unreal.MaterialProperty.MP_ROUGHNESS)
    # Lit windows: only some panes glow (the noise picks them), and not evenly.
    g.out(g.custom("return Color * Glow * step(0.55, N.r) * (0.6 + N.g);", [("Color", glow_color), ("Glow", glow), ("N", noise)],
                   -700, 100, unreal.CustomMaterialOutputType.CMOT_FLOAT3), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat)
    return mat


def make_prop(gen, default_set, name="M_CS_Prop", masked=False, foliage=False):
    """Poly Haven props: separate diffuse / normal / roughness / metallic / alpha.
    Four parents (MI base property overrides are not reachable from Python):
    M_CS_Prop opaque; M_CS_PropCutout masked two-sided (fence wire); M_CS_PropMasked
    cut-out leaves and M_CS_PropFoliage opaque grass blades, both with the two-sided
    foliage shading model."""
    mat, g = fresh_material(name)
    if masked:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    if masked or foliage:
        mat.set_editor_property("two_sided", True)
    if foliage:
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    d0, n0, a0 = default_set
    d = g.texture("Diffuse", None, -1000, -400, SAMPLER_COLOR, d0)
    n = g.texture("Normal", None, -1000, -100, SAMPLER_NORMAL, n0)
    r = g.texture("Roughness", None, -1000, 200, SAMPLER_MASKS, a0)
    m = g.texture("Metallic", None, -1000, 500, SAMPLER_MASKS, a0)
    tint = g.vector("Tint", (1, 1, 1), -700, -500)
    base = g.mul(g.mask(d, -800, -400, r=True, g=True, b=True), tint, -500, -400)
    g.out(base, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(n, unreal.MaterialProperty.MP_NORMAL, "RGB")
    g.out(g.mul(g.mask(r, -800, 200, r=True), g.scalar("RoughnessScale", 1.0, -800, 300), -500, 200), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.mul(g.mask(m, -800, 500, r=True), g.scalar("MetallicScale", 1.0, -800, 600), -500, 500), unreal.MaterialProperty.MP_METALLIC)
    if masked:
        a = g.texture("Alpha", None, -1000, 800, SAMPLER_MASKS, a0)
        g.out(g.mask(a, -500, 800, r=True), unreal.MaterialProperty.MP_OPACITY_MASK)
    if foliage:
        # Light through the leaves: a darker, greener copy of the base colour.
        g.out(g.mul(base, g.vector("Transmission", (0.45, 0.55, 0.25), -700, -250), -300, -250), unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    finish(mat)
    return mat


def instance(name, folder, parent, textures=None, scalars=None, vectors=None, phys=None, overrides=None):
    mi = get_or_create(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, parent)
    for k, v in (textures or {}).items():
        if v:
            MEL.set_material_instance_texture_parameter_value(mi, k, v)
    for k, v in (scalars or {}).items():
        MEL.set_material_instance_scalar_parameter_value(mi, k, float(v))
    for k, v in (vectors or {}).items():
        MEL.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(v[0], v[1], v[2], 1.0))
    if phys:
        pm = EAL.load_asset(PHYS + "/" + phys)
        if pm:
            mi.set_editor_property("phys_material", pm)
    if overrides:
        props = mi.get_editor_property("base_property_overrides")
        for k, v in overrides.items():
            props.set_editor_property(k, v)
        mi.set_editor_property("base_property_overrides", props)
        MEL.update_material_instance(mi)
    EAL.save_loaded_asset(mi, only_if_is_dirty=False)
    return mi


def make_variants(surface, sets):
    for name, (sid, tint, rough, nstr, dirt, phys) in VARIANTS.items():
        if sid not in sets:
            log("variant %s: set %s MISSING" % (name, sid))
            continue
        d, n, a = sets[sid]
        tile = SETS.get(sid) or OLD_SETS.get(sid, 300)
        instance("MI_" + name, SURF, surface, {"Diffuse": d, "Normal": n, "ARM": a},
                 {"TileSize": tile, "RoughnessScale": rough, "NormalStrength": nstr, "DirtAmount": dirt}, {"Tint": tint}, phys)
    log("surface variants: %d" % len(VARIANTS))


# ---------------------------------------------------------------------------
# Props
# ---------------------------------------------------------------------------

def texture_group(folder, stem):
    """Textures of one material group: stem = file prefix before _diff/_nor_gl/..."""
    out = {}
    for key, needles, kind in (("Diffuse", ("_diff",), "color"), ("Normal", ("_nor_gl",), "normal"), ("Roughness", ("_rough",), "mask"),
                               ("Metallic", ("_metal",), "mask"), ("Alpha", ("_alpha",), "mask")):
        for f in sorted(os.listdir(folder)):
            low = f.lower()
            if low.startswith(stem.lower()) and any(n in low for n in needles) and not low.endswith(".blend"):
                out[key] = (os.path.join(folder, f), kind)
                break
    return out


def import_model(mid, opts, prop_mat):
    folder = os.path.join(SRC_PH, mid)
    fbx = find(folder, ".fbx")
    if not fbx:
        log("model MISSING: " + mid)
        return []
    dest = PROPS + "/" + mid
    ensure_dir(dest)
    # A model packs several variants side by side: split ones replace the combined mesh.
    if opts.get("split") and EAL.does_asset_exist(dest + "/SM_" + mid):
        EAL.delete_asset(dest + "/SM_" + mid)
    existing = [EAL.load_asset(p) for p in EAL.list_assets(dest, recursive=False, include_folder=False)] if EAL.does_directory_exist(dest) else []
    existing = [m for m in existing if isinstance(m, unreal.StaticMesh)]
    if existing and not REIMPORT:
        # Already imported: only re-apply the settings below (Nanite, materials).
        return finish_meshes(mid, opts, existing, prop_mat, folder, dest)
    task = unreal.AssetImportTask()
    for k, v in (("filename", fbx), ("destination_path", dest), ("replace_existing", True), ("automated", True), ("save", False)):
        task.set_editor_property(k, v)
    if not opts.get("split"):
        task.set_editor_property("destination_name", "SM_" + mid)
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_as_skeletal", False)
    smd = ui.static_mesh_import_data
    smd.set_editor_property("combine_meshes", not opts.get("split"))
    smd.set_editor_property("auto_generate_collision", opts.get("collide", True))
    smd.set_editor_property("generate_lightmap_u_vs", False)
    task.set_editor_property("options", ui)
    AT.import_asset_tasks([task])
    meshes = [EAL.load_asset(p) for p in task.get_editor_property("imported_object_paths")]
    meshes = [m for m in meshes if isinstance(m, unreal.StaticMesh)]
    if not meshes:
        log("model import FAILED: " + mid)
        return []

    return finish_meshes(mid, opts, meshes, prop_mat, folder, dest)


def finish_meshes(mid, opts, meshes, prop_mat, folder, dest):
    texdir = os.path.join(folder, "textures")
    mi_cache = {}
    for mesh in meshes:
        for i, slot in enumerate(mesh.static_materials):
            slot_name = str(slot.material_slot_name)
            groups = [f.split("_diff")[0] for f in os.listdir(texdir) if "_diff" in f.lower()] if os.path.isdir(texdir) else []
            # The slot is named after its texture group; a single-group model uses that one.
            stem = next((s for s in sorted(groups, key=len, reverse=True) if s.lower() in slot_name.lower() or slot_name.lower() in s.lower()),
                        groups[0] if len(groups) == 1 else (groups[0] if groups else None))
            if not stem:
                continue
            if stem not in mi_cache:
                files = texture_group(texdir, stem)
                textures = {}
                for key, (path, kind) in files.items():
                    t = import_file(path, dest, "T_%s_%s" % (stem, key[0] if key != "Metallic" else "M"))
                    if t:
                        setup_texture(t, kind, opts.get("tex", 1024))
                        textures[key] = t
                parent = prop_mat
                if opts.get("masked"):
                    plant = opts.get("collide", True) is False
                    # With an alpha: cutout (masked, two-sided). Without one: foliage shading for
                    # plants only - a fence's steel posts get the plain opaque prop material.
                    if "Alpha" in textures:
                        parent = PROP_PARENTS["masked" if plant else "cutout"]
                    else:
                        parent = PROP_PARENTS["foliage"] if plant else prop_mat
                # Every texture parameter gets a value of the right kind: a leftover from an
                # earlier import (or the engine's white texture) would not match the Masks sampler.
                cut = parent in (PROP_PARENTS["masked"], PROP_PARENTS["cutout"])
                for key in ("Roughness", "Metallic") + (("Alpha",) if cut else ()):
                    if key not in textures:
                        textures[key] = MEL.get_material_default_texture_parameter_value(parent, key)
                if "Alpha" in textures and not cut:
                    del textures["Alpha"]
                mi_cache[stem] = instance("MI_" + stem, dest, parent, textures,
                                          {"MetallicScale": 1.0 if files.get("Metallic") else 0.0})
            mesh.set_material(i, mi_cache[stem])
        # Per mesh, by its material: a split model (a fence) has opaque posts and masked panels.
        cut_parents = (PROP_PARENTS["masked"], PROP_PARENTS["cutout"], PROP_PARENTS["foliage"])
        masked = any(slot.material_interface and slot.material_interface.get_editor_property("parent") in cut_parents
                     for slot in mesh.static_materials)
        tris = mesh.get_num_triangles(0)
        # Opaque meshes above NANITE_TRIANGLES go Nanite. Masked ones stay off Nanite (overdraw)
        # and get three reduced LODs instead, so the distant fence and bushes cost a fraction.
        nanite = tris > NANITE_TRIANGLES and not masked
        ns = mesh.get_editor_property("nanite_settings")
        ns.set_editor_property("enabled", nanite)
        mesh.set_editor_property("nanite_settings", ns)
        lods = 1
        if masked and tris > MASKED_LOD_TRIANGLES:
            opts_r = unreal.EditorScriptingMeshReductionOptions()
            opts_r.set_editor_property("auto_compute_lod_screen_size", False)
            opts_r.set_editor_property("reduction_settings", [
                unreal.EditorScriptingMeshReductionSettings(1.0, 1.0),
                unreal.EditorScriptingMeshReductionSettings(0.5, 0.35),
                unreal.EditorScriptingMeshReductionSettings(0.25, 0.15),
                unreal.EditorScriptingMeshReductionSettings(0.12, 0.06)])
            sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
            lods = sub.set_lods(mesh, opts_r) if sub else unreal.EditorStaticMeshLibrary.set_lods(mesh, opts_r)
        EAL.save_loaded_asset(mesh, only_if_is_dirty=False)
        box = mesh.get_bounding_box()
        log("prop %s: %d tris%s%s, %.0f x %.0f x %.0f cm" % (mesh.get_name(), tris, " (Nanite)" if nanite else "",
                                                            " (%d LODs)" % lods if lods > 1 else "",
                                                            box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z))
    return meshes


def main():
    gen = import_generated()
    sets = import_sets()
    default = sets.get("concrete_floor_damaged_01") or next(iter(sets.values()))
    surface = make_surface(gen, default)
    make_terrain(gen, default)
    make_decal(gen)
    make_atlas(gen)
    make_glass(gen)
    prop = make_prop(gen, default)
    PROP_PARENTS["masked"] = make_prop(gen, default, "M_CS_PropMasked", masked=True, foliage=True)
    PROP_PARENTS["foliage"] = make_prop(gen, default, "M_CS_PropFoliage", foliage=True)
    PROP_PARENTS["cutout"] = make_prop(gen, default, "M_CS_PropCutout", masked=True)
    flat = unreal.load_object(None, "/Engine/EngineMaterials/FlatNormal.FlatNormal")
    instance("MI_LeafCluster", SURF, PROP_PARENTS["masked"], {
        "Diffuse": gen["T_LeafCluster_D"], "Alpha": gen["T_LeafCluster_A"], "Normal": flat,
        "Roughness": MEL.get_material_default_texture_parameter_value(PROP_PARENTS["masked"], "Roughness"),
        "Metallic": MEL.get_material_default_texture_parameter_value(PROP_PARENTS["masked"], "Metallic")},
        {"MetallicScale": 0.0, "RoughnessScale": 0.9})
    instance("MI_GrassCard", SURF, PROP_PARENTS["masked"], {
        "Diffuse": gen["T_GrassCard_D"], "Alpha": gen["T_GrassCard_A"], "Normal": flat,
        "Roughness": MEL.get_material_default_texture_parameter_value(PROP_PARENTS["masked"], "Roughness"),
        "Metallic": MEL.get_material_default_texture_parameter_value(PROP_PARENTS["masked"], "Metallic")},
        {"MetallicScale": 0.0, "RoughnessScale": 1.0})
    make_variants(surface, sets)
    only = os.environ.get("CS_V21_MODELS")
    for mid, opts in MODELS.items():
        if only and mid not in only.split(","):
            continue
        import_model(mid, opts, prop)
    log("done")
    flush()


try:
    main()
except Exception as e:
    import traceback
    log("FAILED: %s\n%s" % (e, traceback.format_exc()))
    flush()
