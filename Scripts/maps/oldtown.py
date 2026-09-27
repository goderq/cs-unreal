"""
v2.1 Old Town - an old quarter on a hillside on a clear morning (docs/MAPS_REWORK.md 4).
90 x 80 m, Alpha in the south on the lower street, Bravo in the north on the upper street.

    UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script=Scripts/maps/oldtown.py

Levels (z, cm) and zones (x east, y north):
  lower street   0     y -3500..-2500, shops of the south row, Alpha
  courtyard 1    0     x -4400..-3000, y -2000..-1250, through an arch from the lower street
  square        200    x -1800..3000, y -1000..2400: fountain, loggia with a cafe, plane trees,
                       church with a bell tower on the east, palazzo with an arcade on the west
  courtyard 2   200    x -4400..-2400, y -1250..0, external stair to the gallery
  gallery       450    along x -3000 from y 900 to the upper street
  east alley   0-450   x 3000..3700, stepped lane from the lower to the upper street, garden
  upper street  450    y 2400..3400 above a retaining wall with a balustrade, Bravo
"""

import math
import os
import sys

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal  # noqa: E402,F401
import lib  # noqa: E402
import kit  # noqa: E402
from lib import surface as S  # noqa: E402

LEVEL = "/Game/Maps/Lvl_OldTown"
OUT = lib.GEN + "/OldTown"
X, Y = 4500, 4000
LOW, SQ, UP = 0.0, 200.0, 450.0
FH = 320.0                         # floor height
T = 40.0                           # outer wall thickness
ALLEY_X0, ALLEY_X1 = 3000.0, 3700.0
FL1_Y0, FL1_Y1 = -2400.0, -2092.0  # east alley flight 0 -> 200
FL2_Y0, FL2_Y1 = 1600.0, 1992.0    # east alley flight 200 -> 450
SQ_EDGE = -1320.0                  # the square level starts here (under MidB's north wall)


def landing(b, x0, x1, y0, y1, z, mat):
    """A slab at the top of a flight, over the terrain's 50 cm step, so the top joins the floor."""
    b.box(x0, x1, y0, y1, z - 40, z + 1, mat)


# ---------------------------------------------------------------------------
# Terrain: terraces with the steps hidden in buildings, walls and stairs
# ---------------------------------------------------------------------------

def ground(x, y):
    if ALLEY_X0 <= x <= ALLEY_X1 + 800 and y >= FL1_Y1:
        if y >= FL2_Y1:
            return UP
        return SQ
    if y >= 2400:
        return UP
    if y >= (-1250 if x <= -2920 else SQ_EDGE):
        return SQ
    return LOW


def layers(x, y):
    """(cobble, dirt, grass) over the stone pavement base."""
    cobble = dirt = grass = 0.0
    if -3500 < y < -2500:
        cobble = 1.0                                   # lower street
    if 2400 < y < 3400:
        cobble = 1.0                                   # upper street
    if ALLEY_X0 < x < ALLEY_X1:
        cobble = 1.0                                   # east alley
    if x < -3000 and -2000 < y < -1250:
        dirt = 0.7                                     # courtyard 1
    if x < -2400 and -1250 < y < 0:
        dirt = 0.4                                     # courtyard 2
    if ALLEY_X1 < x < 4400 and -1000 < y < 1000:
        grass = 1.0                                    # garden
    for tx, ty in TREES:
        d = math.hypot(x - tx, y - ty)
        if d < 180:
            dirt = max(dirt, 1.0 - d / 180.0)          # tree pits on the square
    return cobble, dirt, grass


TREES = [(-1400, -500), (1300, -500), (-1400, 1900), (1200, 1900)]


def terrain_material():
    mel = unreal.MaterialEditingLibrary
    lib.ensure_dir(OUT)
    path = OUT + "/MI_Terrain_OldTown"
    mi = lib.EAL.load_asset(path) if lib.EAL.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "MI_Terrain_OldTown", OUT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, lib.load(lib.V21 + "/Materials/M_CS_Terrain"))
    for i, v in enumerate(["Stone_Pavement", "Stone_Cobble", "Ground_ParkDirt", "Ground_Grass"]):
        src = S(v)
        for p in ("Diffuse", "Normal", "ARM"):
            mel.set_material_instance_texture_parameter_value(mi, "%s%d" % (p, i), mel.get_material_instance_texture_parameter_value(src, p))
        mel.set_material_instance_scalar_parameter_value(mi, "Tile%d" % i, mel.get_material_instance_scalar_parameter_value(src, "TileSize"))
    mel.set_material_instance_scalar_parameter_value(mi, "MacroVariation", 0.25)
    lib.EAL.save_loaded_asset(mi, only_if_is_dirty=False)
    return mi


def build_terrain():
    dm, mats, tris = lib.terrain_mesh(-X - 600, X + 600, -Y - 600, Y + 600, 50.0, ground, layers, terrain_material(), skirt=700.0)
    mesh = lib.save_mesh(dm, OUT + "/SM_OldTown_Terrain", mats, collision="complex")
    lib.place(mesh, folder="Terrain", label="Terrain")
    # Distant ground: a ring around the terrain, never under it.
    far = lib.MeshBuilder(ground=kit.NOGRIME)
    F, ix, iy = 40000.0, X + 500.0, Y + 500.0
    grass = S("Ground_Grass")
    for a, b_, c, d in (((-F, -F), (F, -F), (F, -iy), (-F, -iy)), ((-F, iy), (F, iy), (F, F), (-F, F)),
                        ((-F, -iy), (-ix, -iy), (-ix, iy), (-F, iy)), ((ix, -iy), (F, -iy), (F, iy), (ix, iy))):
        z = UP - 20 if a[1] >= iy else -60           # the north ring meets the upper street
        far.quad(a + (z,), b_ + (z,), c + (z,), d + (z,), grass, uv_scale=0.25)
    lib.place(lib.save_mesh(far, OUT + "/SM_OldTown_FarGround", collision="none"), folder="Terrain", label="FarGround", collision=False, shadow=False)
    lib.log("terrain: %d triangles" % tris)


# ---------------------------------------------------------------------------
# Houses: walls with window openings around a solid core, stone plinth, floor
# bands, cornice, gable or flat roof, chimneys, drainpipes, windows with
# shutters, balconies, shop fronts
# ---------------------------------------------------------------------------

def window_row(x0, x1, spacing=290.0, w=110.0):
    n = max(1, int((x1 - x0 - 120) // spacing))
    pad = (x1 - x0 - (n - 1) * spacing) / 2.0
    return [(x0 + pad + i * spacing - w / 2, x0 + pad + i * spacing + w / 2) for i in range(n)]


class House:
    """A closed house. faces: dict side -> dict(shops=[(s, e, kind, sign)], doors=[(s, e)],
    balconies={floor: [(s, e)]}, windows=True/False). side in 'n','s','e','w' (the face that
    looks that way). z0 is the ground at the house's lowest face; base per side may differ
    (a house on a terrace step): bases={'n': 200}."""

    def __init__(self, name, x0, x1, y0, y1, z0, floors, plaster, plinth="Stone_Blocks", roof="gable",
                 roof_mat="Roof_Terracotta", faces=None, bases=None, chimneys=1, tint=None):
        self.name, self.x0, self.x1, self.y0, self.y1, self.z0 = name, x0, x1, y0, y1, z0
        self.floors, self.plaster, self.plinth, self.roof, self.roof_mat = floors, plaster, plinth, roof, roof_mat
        self.faces = faces or {}
        self.bases = bases or {}
        self.chimneys = chimneys
        self.top = z0 + floors * FH

    def base(self, side):
        return self.bases.get(side, self.z0)


def facade_axis(h, side):
    """(wall start, wall end, fixed coordinate of the wall line, outer face coordinate, yaw)."""
    if side == "s":
        return (h.x0, h.x1, h.y0 + T / 2, h.y0, 0.0)
    if side == "n":
        return (h.x0, h.x1, h.y1 - T / 2, h.y1, 180.0)
    if side == "w":
        return (h.y0, h.y1, h.x0 + T / 2, h.x0, -90.0)
    return (h.y0, h.y1, h.x1 - T / 2, h.x1, 90.0)


def at_face(side, along, face, z):
    return (along, face, z) if side in ("s", "n") else (face, along, z)


def behind(b, side, s, e, face, zb, zt, depth=24.0):
    """A slab just behind a door leaf or shop glass, inside the opening."""
    mat = S("Plaster_White")
    if side == "s":
        b.box(s, e, face + depth, face + depth + 4, zb, zt, mat)
    elif side == "n":
        b.box(s, e, face - depth - 4, face - depth, zb, zt, mat)
    elif side == "w":
        b.box(face + depth, face + depth + 4, s, e, zb, zt, mat)
    else:
        b.box(face - depth - 4, face - depth, s, e, zb, zt, mat)


def build_house(b, det, h, r):
    plaster, plinth = S(h.plaster), S(h.plinth)
    band = S("Plaster_White")
    shutter_mat = S("Metal_Shutter")
    for side in ("s", "n", "w", "e"):
        f = h.faces.get(side, {})
        a0, a1, fixed, face, yaw = facade_axis(h, side)
        zb = h.base(side)
        openings, plinth_openings = [], []
        if f.get("windows", True):
            for fl in range(1 if (f.get("shops") or zb > h.z0) else 0, h.floors):
                zf = h.z0 + fl * FH
                if zf < zb:
                    continue
                balc = [tuple(bb) for bb in f.get("balconies", {}).get(fl, [])]
                for s, e in window_row(a0 + 40, a1 - 40, f.get("spacing", 290.0)):
                    if any(bs <= (s + e) / 2 <= be for bs, be in balc):
                        continue
                    openings.append((s, e, zf + 95, zf + 245))
                for bs, be in balc:
                    openings.append((bs, be, zf + 5, zf + 240))
        for s, e, kind, sign in f.get("shops", []):
            openings.append((s, e, zb, zb + 280))
        for s, e in f.get("doors", []):
            openings.append((s, e, zb, zb + 230))
        for s, e, top in f.get("passages", []):
            openings.append((s, e, zb - 50, zb + top))
        horizontal = side in ("s", "n")
        a, c = ((a0, fixed), (a1, fixed)) if horizontal else ((fixed, a0), (fixed, a1))
        kit.wall_openings(b, a, c, zb - 60, zb + 90, T, plinth, openings)
        kit.wall_openings(b, a, c, zb + 90, h.top, T, plaster, openings)
        # Window and balcony units, shutters, shop fronts.
        for s, e, lo, hi in openings:
            mid = (s + e) / 2
            w = e - s
            if hi - lo == 150:
                det.add(kit.window(110, 150), at_face(side, mid, face, lo), yaw, cull=9000)
                if (int(mid) // 7 + int(lo)) % 3 != 0:
                    det.add(kit.shutters(110, 150), at_face(side, mid, face, lo), yaw, cull=7000)
            elif hi - lo == 235:
                det.add(kit.window(w - 20, 220), at_face(side, mid, face, lo + 5), yaw, cull=9000)
        for s, e, kind, sign in f.get("shops", []):
            mid = (s + e) / 2
            if kind == "shutter":
                if horizontal:
                    b.box(s, e, face + (12 if side == "s" else -16), face + (16 if side == "s" else -12), zb, zb + 280, shutter_mat)
                else:
                    b.box(face + (12 if side == "w" else -16), face + (16 if side == "w" else -12), s, e, zb, zb + 280, shutter_mat)
            else:
                det.add(kit.window(e - s - 20, 250, "steel"), at_face(side, mid, face, zb + 10), yaw, cull=9000)
                behind(b, side, s, e, face, zb, zb + 280)
            if sign is not None:
                det.add(kit.sign_panel(), at_face(side, mid, face + ((-6 if side in ("s", "w") else 6)), zb + 300), yaw,
                        scl=(1.6, 1.0, 1.6), materials=[kit.sign_material(kit.SIGN[sign]), None])
        for s, e in f.get("doors", []):
            det.add(kit.door(e - s, 230, "wood", open=False), at_face(side, (s + e) / 2, face, zb), yaw, cull=9000)
            behind(b, side, s, e, face, zb, zb + 230)
        # Balcony slabs with iron railings.
        for fl, spans in f.get("balconies", {}).items():
            zf = h.z0 + fl * FH
            for bs, be in spans:
                out = 110.0
                if side == "s":
                    b.box(bs - 30, be + 30, face - out, face, zf - 15, zf + 5, S("Stone_Blocks"))
                    p = [(bs - 25, face - out + 5), (be + 25, face - out + 5)]
                    ends = [((bs - 25, face), (bs - 25, face - out + 5)), ((be + 25, face - out + 5), (be + 25, face))]
                elif side == "n":
                    b.box(bs - 30, be + 30, face, face + out, zf - 15, zf + 5, S("Stone_Blocks"))
                    p = [(be + 25, face + out - 5), (bs - 25, face + out - 5)]
                    ends = [((be + 25, face), (be + 25, face + out - 5)), ((bs - 25, face + out - 5), (bs - 25, face))]
                elif side == "w":
                    b.box(face - out, face, bs - 30, be + 30, zf - 15, zf + 5, S("Stone_Blocks"))
                    p = [(face - out + 5, be + 25), (face - out + 5, bs - 25)]
                    ends = [((face, be + 25), (face - out + 5, be + 25)), ((face - out + 5, bs - 25), (face, bs - 25))]
                else:
                    b.box(face, face + out, bs - 30, be + 30, zf - 15, zf + 5, S("Stone_Blocks"))
                    p = [(face + out - 5, bs - 25), (face + out - 5, be + 25)]
                    ends = [((face, bs - 25), (face + out - 5, bs - 25)), ((face + out - 5, be + 25), (face, be + 25))]
                iron = kit.railing_segment("iron")
                kit.railing_run(det, (p[0][0], p[0][1], zf + 5), (p[1][0], p[1][1], zf + 5), mesh=iron, collision=False)
                for q0, q1 in ends:
                    kit.railing_run(det, (q0[0], q0[1], zf + 5), (q1[0], q1[1], zf + 5), mesh=iron, collision=False)
                if r.random() < 0.6:
                    det.add(lib.prop("potted_plant_04"), at_face(side, bs + 30, face + (-60 if side in ("s", "w") else 60), zf + 5),
                            r.uniform(0, 360), 0.8, cull=5000)
    # Solid core: collision, and nothing behind the glass.
    b.box(h.x0 + T, h.x1 - T, h.y0 + T, h.y1 - T, min(h.base(s_) for s_ in "snwe") - 60, h.top, S("Plaster_White"), faces="t")
    # Floor bands and cornice.
    for fl in range(1, h.floors):
        zf = h.z0 + fl * FH
        if zf > max(h.base(s_) for s_ in "snwe"):
            b.box(h.x0 - 6, h.x1 + 6, h.y0 - 6, h.y1 + 6, zf - 12, zf, band, faces="nsew")
    b.box(h.x0 - 25, h.x1 + 25, h.y0 - 25, h.y1 + 25, h.top - 30, h.top, band, faces="nsewb")
    # Roof.
    if h.roof == "gable":
        along_x = (h.x1 - h.x0) >= (h.y1 - h.y0)
        span = (h.y1 - h.y0) if along_x else (h.x1 - h.x0)
        rise = span * 0.32
        half = span / 2 + 45
        pitch = math.degrees(math.atan2(rise, span / 2))
        slope_len = half / math.cos(math.radians(pitch))
        length = ((h.x1 - h.x0) if along_x else (h.y1 - h.y0)) + 90
        roof = S(h.roof_mat)
        cx, cy = (h.x0 + h.x1) / 2, (h.y0 + h.y1) / 2
        for sgn in (-1, 1):
            zc = h.top + rise - (half / 2) * math.tan(math.radians(pitch)) + 6
            if along_x:
                b.obox((cx, cy + sgn * half / 2, zc), (slope_len, length, 12), -90 * sgn, roof, pitch=pitch)
            else:
                b.obox((cx + sgn * half / 2, cy, zc), (slope_len, length, 12), 90 - 90 * sgn, roof, pitch=pitch)
        # Gable triangles.
        if along_x:
            for x, outward in ((h.x0, False), (h.x1, True)):
                pts = [(x, h.y0, h.top), (x, h.y1, h.top), (x, cy, h.top + rise)]
                b.poly(pts if outward else list(reversed(pts)), plaster)
            b.box(h.x0 - 45, h.x1 + 45, cy - 10, cy + 10, h.top + rise, h.top + rise + 14, roof)
        else:
            for y, outward in ((h.y0, True), (h.y1, False)):
                pts = [(h.x0, y, h.top), (h.x1, y, h.top), (cx, y, h.top + rise)]
                b.poly(pts if outward else list(reversed(pts)), plaster)
            b.box(cx - 10, cx + 10, h.y0 - 45, h.y1 + 45, h.top + rise, h.top + rise + 14, roof)
        roof_top = h.top + rise
    else:
        # Flat roof with a parapet.
        for (a, c) in (((h.x0, h.y0 + 12), (h.x1, h.y0 + 12)), ((h.x0, h.y1 - 12), (h.x1, h.y1 - 12))):
            kit.wall_openings(b, a, c, h.top, h.top + 100, 24, band, [])
        for (a, c) in (((h.x0 + 12, h.y0), (h.x0 + 12, h.y1)), ((h.x1 - 12, h.y0), (h.x1 - 12, h.y1))):
            kit.wall_openings(b, a, c, h.top, h.top + 100, 24, band, [])
        roof_top = h.top
    # Chimneys and drainpipes.
    for i in range(h.chimneys):
        cx = h.x0 + (h.x1 - h.x0) * (0.25 + 0.5 * i / max(1, h.chimneys - 1)) if h.chimneys > 1 else h.x0 + (h.x1 - h.x0) * 0.3
        cy = h.y0 + (h.y1 - h.y0) * 0.35
        b.box(cx - 30, cx + 30, cy - 30, cy + 30, h.top, roof_top + 110, S("Brick_Mixed"))
        b.box(cx - 38, cx + 38, cy - 38, cy + 38, roof_top + 110, roof_top + 122, S("Stone_Blocks"))
    pipe = S("Metal_Rusty")
    for px, py in ((h.x0 - 8, h.y0 - 8), (h.x1 + 8, h.y1 + 8)):
        kit.cyl(b, (px, py, min(h.base(s_) for s_ in "snwe")), (px, py, h.top - 30), 6, pipe, seg=8)
    # A couple of air conditioners on a side face.
    for i in range(r.randint(0, 2)):
        zf = h.z0 + (1 + i) * FH + 40
        if zf < h.top - 100:
            det.add(lib.prop("exterior_aircon_unit"), (h.x1 + 20, h.y0 + 150 + i * 220, zf), 90, cull=8000)


# ---------------------------------------------------------------------------
# The quarter
# ---------------------------------------------------------------------------

def houses():
    H = []
    # South row: the backdrop of the lower street, shops on the ground floor, facades north.
    south = [(-4500, -2600, 3, "Plaster_Ochre", [(-4200, -3700, "shutter", "tabacchi"), (-3300, -2900, "glass", None)]),
             (-2600, -800, 4, "Plaster_Pink", [(-2400, -1900, "glass", "farmacia"), (-1500, -1100, "shutter", None)]),
             (-800, 1000, 3, "Plaster_Yellow", [(-600, -100, "glass", "caffe"), (300, 800, "shutter", "panificio")]),
             (1000, 2800, 3, "Plaster_Blue", [(1300, 1800, "glass", "alimentari"), (2100, 2500, "shutter", None)]),
             (2800, 4500, 2, "Plaster_Red", [(3100, 3600, "glass", "bar"), (3900, 4300, "shutter", None)])]
    for i, (x0, x1, fl, pl, shops) in enumerate(south):
        # Two of them step forward into the street: the street is never one 90 m tube.
        y1 = -2940.0 if i in (0, 4) else -3500.0
        H.append(House("South%d" % i, x0, x1, -4000, y1, LOW, fl, pl, faces={
            "n": dict(shops=shops, balconies={2: [(x0 + 600, x0 + 820)]} if fl >= 3 else {}),
            "e": dict(windows=i in (0, 4)), "w": dict(windows=i in (0, 4))}, chimneys=2))
    # Mid band between the lower street and the square: fronts on the street at 0, backs on the square at 200.
    H.append(House("MidWest", -4500, -3000, -2500, -2000, LOW, 3, "Plaster_Damaged", faces={
        "s": dict(passages=[(-3900, -3500, 330)], doors=[(-4300, -4200)]), "n": dict(passages=[(-3900, -3500, 330)])}))
    H.append(House("MidA", -2900, -1300, -2500, -1000, LOW, 3, "Brick_Mixed", plinth="Stone_Blocks", bases={"n": SQ}, faces={
        "s": dict(shops=[(-2600, -2100, "shutter", None)], balconies={2: [(-1900, -1650)]}), "n": dict(doors=[(-2200, -2100)])}))
    H.append(House("MidB", -300, 1500, -2500, -1300, LOW, 3, "Plaster_Worn", roof="flat", bases={"n": SQ}, faces={
        "s": dict(shops=[(100, 600, "glass", "via")], balconies={1: [(900, 1150)], 2: [(-100, 150)]}), "n": dict(windows=True)}))
    H.append(House("MidC", 1500, 3000, -2500, -1000, LOW, 3, "Plaster_Ochre", bases={"n": SQ}, faces={
        "s": dict(doors=[(1800, 1900)], shops=[(2200, 2700, "shutter", None)]), "e": dict(doors=[(-1800, -1700)])}))
    H.append(House("EastSide", ALLEY_X1, 4500, -2500, -1100, LOW, 3, "Plaster_Pink", bases={"n": SQ}, faces={
        "w": dict(balconies={2: [(-1800, -1550)]})}, chimneys=2))
    # West: the gallery building and a small house by courtyard 2.
    H.append(House("Gallery", -4400, -3150, 200, 2300, SQ, 3, "Plaster_Yellow", faces={
        "e": dict(doors=[(600, 700)]), "s": dict(doors=[(-3900, -3800)])}, chimneys=2))
    H.append(House("LaneHouse", 1700, 2700, -1000, -400, SQ, 2, "Plaster_Yellow", faces={"n": dict(doors=[(2100, 2200)])}))
    H.append(House("CourtWest1", -4500, -4250, -2000, -1250, LOW, 3, "Plaster_Ochre", roof="flat", faces={
        "e": dict(doors=[(-1700, -1600)], balconies={2: [(-1500, -1300)]}), "n": dict(windows=False), "s": dict(windows=False),
        "w": dict(windows=False)}, chimneys=1))
    H.append(House("CourtWest2", -4500, -4250, -1250, 200, SQ, 3, "Plaster_Pink", faces={
        "e": dict(doors=[(-400, -300)], balconies={1: [(-1000, -800)]}), "n": dict(windows=False), "s": dict(windows=False),
        "w": dict(windows=False)}, chimneys=2))
    H.append(House("CourtHouse", -2400, -1850, -1250, -900, SQ, 2, "Plaster_Blue", faces={"n": dict(doors=[(-2200, -2100)])}))
    # North row above the upper street, balconies over it.
    north = [(-4500, -2700, 3, "Plaster_Red"), (-2700, -1100, 3, "Plaster_White"), (-1100, 800, 4, "Plaster_Ochre"),
             (800, 2600, 3, "Plaster_Pink"), (2600, 4500, 3, "Plaster_Yellow")]
    for i, (x0, x1, fl, pl) in enumerate(north):
        y0 = 2950.0 if i in (0, 4) else (2700.0 if i == 2 else 3400.0)
        H.append(House("North%d" % i, x0, x1, y0, 4000, UP, fl, pl, faces={
            "s": dict(doors=[((x0 + x1) / 2 - 50, (x0 + x1) / 2 + 50)], balconies={1: [(x0 + 300, x0 + 560)], 2: [(x1 - 700, x1 - 440)]}),
            "e": dict(windows=i in (0, 4)), "w": dict(windows=i in (0, 4))}, roof="flat" if i == 2 else "gable", chimneys=2))
    return H


def build_houses(det):
    b = lib.MeshBuilder(ground=ground)
    r = lib.rng(7)
    for h in houses():
        build_house(b, det, h, r)
    lib.place(lib.save_mesh(b, OUT + "/SM_OldTown_Houses", collision="complex"), folder="Houses", label="Houses")
    lib.log("houses: %d triangles" % b.triangles)


def build_palazzo(det):
    """Palazzo on the west of the square: three floors, rusticated plinth, an open arcade on the
    square side (walkable, cover), balustraded roof terrace."""
    b = lib.MeshBuilder(ground=ground)
    x0, x1, y0, y1 = -2800.0, -1800.0, 200.0, 2360.0
    z0, floors = SQ, 3
    top = z0 + floors * FH + 60
    stone, plaster, band = S("Stone_Blocks"), S("Plaster_Ochre"), S("Plaster_White")
    ax = x1 - 320.0                                    # the arcade's back wall
    # Back, north, south, west walls (closed), core behind the arcade.
    kit.wall_openings(b, (x0, y0 + T / 2), (x1, y0 + T / 2), z0 - 60, top, T, plaster, [(x, x + 110, z0 + FH * f + 95, z0 + FH * f + 245)
                      for f in (1, 2) for x in (-2650, -2350)])
    kit.wall_openings(b, (x0, y1 - T / 2), (x1, y1 - T / 2), z0 - 60, top, T, plaster, [(x, x + 110, z0 + FH * f + 95, z0 + FH * f + 245)
                      for f in (1, 2) for x in (-2650, -2350)])
    kit.wall_openings(b, (x0 + T / 2, y0), (x0 + T / 2, y1), z0 - 60, top, T, plaster, [(y, y + 110, z0 + FH * f + 95, z0 + FH * f + 245)
                      for f in (1, 2) for y in range(400, 1700, 300)])
    kit.wall_openings(b, (ax, y0), (ax, y1), z0 - 60, top, T, stone, [(y, y + 110, z0 + 95, z0 + 245) for y in range(450, 1650, 400)])
    b.box(x0 + T, ax - T / 2, y0 + T, y1 - T, z0 - 60, top, band, faces="t")
    # Arcade: piers and arches on the square side, the upper floors carried above.
    piers = [y0 + 20 + i * (y1 - y0 - 40) / 6 for i in range(7)]
    for py in piers:
        b.box(x1 - 60, x1, py - 30, py + 30, z0, z0 + FH, stone)
    for p0, p1 in zip(piers, piers[1:]):
        # Arch: a stepped ring of voussoir boxes between the piers.
        mid, rad = (p0 + p1) / 2, (p1 - p0) / 2 - 30
        for k in range(8):
            a0, a1 = math.pi * k / 8, math.pi * (k + 1) / 8
            ya, yb = mid - rad * math.cos(a0), mid - rad * math.cos(a1)
            za = z0 + FH - 110 + rad * 0.55 * math.sin((a0 + a1) / 2)
            b.box(x1 - 60, x1, min(ya, yb), max(ya, yb), za, z0 + FH, stone)
    b.box(ax, x1, y0 + T, y1 - T, z0 + FH, z0 + FH + 20, stone)                # arcade ceiling / first floor
    b.box(ax, x1, y0 + T, y1 - T, z0 - 5, z0 + 5, S("Stone_Tiles"), faces="t")  # arcade floor
    kit.wall_openings(b, (x1 - 20, y0), (x1 - 20, y1), z0 + FH + 20, top, 40, plaster,
                      [(y, y + 110, z0 + FH * f + 95, z0 + FH * f + 245) for f in (1, 2) for y in range(400, 1700, 300)])
    # Floor bands, cornice, roof terrace balustrade.
    for f in (1, 2):
        b.box(x0 - 8, x1 + 8, y0 - 8, y1 + 8, z0 + FH * f - 14, z0 + FH * f, band, faces="nsew")
    b.box(x0 - 30, x1 + 30, y0 - 30, y1 + 30, top - 36, top, band, faces="nsewb")
    for (a, c) in (((x0, y0 + 12), (x1, y0 + 12)), ((x0, y1 - 12), (x1, y1 - 12)), ((x0 + 12, y0), (x0 + 12, y1)), ((x1 - 12, y0), (x1 - 12, y1))):
        kit.wall_openings(b, a, c, top, top + 90, 24, band, [])
    lib.place(lib.save_mesh(b, OUT + "/SM_OldTown_Palazzo", collision="complex"), folder="Palazzo", label="Palazzo")
    for f in (1, 2):
        for y in range(400, 1700, 300):
            det.add(kit.window(110, 150), (x1, y + 55, z0 + FH * f + 95), 90, cull=9000)
            det.add(kit.window(110, 150), (x0, y + 55, z0 + FH * f + 95), -90, cull=9000)
        for x in (-2650, -2350):
            det.add(kit.window(110, 150), (x + 55, y0, z0 + FH * f + 95), 0, cull=9000)
            det.add(kit.window(110, 150), (x + 55, y1, z0 + FH * f + 95), 180, cull=9000)
    for y in range(450, 1650, 400):
        det.add(kit.window(110, 150), (ax + T / 2, y + 55, z0 + 95), 90, cull=9000)
    for py in piers[:-1]:
        det.add(lib.prop("hanging_industrial_lamp"), (x1 - 160, py + 160, z0 + FH - 130), 0, cull=6000)
    lib.reverb(ax, x1, y0, y1, z0, z0 + FH, "RE_Room", 0.45)
    # Cafe tables in the arcade.
    r = lib.rng(11)
    for py in piers[1:-1:2]:
        det.add(kit.crate(80, 80, 75), (x1 - 170, py, z0 + 5), r.uniform(0, 90), collision=True)


def build_church(det):
    """Church on the east of the square: hollow nave with doors to the square and to the lane,
    pews, a bell tower (landmark, 20 m) with open belfry arches."""
    b = lib.MeshBuilder(ground=ground)
    x0, x1, y0, y1 = 1700.0, 2800.0, 300.0, 1500.0
    z0 = SQ
    wall_h = 900.0
    stone, plaster = S("Stone_Blocks"), S("Plaster_White")
    t = 60.0
    kit.wall_openings(b, (x0 + t / 2, y0), (x0 + t / 2, y1), z0 - 60, z0 + wall_h, t, stone,
                      [(820, 980, z0, z0 + 330), (500, 620, z0 + 420, z0 + 700), (1180, 1300, z0 + 420, z0 + 700)])
    kit.wall_openings(b, (x1 - t / 2, y0), (x1 - t / 2, y1), z0 - 60, z0 + wall_h, t, stone, [(700, 820, z0 + 420, z0 + 700)])
    kit.wall_openings(b, (x0, y0 + t / 2), (x1, y0 + t / 2), z0 - 60, z0 + wall_h, t, stone,
                      [(2150, 2290, z0, z0 + 260), (1900, 2000, z0 + 420, z0 + 700), (2450, 2550, z0 + 420, z0 + 700)])
    kit.wall_openings(b, (x0, y1 - t / 2), (x1, y1 - t / 2), z0 - 60, z0 + wall_h, t, stone, [])
    b.box(x0 + t, x1 - t, y0 + t, y1 - t, z0 - 5, z0 + 5, S("Stone_Tiles"), faces="t")
    # Gable roof along X... the nave runs E-W; a pediment on the square facade.
    span = y1 - y0
    rise = 380.0
    half = span / 2 + 40
    pitch = math.degrees(math.atan2(rise, span / 2))
    slope_len = half / math.cos(math.radians(pitch))
    cy = (y0 + y1) / 2
    for sgn in (-1, 1):
        zc = z0 + wall_h + rise - (half / 2) * math.tan(math.radians(pitch)) + 6
        b.obox(((x0 + x1) / 2, cy + sgn * half / 2, zc), (slope_len, x1 - x0 + 80, 12), -90 * sgn, S("Roof_Clay"), pitch=pitch)
    for x, outward in ((x0, False), (x1, True)):
        pts = [(x, y0, z0 + wall_h), (x, y1, z0 + wall_h), (x, cy, z0 + wall_h + rise)]
        b.poly(pts if outward else list(reversed(pts)), stone)
    b.box(x0 - 40, x0, y0 - 20, y1 + 20, z0 + wall_h - 30, z0 + wall_h, plaster)
    # Interior: pews and an altar, a wooden beam ceiling.
    for yy in range(450, 1400, 220):
        if 780 < yy < 1020:
            continue
        for xx in range(1900, 2500, 150):
            b.box(xx, xx + 90, yy - 60, yy + 60, z0, z0 + 45, S("Wood_Dark"))
            b.box(xx + 80, xx + 90, yy - 60, yy + 60, z0 + 45, z0 + 95, S("Wood_Dark"))
    b.box(2600, 2720, 700, 1100, z0, z0 + 110, stone)
    b.box(1860, 1890, 760, 1040, z0, z0 + 260, S("Wood_Dark"))                  # inner porch screen at the west door
    for xx in range(1800, 2800, 200):
        b.box(xx - 12, xx + 12, y0 + t, y1 - t, z0 + wall_h - 40, z0 + wall_h - 10, S("Wood_Dark"))
    # Bell tower at the north-east corner.
    b.box(2350, 2800, 1500, 1900, z0 - 60, z0 + 700, stone)                     # sacristy
    b.box(2340, 2810, 1490, 1910, z0 + 700, z0 + 720, S("Roof_Clay"))
    tx0, tx1, ty0, ty1 = 2350.0, 2800.0, 1900.0, 2350.0
    bt = z0 + 1500.0
    b.box(tx0, tx1, ty0, ty1, z0 - 60, bt, stone)
    for k in range(4):
        zz = z0 + 300 + k * 300
        b.box(tx0 - 8, tx1 + 8, ty0 - 8, ty1 + 8, zz, zz + 14, plaster, faces="nsew")
    # Belfry: four piers and a pyramid roof, open arches.
    for px, py in ((tx0, ty0), (tx1 - 70, ty0), (tx0, ty1 - 70), (tx1 - 70, ty1 - 70)):
        b.box(px, px + 70, py, py + 70, bt, bt + 320, stone)
    b.box(tx0 - 20, tx1 + 20, ty0 - 20, ty1 + 20, bt + 320, bt + 360, plaster)
    kit.cyl(b, ((tx0 + tx1) / 2, (ty0 + ty1) / 2, bt + 360), ((tx0 + tx1) / 2, (ty0 + ty1) / 2, bt + 620), 320, S("Roof_Clay"), seg=4, r1=10)
    kit.cyl(b, ((tx0 + tx1) / 2, (ty0 + ty1) / 2, bt + 60), ((tx0 + tx1) / 2, (ty0 + ty1) / 2, bt + 200), 70, S("Metal_Rusty"), seg=12, r1=45)
    lib.place(lib.save_mesh(b, OUT + "/SM_OldTown_Church", collision="complex"), folder="Church", label="Church")
    det.add(kit.door(160, 330, "wood"), (x0, 900, z0), -90, cull=9000)
    det.add(kit.door(140, 260, "wood"), (2220, y0, z0), 0, cull=9000)
    lib.reverb(x0, x1, y0, y1, z0, z0 + wall_h, "RE_Hall", 0.7)
    lib.point_light((2200, 900, z0 + 600), intensity=900, radius=1200, color=(1.0, 0.85, 0.6))


def build_square(det):
    """Square at +2 m: fountain (landmark and cover), loggia on MidB's roof edge, plane trees,
    benches, lamps; balustraded retaining wall to the upper street with the grand stair."""
    b = lib.MeshBuilder(ground=ground)
    stone = S("Stone_Blocks")
    # Fountain: octagonal basin, column, bowl.
    fx, fy = 150.0, 700.0
    kit.cyl(b, (fx, fy, SQ - 10), (fx, fy, SQ + 70), 330, stone, seg=8)
    kit.cyl(b, (fx, fy, SQ + 70), (fx, fy, SQ + 75), 300, S("Metal_Plain"), seg=8)
    kit.cyl(b, (fx, fy, SQ + 70), (fx, fy, SQ + 340), 60, stone, seg=12)
    kit.cyl(b, (fx, fy, SQ + 340), (fx, fy, SQ + 385), 150, stone, seg=12, r1=175)
    kit.cyl(b, (fx, fy, SQ + 385), (fx, fy, SQ + 470), 30, stone, seg=10)
    b.box(fx - 35, fx + 35, fy - 30, fy + 30, SQ + 470, SQ + 600, S("Metal_Rusty"))
    # Retaining wall with a balustrade along y 2400, gaps: grand stair, east alley.
    wall_gaps = [(400, 1000), (ALLEY_X0, ALLEY_X1)]
    x = -2800.0
    for g0, g1 in wall_gaps + [(4500, 4500)]:
        if g0 - x > 1:
            b.box(x, g0, 2400 - 40, 2400 + 40, SQ - 60, UP, stone)
            b.box(x, g0, 2400 - 50, 2400 + 50, UP, UP + 12, S("Plaster_White"))
            b.box(x, g0, 2400 - 30, 2400 + 30, UP + 12, UP + 25, S("Plaster_White"))
            bx = x + 30
            while bx < g0 - 20:
                kit.cyl(b, (bx, 2400, UP + 25), (bx, 2400, UP + 95), 9, S("Plaster_White"), seg=8)
                bx += 28
            b.box(x, g0, 2400 - 35, 2400 + 35, UP + 95, UP + 108, S("Plaster_White"))
        x = g1
    # Wide stair from the lower street (0) to the square (200) between MidA and MidB.
    n = int(round(SQ / 17.5))
    kit.stair_flight(b, -800, SQ_EDGE - n * 28, LOW, SQ, 800, 90, stone)
    landing(b, -1200, -400, SQ_EDGE, SQ_EDGE + 80, SQ, stone)
    for x0, x1 in ((-1300, -1200), (-400, -300)):
        b.box(x0, x1, SQ_EDGE - n * 28 - 20, SQ_EDGE + 20, LOW - 60, SQ + 90, stone)
    # Grand stair: 250 rise, landing halfway.
    kit.stair_flight(b, 700, 2400 - 14 * 28 - 200, SQ, SQ + 122.5, 560, 90, stone)
    b.box(420, 980, 2400 - 7 * 28 - 200, 2400 - 7 * 28, SQ, SQ + 122.5, stone)
    kit.stair_flight(b, 700, 2400 - 7 * 28, SQ + 122.5, UP, 560, 90, stone)
    for sx in (410, 990):
        b.box(sx - 15, sx + 15, 2400 - 14 * 28 - 200, 2400, SQ, SQ + 90, stone)
    landing(b, 420, 980, 2400, 2480, UP, stone)
    # Courtyard 2 meets the square through one arched gate.
    b.box(-2440, -2400, -900, -870, SQ - 60, SQ + 420, S("Plaster_Damaged"))
    b.box(-2440, -2400, -620, 200, SQ - 60, SQ + 420, S("Plaster_Damaged"))
    b.box(-2450, -2390, -870, -620, SQ + 300, SQ + 420, stone)
    # Market stalls: tables of crates under canvas on posts.
    canvas = [kit.paint("Paint_Canvas%d" % i, c, rough=0.9, dirt=0.3) for i, c in enumerate(((0.55, 0.12, 0.08), (0.12, 0.3, 0.14), (0.6, 0.5, 0.2)))]
    for i, (cx, cy) in enumerate(((-900, 1700), (1150, 120), (-1000, -400))):
        for dx in (-160, 160):
            for dy in (-110, 110):
                b.box(cx + dx - 5, cx + dx + 5, cy + dy - 5, cy + dy + 5, SQ, SQ + 240, S("Wood_Dark"))
        b.obox((cx, cy, SQ + 245), (380, 280, 4), 0, canvas[i], pitch=8)
        b.box(cx - 150, cx + 150, cy - 90, cy + 90, SQ, SQ + 95, S("Wood_Planks"))
    # A wayside shrine with a well in the lane to the east alley.
    b.box(2650, 2900, -350, -150, SQ - 10, SQ + 260, stone)
    b.box(2630, 2920, -370, -130, SQ + 260, SQ + 285, S("Roof_Terracotta"))
    # Column monument (landmark, cover): plinth, column, statue block.
    b.box(630, 870, -520, 40, SQ - 10, SQ + 200, stone)
    kit.cyl(b, (750, -200, SQ + 200), (750, -200, SQ + 650), 40, S("Stone_Blocks"), seg=12)
    b.box(700, 800, -250, -150, SQ + 650, SQ + 780, S("Metal_Rusty"))
    # A memorial wall behind the Porta: the arch opens onto it, not onto the square.
    b.box(-1100, -500, -950, -910, SQ - 10, SQ + 330, stone)
    b.box(-1110, -490, -960, -900, SQ + 330, SQ + 350, S("Plaster_White"))
    # Porta: a gate wall at the head of the wide stair, the arch off the stair's axis.
    b.box(-1300, -950, -1150, -1110, SQ - 60, SQ + 560, stone)
    b.box(-650, -300, -1150, -1110, SQ - 60, SQ + 560, stone)
    b.box(-950, -650, -1150, -1110, SQ + 340, SQ + 560, stone)
    b.box(-1320, -280, -1165, -1095, SQ + 560, SQ + 590, S("Plaster_White"))
    kit.cyl(b, (2000, 0, SQ - 10), (2000, 0, SQ + 90), 90, stone, seg=12)
    # Low walls and planters as cover on the square.
    for x0, x1, y0, y1 in ((-1500, -900, 1200, 1300), (700, 1300, 1200, 1300), (-1700, -1600, 100, 600), (1450, 1550, 0, 500)):
        b.box(x0, x1, y0, y1, SQ - 5, SQ + 85, stone)
        b.box(x0 + 10, x1 - 10, y0 + 10, y1 - 10, SQ + 85, SQ + 90, S("Ground_ParkDirt"), faces="t")
    # Loggia: a roof on columns along MidB's square side (cafe terrace, cover).
    for xx in range(-250, 1500, 350):
        kit.cyl(b, (xx, -1250, SQ), (xx, -1250, SQ + 330), 22, stone, seg=12)
    b.box(-300, 1500, -1300, -1000, SQ + 330, SQ + 360, S("Plaster_White"))
    b.box(-300, -180, -1300, -1000, SQ - 60, SQ + 330, S("Plaster_Worn"))        # the loggia's closed west end
    # Stone pillars with urns on the balustrade: nobody sees along it over the parapet.
    for px in (-2600, -2000, -1550, -1000, -500, 0, 410, 990, 1250, 1500, 1800, 2100, 2400, 2650):
        b.box(px - 30, px + 30, 2345, 2455, UP - 10, UP + 300, stone)
        b.box(px - 38, px + 38, 2337, 2463, UP + 300, UP + 318, S("Plaster_White"))
        kit.cyl(b, (px, 2400, UP + 318), (px, 2400, UP + 380), 26, stone, seg=10, r1=14)
    b.box(-300, 1500, -1300, -1000, SQ + 360, SQ + 372, S("Roof_Terracotta"))
    lib.place(lib.save_mesh(b, OUT + "/SM_OldTown_Square", collision="complex"), folder="Square", label="Square")
    r = lib.rng(21)
    for xx in range(-100, 1400, 350):
        det.add(kit.crate(80, 80, 75), (xx + 170, -1150, SQ), r.uniform(0, 30), collision=True)
    for tx, ty in TREES:
        det.add(kit.tree(r.randint(0, 2)), (tx, ty, SQ - 5), r.uniform(0, 360), r.uniform(1.0, 1.2), collision=True, cull=0)
    for x, y, yaw in ((-900, 400, 90), (900, 400, -90), (-300, 1500, 0), (600, -700, 180)):
        det.add(old_prop("painted_wooden_bench"), (x, y, SQ), yaw, collision=True)
    for x, y in ((-1700, -900), (1500, -900), (-1700, 2250), (1400, 2250)):
        det.add(lib.prop("street_lamp_01"), (x, y, SQ), r.uniform(0, 360), collision=True, cull=0)
        lib.point_light((x, y, SQ + 400), intensity=400, radius=900, color=(1.0, 0.85, 0.6))
    lib.decal(lib.WET, (fx, fy - 420, SQ), (80, 300, 200), yaw=0, roughness=0.08, opacity=0.8)


def old_prop(name):
    return lib.load("/Game/Environment/Props/%s/SM_%s" % (name, name))


def build_alley(det):
    """East stepped alley: flight to the square level, a long lane with a side lane to the square,
    flight to the upper street; walls, a garden with a shed, a fire escape."""
    b = lib.MeshBuilder(ground=ground)
    stone, plaster = S("Stone_Blocks"), S("Plaster_Worn")
    xc = (ALLEY_X0 + ALLEY_X1) / 2
    w = ALLEY_X1 - ALLEY_X0 - 40
    kit.stair_flight(b, xc, FL1_Y0, LOW, SQ, w, 90, stone)
    kit.stair_flight(b, xc, FL2_Y0, SQ, UP, w, 90, stone)
    landing(b, ALLEY_X0 + 20, ALLEY_X1 - 20, FL1_Y1, FL1_Y1 + 80, SQ, stone)
    landing(b, ALLEY_X0 + 20, ALLEY_X1 - 20, FL2_Y1, FL2_Y1 + 80, UP, stone)
    for xx in (ALLEY_X0 + 15, ALLEY_X1 - 15):
        kit.railing_run(det, (xx, FL1_Y0, LOW), (xx, FL1_Y1, SQ), mesh=kit.railing_segment("iron"))
        kit.railing_run(det, (xx, FL2_Y0, SQ), (xx, FL2_Y1, UP), mesh=kit.railing_segment("iron"))
    # Walls: the alley's west side from the church lane to the upper street (square level to 450),
    # the east side along the garden.
    b.box(ALLEY_X0 - 40, ALLEY_X0, 1500, 2400, SQ - 60, UP + 260, plaster)
    b.box(ALLEY_X0 - 40, ALLEY_X0, -1000, -150, SQ - 60, SQ + 250, plaster)
    b.box(ALLEY_X0 - 40, ALLEY_X0, 150, 1500, SQ - 60, SQ + 250, plaster)
    b.box(ALLEY_X0 - 60, ALLEY_X0 + 20, -150, 150, SQ + 230, SQ + 280, stone)        # lintel over the gate
    b.box(ALLEY_X1, ALLEY_X1 + 40, -1100, -1000, SQ - 60, SQ + 250, plaster)
    b.box(ALLEY_X1, ALLEY_X1 + 40, 1000, 2400, SQ - 60, UP + 100, plaster)
    # Garden: low wall with a gate, a shed, trees, grass.
    b.box(ALLEY_X1, ALLEY_X1 + 30, -1000, 500, SQ - 20, SQ + 220, stone)
    b.box(ALLEY_X1, ALLEY_X1 + 30, 800, 1000, SQ - 20, SQ + 220, stone)
    b.box(ALLEY_X1, 4450, 985, 1015, SQ - 20, SQ + 110, stone)
    b.box(ALLEY_X1, 4450, -1015, -985, SQ - 20, SQ + 110, stone)
    gx0, gx1, gy0, gy1 = 4000.0, 4400.0, 400.0, 900.0
    kit.wall_openings(b, (gx0, gy0), (gx1, gy0), SQ, SQ + 240, 12, S("Wood_Planks"), [(4150, 4250, SQ, SQ + 200)])
    kit.wall_openings(b, (gx0, gy1), (gx1, gy1), SQ, SQ + 240, 12, S("Wood_Planks"), [])
    kit.wall_openings(b, (gx0, gy0), (gx0, gy1), SQ, SQ + 240, 12, S("Wood_Planks"), [])
    kit.wall_openings(b, (gx1, gy0), (gx1, gy1), SQ, SQ + 240, 12, S("Wood_Planks"), [])
    b.obox(((gx0 + gx1) / 2, (gy0 + gy1) / 2, SQ + 260), (gx1 - gx0 + 60, gy1 - gy0 + 60, 10), 0, S("Metal_Corrugated"), pitch=6)
    lib.place(lib.save_mesh(b, OUT + "/SM_OldTown_Alley", collision="complex"), folder="Alley", label="Alley")
    # Gate gap in the garden wall is where the wall is not (y -200..200 at the alley).
    r = lib.rng(31)
    for x, y in ((3950, -600), (4200, 0), (3900, 600)):
        det.add(kit.tree(r.randint(0, 2)), (x, y, SQ - 5), r.uniform(0, 360), r.uniform(0.8, 1.0), collision=True, cull=0)
    kit.grass_patch(det, r, 4100, 0, 500, 4, ground, keep=lambda x, y: layers(x, y)[2] > 0.5 and not (gx0 < x < gx1 and gy0 < y < gy1))
    for x, y in ((3350, -1500), (3450, 300), (3250, 1200)):
        det.add(lib.prop("metal_trash_can"), (x, y, ground(x, y)), r.uniform(-10, 10), collision=True)
    # Fire escape on the east side house (decor: landings and ladders on the wall).
    iron = kit.paint("Paint_Iron", (0.02, 0.02, 0.022), rough=0.6)
    fe = lib.MeshBuilder(ground=ground)
    for k in range(1, 3):
        zf = LOW + k * FH
        fe.box(ALLEY_X1 + 5, ALLEY_X1 + 125, -2200, -1800, zf - 6, zf, iron)
        kit.ladder(fe, ALLEY_X1 + 60, -1850, zf - FH + 20, zf, 90, iron)
    lib.place(lib.save_mesh(fe, OUT + "/SM_OldTown_FireEscape", collision="none"), folder="Alley", label="FireEscape", collision=False)
    lib.reverb(ALLEY_X0, ALLEY_X1, -2500, 2400, LOW, UP + 400, "RE_Alley", 0.4)


def build_west(det):
    """Courtyards: arch from the lower street, courtyard 1 (0) with laundry and bins, stair to
    courtyard 2 (200), external stair to the gallery (+450) and on to the upper street."""
    b = lib.MeshBuilder(ground=ground)
    stone, plaster = S("Stone_Blocks"), S("Plaster_Damaged")
    # The arch through MidWest: a vaulted tunnel, closed off from the house around it.
    for tx0, tx1 in ((-3920, -3900), (-3500, -3480)):
        b.box(tx0, tx1, -2500, -2000, LOW - 20, LOW + 350, stone)
    b.box(-3920, -3480, -2500, -2000, LOW + 330, LOW + 350, stone)
    for k in range(6):
        a0, a1 = math.pi * k / 6, math.pi * (k + 1) / 6
        xa, xb_ = -3700 - 200 * math.cos(a0), -3700 - 200 * math.cos(a1)
        b.box(min(xa, xb_), max(xa, xb_), -2500, -2000, LOW + 250 + 70 * math.sin((a0 + a1) / 2), LOW + 330, stone)
    # Retaining wall between the courtyards with a stair.
    b.box(-4450, -3300, -1290, -1250, LOW - 60, SQ + 100, stone)
    kit.stair_flight(b, -3100, -1250 - 11 * 28, LOW, SQ, 280, 90, stone)
    landing(b, -3240, -2960, -1250, -1170, SQ, stone)
    b.box(-2960, -2920, -1600, -1250, LOW - 60, SQ + 100, stone)
    # Courtyard 1 walls (west, and the side toward MidA).
    b.box(-3000, -2920, -2000, -1600, LOW - 60, LOW + 600, plaster)
    # Courtyard 2 west wall.
    # External stair up to the gallery: along the lane between the gallery building and the palazzo.
    lx0, lx1 = -3150.0, -2800.0
    kit.stair_flight(b, (lx0 + lx1) / 2, 100, SQ, UP, 300, 90, stone, solid=True)
    top_y = 100 + int(round((UP - SQ) / 17.5)) * 28
    gallery = S("Wood_Planks")
    b.box(lx0, lx1, top_y, 2400, UP - 20, UP, gallery)                              # gallery deck
    for yy in range(int(top_y) + 200, 2400, 400):
        for xx in (lx0 + 15, lx1 - 15):
            b.box(xx - 10, xx + 10, yy - 10, yy + 10, SQ, UP - 20, S("Wood_Dark"))
    kit.railing_run(det, (lx1 - 5, top_y, UP), (lx1 - 5, 2400, UP), mesh=kit.railing_segment("iron"))
    kit.railing_run(det, (lx1 - 5, 100, SQ), (lx1 - 5, top_y, UP), mesh=kit.railing_segment("iron"))
    # A roof over the gallery on brackets.
    b.box(lx0, lx1 + 20, top_y, 2400, UP + 300, UP + 310, S("Roof_Terracotta"))
    for ly in (1300, 1900):
        lib.point_light(((lx0 + lx1) / 2, ly, UP + 260), intensity=500, radius=800, color=(1.0, 0.82, 0.55))
        det.add(lib.prop("hanging_industrial_lamp"), ((lx0 + lx1) / 2, ly, UP + 300 - 136), 0, cull=6000)
    lib.place(lib.save_mesh(b, OUT + "/SM_OldTown_West", collision="complex"), folder="West", label="West")
    r = lib.rng(41)
    # Laundry lines across courtyard 1 and bins, pots, a bench.
    cloth = [kit.paint("Paint_Cloth%d" % i, c, rough=0.9, dirt=0.1) for i, c in enumerate(((0.6, 0.6, 0.58), (0.5, 0.15, 0.12), (0.12, 0.2, 0.45)))]
    lb = lib.MeshBuilder(ground=kit.NOGRIME)
    for k, y in enumerate((-1800, -1500)):
        z = LOW + 520
        kit.cyl(lb, (-4250, y, z), (-3000, y, z), 1.2, S("Metal_Plain"), seg=4, caps=False)
        xx = -4150
        while xx < -3100:
            wdt, hgt = r.uniform(40, 90), r.uniform(50, 90)
            lb.box(xx, xx + wdt, y - 1, y + 1, z - hgt, z, r.choice(cloth))
            xx += wdt + r.uniform(20, 70)
    lib.place(lib.save_mesh(lb, OUT + "/SM_OldTown_Laundry", collision="none"), folder="West", label="Laundry", collision=False)
    for x, y in ((-4150, -1350), (-4050, -1350)):
        det.add(lib.prop("metal_trash_can"), (x, y, LOW), r.uniform(-10, 10), collision=True)
    for x, y, z in ((-4150, -1900, LOW), (-3100, -1900, LOW), (-4150, -300, SQ), (-3500, -1100, SQ), (-2600, -200, SQ)):
        det.add(lib.prop("potted_plant_04"), (x, y, z), r.uniform(0, 360), 1.1, collision=True, cull=6000)
    det.add(old_prop("painted_wooden_bench"), (-3900, -700, SQ), 90, collision=True)
    det.add(old_prop("Barrel_01"), (-3400, -1700, LOW), 0, collision=True)
    det.add(kit.crate(120, 100, 100), (-3700, -600, SQ), 20, collision=True)
    det.add(kit.crate(100, 80, 80), (-3690, -605, SQ + 100), 40, collision=True)
    lib.reverb(-4400, -2920, -2000, -1250, LOW, LOW + 700, "RE_Alley", 0.4)
    lib.reverb(-3900, -3500, -2500, -2000, LOW, LOW + 330, "RE_Alley", 0.5)


def build_streets(det):
    """Street furniture: cars, a kiosk, lamps, bollards, wires; decals."""
    r = lib.rng(51)
    b = lib.MeshBuilder(ground=ground)
    # Newsstand on the lower street, and one on the upper street (cover, chicane).
    for kx, ky, kz in ((600, -2780, LOW), (600, -2960, LOW), (600, -3060, LOW), (600, -3160, LOW), (2300, -2610, LOW), (1600, 1900, SQ), (-2600, 2600, UP), (2100, 2560, UP), (-1550, 2550, UP), (-1550, 2650, UP)):
        b.box(kx - 150, kx + 150, ky - 100, ky + 100, kz, kz + 240, S("Paint_Kiosk") if False else kit.paint("Paint_KioskGreen", (0.05, 0.16, 0.08), dirt=0.5))
        b.box(kx - 190, kx + 190, ky - 140, ky + 140, kz + 240, kz + 256, S("Metal_Plain"))
    # Sidewalk kerbs along both streets.
    for y0, y1, z in ((-3500, -3380, LOW), (-2620, -2500, LOW), (2400, 2520, UP), (3280, 3400, UP)):
        b.box(-X, X, y0, y1, z - 5, z + 14, S("Stone_Pavement"), faces="t")
    b.box(1480, 1720, 1880, 1920, SQ + 256, SQ + 440, S("Wood_Dark"))            # billboard on the square's newsstand
    lib.place(lib.save_mesh(b, OUT + "/SM_OldTown_Streets", collision="complex"), folder="Streets", label="Streets")
    car = lib.prop("covered_car")
    for x, y, z, yaw in ((2000, -2750, LOW, 178), (-2200, 3100, UP, 181), (2800, 2700, UP, 2), (4100, -2720, LOW, 0)):
        det.add(car, (x, y, z), yaw, collision=True)
    for x in range(-4000, 4500, 1300):
        det.add(lib.prop("street_lamp_02"), (x, -2540, LOW), 90, collision=True, cull=0)
        det.add(lib.prop("street_lamp_02"), (x + 600, 3340, UP), -90, collision=True, cull=0)
    for x in range(-4200, 4400, 900):
        det.add(kit.bollard(), (x, -2600, LOW), 0, collision=True)
    # Overhead wires across the lower street (sagging).
    wires = lib.MeshBuilder(ground=kit.NOGRIME)
    for x in (-3000, -500, 2200):
        pts = [(x, -3500, 700 - 60 * math.sin(math.pi * k / 8)) for k in range(9)]
        pts = [(x + k * 20, -3500 + k * 125, 700 - 70 * math.sin(math.pi * k / 8)) for k in range(9)]
        wires.tube(pts, 1.5, S("Metal_Plain"), segments=4)
    lib.place(lib.save_mesh(wires, OUT + "/SM_OldTown_Wires", collision="none"), folder="Streets", label="Wires", collision=False, shadow=False)
    # Decals: cracks, stains, manhole-like dirt, puddles.
    for i in range(30):
        x, y = r.uniform(-4300, 4300), r.choice([r.uniform(-3450, -2550), r.uniform(2450, 3350), r.uniform(-900, 2300)])
        lib.decal(r.choice([lib.CRACK, lib.DIRT, lib.OIL_SMALL, lib.CRACK_WEB]), (x, y, ground(x, y)), (80, r.uniform(120, 260), r.uniform(120, 260)),
                  yaw=r.uniform(0, 360), opacity=0.75)
    for x, y in ((-1500, -3000), (1800, 2900), (-3700, -1600)):
        lib.decal(lib.WET, (x, y, ground(x, y)), (80, 300, 220), yaw=r.uniform(0, 360), roughness=0.08, opacity=0.75)
    for yy in range(-2400, 2400, 700):
        lib.decal(lib.MOSS, (ALLEY_X0 + 25, yy, ground(ALLEY_X0 + 25, yy) + 60), (40, 400, 120), pitch=0, yaw=0, opacity=0.8)


def build_perimeter(det):
    """Map edges: the house rows close north and south; west and east get walls and blockers."""
    b = lib.MeshBuilder(ground=ground)
    wall = S("Stone_Blocks")
    for x0, x1 in ((-X - 60, -X), (X, X + 60)):
        for y0, y1, z in ((-Y, -2500, LOW), (-2500, -1250, LOW), (-1250, 2400, SQ), (2400, Y, UP)):
            b.box(x0, x1, y0, y1, z - 60, z + 420, wall)
    lib.place(lib.save_mesh(b, OUT + "/SM_OldTown_Perimeter", collision="complex"), folder="Perimeter", label="Perimeter")
    for x0, x1, y0, y1 in ((-X - 60, X + 60, -Y - 60, -Y + 60), (-X - 60, -X + 60, -Y, Y), (X - 60, X + 60, -Y, Y), (-X, X, Y - 60, Y + 60)):
        lib.blocking_box(x0, x1, y0, y1, -500, 2600, "Edge")


def build_backdrop(det):
    """Beyond the map: more roofs down the hill to the south, hills and a far campanile."""
    b = lib.MeshBuilder(ground=kit.NOGRIME)
    r = lib.rng(61)
    roofs = [S("Roof_Terracotta"), S("Roof_Clay")]
    walls = [S("Plaster_Ochre"), S("Plaster_Pink"), S("Plaster_Yellow"), S("Plaster_White")]
    for i in range(34):
        side = i % 4
        if side == 0:
            x, y = r.uniform(-9000, 9000), r.uniform(-9500, -5200)
            z = -300 - (abs(y) - 5000) * 0.18
        elif side == 1:
            x, y = r.uniform(-9000, 9000), r.uniform(5200, 9000)
            z = UP + (y - 5000) * 0.15
        elif side == 2:
            x, y = r.uniform(-11000, -5500), r.uniform(-7000, 7000)
            z = y * 0.06
        else:
            x, y = r.uniform(5500, 11000), r.uniform(-7000, 7000)
            z = y * 0.06
        w, d, hgt = r.uniform(700, 1400), r.uniform(600, 1100), r.uniform(700, 1300)
        b.box(x - w / 2, x + w / 2, y - d / 2, y + d / 2, z - 400, z + hgt, r.choice(walls))
        b.obox((x, y - d / 4, z + hgt + 120), (w + 60, d / 2 + 60, 14), 0, r.choice(roofs), pitch=-14)
        b.obox((x, y + d / 4, z + hgt + 120), (w + 60, d / 2 + 60, 14), 0, r.choice(roofs), pitch=14)
    kit.cyl(b, (-7000, 7500, UP), (-7000, 7500, UP + 2600), 260, S("Stone_Blocks"), seg=4)
    kit.cyl(b, (-7000, 7500, UP + 2600), (-7000, 7500, UP + 3100), 300, S("Roof_Clay"), seg=4, r1=10)
    lib.place(lib.save_mesh(b, OUT + "/SM_OldTown_Backdrop", collision="none"), folder="Backdrop", label="Backdrop", collision=False)
    for i in range(24):
        x = r.uniform(-12000, 12000)
        y = r.choice([r.uniform(-12000, -9000), r.uniform(9500, 12000)])
        det.add(kit.tree(r.randint(0, 2)), (x, y, (UP if y > 0 else -1200)), r.uniform(0, 360), r.uniform(1.3, 1.8), cull=0)


def scatter(det):
    r = lib.rng(71)
    spots = []
    for x in range(-4300, 4300, 180):
        for y, z in ((-3480, LOW), (-2520, LOW), (2420, UP), (3380, UP)):
            if r.random() < 0.25:
                spots.append((x + r.uniform(-40, 40), y))
    kit.debris(det, r, spots[::3], ground)
    for x, y in spots[1::3]:
        det.add(lib.prop("weed_plant_02", r.choice(["weed_plant_02_a_LOD0", "weed_plant_02_b_LOD0", "weed_plant_02_c_LOD0"])),
                (x, y, ground(x, y) - 1), r.uniform(0, 360), r.uniform(1.5, 2.5), cull=3000, shadow=False)
    for x, y in spots[2::3]:
        det.add(lib.prop("dandelion_01", r.choice(["dandelion_01_c_LOD1", "dandelion_01_e_LOD1"])), (x, y, ground(x, y) - 1),
                r.uniform(0, 360), r.uniform(1.0, 1.4), cull=2500, shadow=False)


# ---------------------------------------------------------------------------
# Gameplay, light, views
# ---------------------------------------------------------------------------

ALPHA = [(-1800, -3250), (-1200, -3200), (-600, -3250), (0, -3200), (-2400, -3200), (-1500, -2750), (-900, -2700), (-300, -2750),
         (-2100, -2750), (1300, -3250)]
BRAVO = [(950, 2650), (1350, 2600), (1700, 2650), (1000, 3000), (1400, 3050), (1800, 3000), (2300, 3000), (1100, 3300),
         (1600, 3300), (2100, 3300)]
FREE = [(-3700, -1700, LOW), (-3300, -700, SQ), (-3000, 1600, UP), (-1950, 850, SQ), (-800, 300, SQ), (900, 1000, SQ),
        (2200, 900, SQ), (2200, 100, SQ), (3350, -1600, SQ), (3350, 900, SQ), (4100, -500, SQ), (3300, 2800, UP),
        (-3800, 2800, UP), (3700, -2700, LOW), (-4000, -2700, LOW), (650, -1100, SQ), (-1550, 2150, SQ)]


def gameplay():
    for x, y in ALPHA:
        lib.start("Alpha", x, y, 90, LOW)
    for x, y in BRAVO:
        lib.start("Bravo", x, y, -90, UP)
    for x, y, z in FREE:
        lib.start("", x, y, math.degrees(math.atan2(-y, -x)) if (x or y) else 0, z)
    lib.machine(-2800, -2600, -90, LOW)
    lib.machine(-3300, 2500, 90, UP)
    lib.machine(-1950, 1000, 180, SQ)
    lib.machine(3350, 400, 0, SQ)
    lib.navmesh(-X, X, -Y, Y, -400, 1500)


def views():
    e = 165.0

    def v(label, x, y, yaw, z=None, pitch=-3):
        return (label, (x, y, (ground(x, y) if z is None else z) + e), pitch, yaw)
    lib.write_views("oldtown", [
        v("alpha", -600, -3000, 90), v("lower_w", -3800, -2700, 0), v("lower_e", 3800, -2700, 180), v("arch", -3700, -2700, 90),
        v("court1", -3200, -1700, 150), v("court2", -2700, -600, 180), v("gallery", -2975, 1400, 90, UP), v("square_s", 0, -700, 90),
        v("square_n", 300, 2100, -90), v("fountain", -800, 500, 20), v("arcade", -1950, 350, 90), v("church_in", 2300, 900, 180),
        v("church_out", 900, 700, 5, None, 2), v("alley_low", 3350, -2450, 90), v("alley_mid", 3350, 0, 90), v("alley_up", 3350, 2100, 90),
        v("garden", 4200, -400, 150), v("upper_w", -3800, 2900, 0), v("upper_e", 3800, 2900, 180), v("bravo", 1500, 2900, -90),
        v("balustrade", 1800, 2600, -110), ("over_s", (0, -9000, 3600), -22, 90), ("over_n", (0, 9000, 4200), -25, -90),
        ("over_top", (-6000, -6000, 5500), -38, 45)])


def build():
    lib.new_level(LEVEL)
    build_terrain()
    det = lib.Cluster("OldTownDetail")
    build_houses(det)
    build_palazzo(det)
    build_church(det)
    build_square(det)
    build_alley(det)
    build_west(det)
    build_streets(det)
    build_perimeter(det)
    build_backdrop(det)
    scatter(det)
    det.build()
    lib.log("detail instances: %d in %d groups" % (det.count(), len(det.groups)))
    gameplay()
    lib.environment(sun_pitch=-34, sun_yaw=200, sun_lux=8.0, sun_color=(1.0, 0.9, 0.76), sky_intensity=1.5, sky_tint=(0.95, 0.97, 1.0),
                    fog_density=0.01, fog_falloff=0.2, exposure=(0.9, 2.2, 0.2), contrast=1.04, saturation=1.08, temperature=6000.0)
    views()
    lib.save_level()


if __name__ == "__main__" or True:
    try:
        build()
        lib.log("done")
    except Exception as ex:
        import traceback
        lib.log("FAILED: %s\n%s" % (ex, traceback.format_exc()))
    lib.flush_log("oldtown.txt")
