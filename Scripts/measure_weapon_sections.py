"""
Development helper for weapon hand points: cross-sections of weapon meshes.
For each station X it slices the mesh with the plane x = X and prints the
lowest and highest Z (and the Y span), so a hand point can be put exactly on a surface
instead of guessed from a profile image.

    set CS_SECTION_SPEC=/Game/Weapons/Fab/SM_Sniper:-90,-40,-28;/Game/Weapons/Fab/SM_SMG:0,13
    UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script=Scripts/measure_weapon_sections.py

Output: Saved/WeaponProfiles/sections.txt (mesh units, before Scale).
"""

import os
import unreal

EAL = unreal.EditorAssetLibrary
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJECT_DIR, "Saved", "WeaponProfiles", "sections.txt")

def section_points(mesh, x):
    """Points where the mesh's triangles cross the plane x = X (exact slice)."""
    desc = mesh.get_static_mesh_description(0)
    out = []
    for i in range(desc.get_triangle_count()):
        tid = unreal.TriangleID()
        tid.set_editor_property("id_value", i)
        if not desc.is_triangle_valid(tid):
            continue
        section = desc.get_triangle_polygon_group(tid).get_editor_property("id_value")
        tri = [desc.get_vertex_position(desc.get_vertex_instance_vertex(vi)) for vi in desc.get_triangle_vertex_instances(tid)][:3]
        for a, b in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
            if (a.x - x) * (b.x - x) <= 0 and a.x != b.x:
                t = (x - a.x) / (b.x - a.x)
                out.append((section, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t))
    return out

def main():
    lines = []
    for entry in [e for e in os.environ.get("CS_SECTION_SPEC", "").split(";") if e]:
        path, stations = entry.split(":")
        mesh = EAL.load_asset(path)
        if mesh is None:
            lines.append("MISSING " + path)
            continue
        lines.append("%s (exact slices)" % path)
        for x in [float(s) for s in stations.split(",")]:
            near = section_points(mesh, x)
            if not near:
                lines.append("  x=%7.1f: nothing" % x)
                continue
            by_section = {}
            for s, y, z in near:
                by_section.setdefault(s, []).append((y, z))
            parts = []
            for s in sorted(by_section):
                pts_s = by_section[s]
                parts.append("s%d z[%.1f..%.1f] y[%.1f..%.1f]" % (s, min(p[1] for p in pts_s), max(p[1] for p in pts_s),
                                                             min(p[0] for p in pts_s), max(p[0] for p in pts_s)))
            lines.append("  x=%7.1f: %s" % (x, " | ".join(parts)))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        f.write("\n".join(lines))

main()
