"""
Map statistics for the v2.1 rework reports (docs/MAPS_REWORK.md): actors, static mesh
components and instances, triangles (LOD0 / Nanite fallback), Nanite meshes, materials
and the textures they reference (size on disk and in memory at full mip).

    UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script="Scripts/maps/stats.py" -- /Game/Maps/Lvl_Depot

Writes Saved/Logs/stats_<map>.txt.
"""
import os
import sys
import unreal

LEVEL = next((a for a in sys.argv[1:] if a.startswith("/Game/")), "/Game/Maps/Lvl_Depot")
out = []


def log(s):
    out.append(s)


unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
world = unreal.EditorLevelLibrary.get_editor_world()
actors = unreal.EditorLevelLibrary.get_all_level_actors()
sm_sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)

comps, instances, tris, nanite_tris = 0, 0, 0, 0
meshes, materials = {}, set()
lights = 0
for a in actors:
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        mesh = c.get_editor_property("static_mesh")
        if not mesh:
            continue
        n = c.get_instance_count() if isinstance(c, unreal.InstancedStaticMeshComponent) else 1
        if n == 0:
            continue
        comps += 1
        instances += n
        t = mesh.get_num_triangles(0)
        nanite = mesh.get_editor_property("nanite_settings").get_editor_property("enabled")
        rec = meshes.setdefault(mesh.get_path_name(), [0, t, nanite])
        rec[0] += n
        tris += t * n
        if nanite:
            nanite_tris += t * n
        for i in range(c.get_num_materials()):
            m = c.get_material(i)
            if m:
                materials.add(m.get_path_name())
    lights += len(a.get_components_by_class(unreal.LightComponent))

textures = {}
for mp in materials:
    m = unreal.load_object(None, mp)
    if not m:
        continue
    mel = unreal.MaterialEditingLibrary
    if isinstance(m, unreal.MaterialInstance):
        for name in mel.get_texture_parameter_names(m.get_base_material()):
            t = mel.get_material_instance_texture_parameter_value(m, name)
            if t:
                textures[t.get_path_name()] = t
        base = m.get_base_material()
    else:
        base = m
    if isinstance(base, unreal.Material):
        for t in mel.get_used_textures(base):
            textures[t.get_path_name()] = t
tex_bytes, tex_disk = 0, 0
sizes = {}
for p, t in textures.items():
    w = t.blueprint_get_size_x() if hasattr(t, "blueprint_get_size_x") else 0
    h = t.blueprint_get_size_y() if hasattr(t, "blueprint_get_size_y") else 0
    fmt = str(t.get_editor_property("compression_settings"))
    # BC1 0.5 B/px, BC5/BC7 1 B/px, full mip chain x 4/3.
    bpp = 0.5 if ("DEFAULT" in fmt and not t.get_editor_property("srgb") is None and "NORMAL" not in fmt) else 1.0
    if "MASKS" in fmt or "NORMAL" in fmt or "BC7" in fmt:
        bpp = 1.0
    tex_bytes += int(w * h * bpp * 4 / 3)
    key = "%dx%d" % (w, h)
    sizes[key] = sizes.get(key, 0) + 1
    f = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()) + p.split(".")[0].replace("/Game/", "") + ".uasset"
    if os.path.exists(f):
        tex_disk += os.path.getsize(f)

# Shader complexity: instruction counts of the base materials the map uses (the editor's
# material statistics; the shader complexity view mode is not available in -game).
bases = {}
for mp in materials:
    m = unreal.load_object(None, mp)
    base = m.get_base_material() if m else None
    if base:
        bases.setdefault(base.get_path_name(), [base, 0])[1] += 1
shader_lines = []
for bp, (base, users) in sorted(bases.items(), key=lambda kv: -kv[1][1]):
    try:
        st = unreal.MaterialEditingLibrary.get_statistics(base)
        shader_lines.append("  %-28s %-12s used by %3d MIs: PS %3d instr, VS %3d instr, %2d samplers" % (
            base.get_name(), str(base.get_editor_property("blend_mode")).split(".")[-1].replace(">", ""), users,
            st.num_pixel_shader_instructions, st.num_vertex_shader_instructions, st.num_samplers))
    except Exception as e:
        shader_lines.append("  %s: statistics unavailable (%s)" % (base.get_name(), e))

nanite_meshes = [p for p, r in meshes.items() if r[2]]
log("map %s" % LEVEL)
log("actors %d" % len(actors))
log("static mesh components %d (instanced %d instances in total)" % (comps, instances))
log("distinct static meshes %d, Nanite-enabled %d" % (len(meshes), len(nanite_meshes)))
log("triangles placed (LOD0 x instances) %d, of them on Nanite meshes %d" % (tris, nanite_tris))
log("materials %d, textures %d (%s)" % (len(materials), len(textures), ", ".join("%s x%d" % kv for kv in sorted(sizes.items(), key=lambda kv: -kv[1]))))
log("texture memory at full mip (block-compressed estimate) %.0f MB, on disk %.0f MB" % (tex_bytes / 1048576.0, tex_disk / 1048576.0))
log("light components %d" % lights)
log("base materials (shader complexity):")
for line in shader_lines:
    log(line)
top = sorted(meshes.items(), key=lambda kv: -kv[1][0] * kv[1][1])[:12]
for p, (n, t, nan) in top:
    log("  %-60s x%-5d %7d tris%s" % (p.split(".")[-1], n, t, " nanite" if nan else ""))
for p in nanite_meshes:
    log("  nanite: %s" % p.split(".")[-1])
name = LEVEL.split("/")[-1]
open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "stats_%s.txt" % name), "w").write("\n".join(out))
