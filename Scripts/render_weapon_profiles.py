"""
Development helper for the v1.0 weapon models: draws a side view (X right,
Z up) of every mesh in /Game/Weapons/Quaternius into Saved/WeaponProfiles/
<name>.png, coloured by material section, over a grid (thin lines every 10
units, thick every 50, red axes through the origin). The grip, support-hand
and sight points in Config/DefaultGame.ini are read off these images.

    UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script=Scripts/render_weapon_profiles.py

Pure Python PNG writer (zlib + struct): the editor's Python has no imaging
library.
"""

import os
import struct
import zlib
import unreal

EAL = unreal.EditorAssetLibrary
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJECT_DIR, "Saved", "WeaponProfiles")
SRC = "/Game/Weapons/Quaternius"
PX_PER_UNIT = 2.0
MARGIN = 20

PALETTE = [(200, 120, 60), (60, 60, 60), (120, 120, 130), (170, 170, 180), (90, 60, 40),
           (80, 140, 80), (60, 110, 170), (190, 190, 90)]


def write_png(path, w, h, pixels):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw.extend(pixels[y * w * 3:(y + 1) * w * 3])

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def render(mesh, name, report):
    box = mesh.get_bounding_box()
    x0, x1 = min(box.min.x, 0) - 10, max(box.max.x, 0) + 10
    z0, z1 = min(box.min.z, 0) - 10, max(box.max.z, 0) + 10
    w = int((x1 - x0) * PX_PER_UNIT) + 2 * MARGIN
    h = int((z1 - z0) * PX_PER_UNIT) + 2 * MARGIN
    pix = bytearray([255] * (w * h * 3))

    def to_px(x, z):
        return (MARGIN + (x - x0) * PX_PER_UNIT, MARGIN + (z1 - z) * PX_PER_UNIT)

    def put(px, py, c):
        if 0 <= px < w and 0 <= py < h:
            i = (py * w + px) * 3
            pix[i:i + 3] = bytes(c)

    # Grid.
    for gx in range(int(x0) // 10 * 10, int(x1) + 10, 10):
        px = int(to_px(gx, 0)[0])
        col = (255, 0, 0) if gx == 0 else ((150, 150, 150) if gx % 50 == 0 else (220, 220, 220))
        for py in range(h):
            put(px, py, col)
    for gz in range(int(z0) // 10 * 10, int(z1) + 10, 10):
        py = int(to_px(0, gz)[1])
        col = (255, 0, 0) if gz == 0 else ((150, 150, 150) if gz % 50 == 0 else (220, 220, 220))
        for px in range(w):
            put(px, py, col)

    # Triangles grouped by polygon group (= material section), from the
    # mesh description; the ProceduralMesh helper is not enabled here.
    desc = mesh.get_static_mesh_description(0)
    groups = {}
    for i in range(desc.get_triangle_count()):
        tid = unreal.TriangleID()
        tid.set_editor_property("id_value", i)
        if not desc.is_triangle_valid(tid):
            continue
        g = desc.get_triangle_polygon_group(tid).get_editor_property("id_value")
        vids = [desc.get_vertex_instance_vertex(vi) for vi in desc.get_triangle_vertex_instances(tid)][:3]
        groups.setdefault(g, []).append([desc.get_vertex_position(v) for v in vids])

    for s, triangles in sorted(groups.items()):
        col = PALETTE[s % len(PALETTE)]
        mat = mesh.get_material(s) if s < len(mesh.static_materials) else None
        pts = [p for tri in triangles for p in tri]
        report.append("  section %d %-10s x[%.0f..%.0f] y[%.0f..%.0f] z[%.0f..%.0f] color=%s" % (
            s, mat.get_name() if mat else "None", min(p.x for p in pts), max(p.x for p in pts),
            min(p.y for p in pts), max(p.y for p in pts), min(p.z for p in pts), max(p.z for p in pts), col))
        for a, b, c in triangles:
            pa, pb, pc = to_px(a.x, a.z), to_px(b.x, b.z), to_px(c.x, c.z)
            minx = int(max(0, min(pa[0], pb[0], pc[0])))
            maxx = int(min(w - 1, max(pa[0], pb[0], pc[0])))
            miny = int(max(0, min(pa[1], pb[1], pc[1])))
            maxy = int(min(h - 1, max(pa[1], pb[1], pc[1])))
            den = (pb[1] - pc[1]) * (pa[0] - pc[0]) + (pc[0] - pb[0]) * (pa[1] - pc[1])
            if abs(den) < 1e-6:
                continue
            for py in range(miny, maxy + 1):
                for px in range(minx, maxx + 1):
                    l1 = ((pb[1] - pc[1]) * (px - pc[0]) + (pc[0] - pb[0]) * (py - pc[1])) / den
                    l2 = ((pc[1] - pa[1]) * (px - pc[0]) + (pa[0] - pc[0]) * (py - pc[1])) / den
                    if l1 >= 0 and l2 >= 0 and l1 + l2 <= 1:
                        put(px, py, col)

    # Grid again on top, faint, so it stays readable over the gun.
    for gx in range(int(x0) // 10 * 10, int(x1) + 10, 50):
        px = int(to_px(gx, 0)[0])
        for py in range(0, h, 3):
            put(px, py, (255, 0, 0) if gx == 0 else (0, 0, 255))
    for gz in range(int(z0) // 10 * 10, int(z1) + 10, 50):
        py = int(to_px(0, gz)[1])
        for px in range(0, w, 3):
            put(px, py, (255, 0, 0) if gz == 0 else (0, 0, 255))

    write_png(os.path.join(OUT, name + ".png"), w, h, pix)
    report.append("  image %dx%d, x0=%.0f z1=%.0f, %.1f px/unit, margin %d" % (w, h, x0, z1, PX_PER_UNIT, MARGIN))


def main():
    os.makedirs(OUT, exist_ok=True)
    report = []
    for path in EAL.list_assets(SRC, recursive=True, include_folder=False):
        asset = EAL.load_asset(path)
        if isinstance(asset, unreal.StaticMesh):
            name = asset.get_name()
            report.append(name)
            render(asset, name, report)
    with open(os.path.join(OUT, "report.txt"), "w") as f:
        f.write("\n".join(report))


main()
