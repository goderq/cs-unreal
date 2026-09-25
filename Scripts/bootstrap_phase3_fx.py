"""
v2.0 phase 3 (AUDIT C10): particle materials for the Niagara effects.

Run BEFORE the FX builder commandlet, which assigns these materials:

    UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script=Scripts/bootstrap_phase3_fx.py
    UnrealEditor-Cmd.exe CSFusion.uproject -run=CSFXBuilder

Both materials take their colour from the particle (Particle Color), so one
material serves every effect:

  M_CS_ParticleAdditive     sparks, flashes, embers: rgb * alpha * round mask
  M_CS_ParticleTranslucent  dust, smoke, chips, blood: lit, rgb as base colour,
                            alpha * round mask,
                            softened where it meets geometry (depth fade)

Idempotent: the materials are rebuilt in place on every run.
"""

import os
import unreal

ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
LOG_PATH = os.path.join(PROJECT_DIR, "Saved", "Logs", "bootstrap_phase3_fx.txt")
FX_MAT_DIR = "/Game/FX/Materials"

_log_lines = []


def log(msg):
    line = "[CS-Phase3FX] " + str(msg)
    _log_lines.append(line)
    unreal.log(line)


def flush_log():
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
    with open(LOG_PATH, "w") as f:
        f.write("\n".join(_log_lines))


def get_or_create_material(name):
    path = FX_MAT_DIR + "/" + name
    if EAL.does_asset_exist(path):
        mat = EAL.load_asset(path)
        MEL.delete_all_material_expressions(mat)
        return mat
    return ASSET_TOOLS.create_asset(name, FX_MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())


def link(a, a_out, b, b_in):
    if not MEL.connect_material_expressions(a, a_out, b, b_in):
        log("  LINK FAILED {0}.{1} -> {2}.{3}".format(a.get_class().get_name(), a_out, b.get_class().get_name(), b_in))


def expr(mat, cls, x, y):
    return MEL.create_material_expression(mat, cls, x, y)


def radial_mask(mat, x, y, exponent):
    """1 at the UV centre, 0 at the edge: saturate(1 - 2*|uv - 0.5|) ^ exponent."""
    uv = expr(mat, unreal.MaterialExpressionTextureCoordinate, x - 800, y)
    centre = expr(mat, unreal.MaterialExpressionConstant2Vector, x - 800, y + 120)
    centre.set_editor_property("r", 0.5)
    centre.set_editor_property("g", 0.5)
    dist = expr(mat, unreal.MaterialExpressionDistance, x - 600, y)
    link(uv, "", dist, "A")
    link(centre, "", dist, "B")
    scale = expr(mat, unreal.MaterialExpressionMultiply, x - 450, y)
    scale.set_editor_property("const_b", 2.0)
    link(dist, "", scale, "A")
    inv = expr(mat, unreal.MaterialExpressionOneMinus, x - 300, y)
    link(scale, "", inv, "")
    sat = expr(mat, unreal.MaterialExpressionSaturate, x - 200, y)
    link(inv, "", sat, "")
    powr = expr(mat, unreal.MaterialExpressionPower, x - 100, y)
    powr.set_editor_property("const_exponent", exponent)
    link(sat, "", powr, "Base")
    return powr


def particle_color(mat, x, y):
    """Returns (rgb, alpha) expressions of the particle colour.

    The node's main output is RGB; alpha has its own pin. Each goes through a
    x1 multiply so callers link from a plain single-output expression."""
    pc = expr(mat, unreal.MaterialExpressionParticleColor, x, y)
    rgb = expr(mat, unreal.MaterialExpressionMultiply, x + 200, y)
    rgb.set_editor_property("const_b", 1.0)
    link(pc, "", rgb, "A")
    alpha = expr(mat, unreal.MaterialExpressionMultiply, x + 200, y + 120)
    alpha.set_editor_property("const_b", 1.0)
    link(pc, "A", alpha, "A")
    return rgb, alpha


def common(mat, blend, lit=False):
    mat.set_editor_property("blend_mode", blend)
    # Set before any node exists: changing these later rebuilds the graph and
    # drops links made through MaterialEditingLibrary.
    if lit:
        # Dust and smoke darken in shadow instead of glowing; the translucency
        # lighting volume is the cheap way particles take light.
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
        mat.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL)
    else:
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    for usage in ("used_with_niagara_sprites", "used_with_niagara_mesh_particles"):
        mat.set_editor_property(usage, True)


def make_additive():
    mat = get_or_create_material("M_CS_ParticleAdditive")
    common(mat, unreal.BlendMode.BLEND_ADDITIVE)
    rgb, alpha = particle_color(mat, -900, -200)
    mask = radial_mask(mat, -300, 200, 2.0)
    lit = expr(mat, unreal.MaterialExpressionMultiply, -400, -150)
    link(rgb, "", lit, "A")
    link(alpha, "", lit, "B")
    out = expr(mat, unreal.MaterialExpressionMultiply, -150, -100)
    link(lit, "", out, "A")
    link(mask, "", out, "B")
    MEL.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log("material M_CS_ParticleAdditive")


def make_translucent():
    mat = get_or_create_material("M_CS_ParticleTranslucent")
    common(mat, unreal.BlendMode.BLEND_TRANSLUCENT, lit=True)
    rgb, alpha = particle_color(mat, -900, -200)
    MEL.connect_material_property(rgb, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = expr(mat, unreal.MaterialExpressionConstant, -400, -40)
    rough.set_editor_property("r", 1.0)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mask = radial_mask(mat, -500, 200, 1.5)
    shaped = expr(mat, unreal.MaterialExpressionMultiply, -350, 100)
    link(alpha, "", shaped, "A")
    link(mask, "", shaped, "B")
    # Soft particle: fades where the sprite cuts into a wall or the floor.
    fade = expr(mat, unreal.MaterialExpressionDepthFade, -150, 100)
    fade.set_editor_property("fade_distance_default", 8.0)
    link(shaped, "", fade, "Opacity")
    MEL.connect_material_property(fade, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log("material M_CS_ParticleTranslucent")


def main():
    try:
        make_additive()
        make_translucent()
        log("done")
    finally:
        flush_log()


main()
