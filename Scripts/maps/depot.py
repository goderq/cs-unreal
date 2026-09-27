"""
v2.1 Depot - an industrial rail and road depot at the end of a summer evening
(docs/MAPS_REWORK.md 3). 96 x 72 m, Alpha in the west, Bravo in the east.

    UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script=Scripts/maps/depot.py

Zones (x east, y north, cm):
  upper yard (Alpha)   x < -3000, +150, retaining wall with a parapet, ramp and two stairs
  warehouse            x -1600..2000, y -2600..-600: 36 x 20 m hall, mezzanine offices, racks
  docks + truck apron  x 2000..4800, y < -600: dock platform, bays, trailers, apron sunk to -120
  tech zone            north-west: water tower, pump house, transformer compound
  pipe rack            along y 900 from x -1600 to 2400, walkway at 450
  containers           north-east: stacks of 1-3, stairs to the top of a row
  parking              south-west: cars, lamp poles, verge with trees, guard booth
  rail embankment      north: crest +250 with the line, wagons, a ditch at its foot
"""

import math
import os
import sys

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal  # noqa: E402
import lib  # noqa: E402
import kit  # noqa: E402
from lib import surface as S  # noqa: E402

LEVEL = "/Game/Maps/Lvl_Depot"
OUT = lib.GEN + "/Depot"
X, Y = 4800, 3600
UPPER = 150.0          # Alpha's upper yard
APRON = -120.0         # truck apron at the docks
WALL_X = -3000.0       # retaining wall line


# ---------------------------------------------------------------------------
# Terrain
# ---------------------------------------------------------------------------

def smooth(e0, e1, v):
    t = max(0.0, min(1.0, (v - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


_noise_rng = lib.rng(5)
_grid = [[_noise_rng.random() for _ in range(64)] for _ in range(64)]


def vnoise(x, y, cell=600.0):
    """Value noise 0..1 (for ragged layer edges and small undulation)."""
    fx, fy = x / cell, y / cell
    ix, iy = int(math.floor(fx)), int(math.floor(fy))
    tx, ty = fx - ix, fy - iy
    tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)

    def g(i, j):
        return _grid[j % 64][i % 64]
    a = g(ix, iy) + (g(ix + 1, iy) - g(ix, iy)) * tx
    b = g(ix, iy + 1) + (g(ix + 1, iy + 1) - g(ix, iy + 1)) * tx
    return a + (b - a) * ty


def in_ramp(x, y):
    return 800 <= y <= 1400 and WALL_X <= x <= -2200


def ground(x, y):
    z = 0.0
    # Upper yard behind the retaining wall; the step hides inside the wall.
    if x < WALL_X:
        z = UPPER
    if in_ramp(x, y):
        z = UPPER * (-2200 - x) / 800.0
    # Truck apron: down to -120 at the dock face (x 2400), back up by x 4000.
    if x > 2300 and -3450 < y < -700:
        dip = smooth(2300, 2420, x) * (1 - smooth(3300, 4000, x))
        dip *= smooth(-3450, -3250, y) * (1 - smooth(-900, -700, y))
        z += APRON * dip
    # Rail embankment: crest +250 from y 2850 to 3150, back slope to +100 at the wall.
    if y > 2450:
        z = max(z, 250.0 * smooth(2450, 2850, y) - 150.0 * smooth(3150, 3500, y))
    # Drainage ditch at the embankment foot (east of the ramp so Alpha's yard stays level).
    if -2800 < x and 2150 < y < 2550:
        z -= 55.0 * math.exp(-((y - 2340) ** 2) / (2 * 70.0 ** 2)) * smooth(-2800, -2500, x)
    # A gravel heap in the container yard and a soil mound by the water tower: cover.
    z += 110.0 * math.exp(-((x - 2750) ** 2 + (y - 2150) ** 2) / (2 * 260.0 ** 2))
    z += 60.0 * math.exp(-((x + 1500) ** 2 + (y - 2450) ** 2) / (2 * 300.0 ** 2)) if y < 2450 else 0.0
    # Small undulation on unpaved ground only.
    if layers_raw(x, y)[1] + layers_raw(x, y)[2] > 0.5:
        z += (vnoise(x, y, 350.0) - 0.5) * 12.0
    return z


def layers_raw(x, y):
    """(asphalt, gravel, grass) weights over the concrete base, before noise."""
    n = vnoise(x, y, 500.0)
    asphalt = gravel = grass = 0.0
    # Roads: the east-west service road north of the warehouse, the parking, the apron.
    if -2950 < x < 4700 and -520 < y < 380:
        asphalt = 1.0
    if -2950 < x < -1650 and -3450 < y < -500:
        asphalt = 1.0
    if 2420 < x < 4700 and -3450 < y < -500:
        asphalt = 1.0
    # Gravel: embankment, container yard, tech zone, boundary strips.
    if y > 2250 or (x > 2300 and y > 750) or (-2100 < x < -700 and 1050 < y < 2150):
        gravel = 1.0
    if abs(x) > X - 180 or abs(y) > Y - 180:
        gravel = 1.0
    # Grass: verges, the ditch banks, around the water tower, parking verge, pump house lawn.
    if 2150 < y < 2600 and x > -2600:
        grass = 1.0
    if (x + 2400) ** 2 + (y - 1700) ** 2 < 900 ** 2 and y < 2250:
        grass = max(grass, 0.8)
    if -2950 < x < -1650 and y < -3150:
        grass = 1.0
    if x < -3900 and y > 1300:
        grass = max(grass, 0.7)
    # Ragged edges, and grass eating into the gravel here and there.
    gravel = max(0.0, min(1.0, gravel + (n - 0.5) * 0.6))
    grass = max(0.0, min(1.0, grass + (vnoise(x + 900, y, 350.0) - 0.62) * 1.2 * gravel))
    asphalt = max(0.0, min(1.0, asphalt - max(0.0, n - 0.8) * 2.0))
    return asphalt, gravel, grass


def layers(x, y):
    return layers_raw(x, y)


# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

def terrain_material():
    mel = unreal.MaterialEditingLibrary
    lib.ensure_dir(OUT)
    path = OUT + "/MI_Terrain_Depot"
    mi = lib.EAL.load_asset(path) if lib.EAL.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "MI_Terrain_Depot", OUT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, lib.load(lib.V21 + "/Materials/M_CS_Terrain"))
    for i, v in enumerate(["Concrete_A", "Asphalt_Old", "Ground_Gravel", "Ground_GrassDry"]):
        src = S(v)
        for p in ("Diffuse", "Normal", "ARM"):
            mel.set_material_instance_texture_parameter_value(mi, "%s%d" % (p, i), mel.get_material_instance_texture_parameter_value(src, p))
        mel.set_material_instance_scalar_parameter_value(mi, "Tile%d" % i, mel.get_material_instance_scalar_parameter_value(src, "TileSize"))
    mel.set_material_instance_scalar_parameter_value(mi, "MacroVariation", 0.2)
    phys = lib.load("/Game/Environment/Physics/PM_Dirt")
    lib.EAL.save_loaded_asset(mi, only_if_is_dirty=False)
    return mi


def build_terrain():
    dm, mats, tris = lib.terrain_mesh(-X - 600, X + 600, -Y - 600, Y + 600, 50.0, ground, layers, terrain_material())
    mesh = lib.save_mesh(dm, OUT + "/SM_Depot_Terrain", mats, collision="complex")
    lib.place(mesh, folder="Terrain", label="Terrain")
    far = lib.MeshBuilder(ground=kit.NOGRIME)
    # A ring around the terrain, never under it: the apron (-120) and the ditch (-55) sink below
    # the ring's height, and a full plane showed grass above their floor.
    F, ix, iy = 40000.0, X + 500.0, Y + 500.0
    grass = S("Ground_GrassDry")
    for a, b_, c, d in (((-F, -F), (F, -F), (F, -iy), (-F, -iy)), ((-F, iy), (F, iy), (F, F), (-F, F)),
                        ((-F, -iy), (-ix, -iy), (-ix, iy), (-F, iy)), ((ix, -iy), (F, -iy), (F, iy), (ix, iy))):
        far.quad(a + (-40,), b_ + (-40,), c + (-40,), d + (-40,), grass, uv_scale=0.25)
    lib.place(lib.save_mesh(far, OUT + "/SM_Depot_FarGround", collision="none"), folder="Terrain", label="FarGround", collision=False, shadow=False)
    lib.log("terrain: %d triangles" % tris)


def old_prop(name):
    """v1.1 Poly Haven props kept from the old maps (crates, boxes, barrels, bench)."""
    return lib.load("/Game/Environment/Props/%s/SM_%s" % (name, name))


# ---------------------------------------------------------------------------
# Warehouse: 36 x 20 m hall, gable roof along X, docks on the east face
# ---------------------------------------------------------------------------

WX0, WX1, WY0, WY1 = -1600.0, 2000.0, -2600.0, -600.0
EAVES, RIDGE = 900.0, 1150.0
MEZZ = 330.0
DOCKS_Y = [-2300.0, -1850.0, -1400.0, -950.0]


def build_warehouse(det):
    b = lib.MeshBuilder(ground=ground)
    plinth = S("Concrete_Wall")
    clad = kit.tinted("Metal_Cladding", "Metal_CladdingGrey", (1.3, 1.36, 1.42), desat=0.9)
    roof = kit.tinted("Metal_BoxProfile", "Metal_RoofRed", (1.1, 1.0, 1.0), desat=0.3)
    trim = kit.paint("Paint_TrimDark", (0.05, 0.055, 0.06), dirt=0.4)
    steel = kit.paint("Paint_SteelBlue", (0.05, 0.1, 0.18))
    rubber = kit.paint("Paint_Rubber", (0.025, 0.025, 0.025), rough=0.95, dirt=0.2)
    floor = S("Concrete_Painted")
    shutter = S("Metal_Shutter")
    gl = kit.glass("Warehouse", tint=(0.06, 0.08, 0.09))
    t = 30.0

    def walls(a, c, openings, z_split=140.0):
        kit.wall_openings(b, a, c, 0, z_split, t, plinth, openings)
        kit.wall_openings(b, a, c, z_split, EAVES, t, clad, openings)

    # Openings: (start, end, bottom, top) along each wall.
    north = [(-900, -500, 0, 420), (800, 1200, 0, 420), (190, 310, 0, 215)]
    north += [(x, x + 260, 600, 720) for x in (-1400, -300, 1400)]
    south = [(1490, 1610, 0, 215)] + [(x, x + 260, 600, 720) for x in (-1300, -500, 300, 1100)]
    east = [(yc - 150, yc + 150, 0, 320) for yc in DOCKS_Y]
    west = [(-1310, -1190, 0, 215)] + [(y, y + 180, MEZZ + 110, MEZZ + 230) for y in (-2450, -2150, -1850)]
    walls((WX0, WY1), (WX1, WY1), north)
    walls((WX0, WY0), (WX1, WY0), south)
    walls((WX1, WY0), (WX1, WY1), east)
    walls((WX0, WY0), (WX0, WY1), west)
    # Gable triangles above the eaves, outer and inner faces.
    ym = (WY0 + WY1) / 2
    for x, faces_plus_x in ((WX0 - t / 2, False), (WX0 + t / 2, True), (WX1 - t / 2, False), (WX1 + t / 2, True)):
        pts = [(x, WY0, EAVES), (x, WY1, EAVES), (x, ym, RIDGE)]
        # Counter-clockwise seen from the side the face looks at.
        b.poly(pts if faces_plus_x else list(reversed(pts)), clad)
    # Roof: two slopes with 50 cm overhang, skylight strips, ridge cap, gutters, downpipes.
    half = (WY1 - WY0) / 2
    pitch = math.degrees(math.atan2(RIDGE - EAVES, half))
    span = half + 50
    slope_len = span / math.cos(math.radians(pitch))
    length = (WX1 - WX0) + 100
    cx = (WX0 + WX1) / 2
    for sgn in (-1, 1):
        yc = ym + sgn * span / 2
        zc = RIDGE - (RIDGE - EAVES) * (span / 2) / half + 8
        b.obox((cx, yc, zc), (slope_len, length, 14), -90 * sgn, roof, pitch=pitch)
        ys = ym + sgn * span * 0.45
        zs = RIDGE - (RIDGE - EAVES) * (span * 0.45) / half + 16
        b.obox((cx, ys, zs), (140, length - 400, 3), -90 * sgn, gl, pitch=pitch)
    b.box(WX0 - 50, WX1 + 50, ym - 20, ym + 20, RIDGE + 8, RIDGE + 22, trim)
    for yg in (WY0 - 62, WY1 + 42):
        b.box(WX0 - 50, WX1 + 50, yg, yg + 20, EAVES - 30, EAVES - 5, trim)
    for xg in (WX0 + 60, cx, WX1 - 60):
        for yg in (WY0 - 52, WY1 + 52):
            kit.cyl(b, (xg, yg, 5), (xg, yg, EAVES - 20), 7, trim, seg=8)
    # Floor slab, columns, roof beams.
    b.box(WX0 + t / 2, WX1 - t / 2, WY0 + t / 2, WY1 - t / 2, -10, 3, floor, faces="t")
    for x in (-600.0, 400.0, 1400.0):
        for y in (-1900.0, -1300.0):
            b.box(x - 13, x + 13, y - 13, y + 13, 3, EAVES - 30, steel)
            b.box(x - 30, x + 30, y - 30, y + 30, 3, 12, plinth)
        b.box(x - 12, x + 12, WY0, WY1, EAVES - 60, EAVES - 30, steel)
        for sgn in (-1, 1):
            kit.beam(b, (x, ym + sgn * half, EAVES - 30), (x, ym, RIDGE - 30), 20, 30, steel)
    # Roller door shutters: door A fully up (coil box), door B half down, docks mostly up.
    for (s, e, zb, zt), down in zip(north[:2], (0.0, 200.0)):
        b.box(s - 10, e + 10, WY1 + 15, WY1 + 45, zt, zt + 55, trim)
        if down > 0:
            b.box(s, e, WY1 - 5, WY1 + 5, zt - down, zt, shutter)
    for (s, e, zb, zt), down in zip(east, (180.0, 0.0, 200.0, 200.0)):
        b.box(WX1 + 15, WX1 + 45, s - 10, e + 10, zt, zt + 50, trim)
        if down > 0:
            b.box(WX1 - 5, WX1 + 5, s, e, zt - down, zt, shutter)
        b.box(WX1 + 15, WX1 + 45, s - 30, s, 0, zt + 20, rubber)       # dock seal
        b.box(WX1 + 15, WX1 + 45, e, e + 30, 0, zt + 20, rubber)
        b.box(WX1 + 15, WX1 + 45, s - 30, e + 30, zt, zt + 30, rubber)
        b.box(WX1 + 15, WX1 + 330, s + 20, e - 20, 0, 4, S("Metal_Plain"))  # leveller

    # Mezzanine: deck, supports, two offices, stairs, railings.
    mx0, mx1, my0, my1 = WX0 + t / 2, -700.0, WY0 + t / 2, -1300.0
    b.box(mx0, mx1, my0, my1, MEZZ - 20, MEZZ, floor)
    b.box(mx0, mx1, my0, my1, MEZZ - 32, MEZZ - 20, steel)
    for x in (-1150.0, -710.0):
        for y in (-2400.0, -1900.0, -1310.0):
            b.box(x - 10, x + 10, y - 10, y + 10, 3, MEZZ - 32, steel)
    office = S("Plaster_White")
    oz0, oz1 = MEZZ, MEZZ + 260
    kit.wall_openings(b, (-720, my0), (-720, -1500), oz0, oz1, 12, office,
                      [(y, y + 220, oz0 + 95, oz0 + 205) for y in (-2450, -2150, -1850)])
    kit.wall_openings(b, (mx0, -1500), (-720, -1500), oz0, oz1, 12, office, [(-1010, -890, oz0, oz0 + 215)])
    kit.wall_openings(b, (mx0, -2000), (-720, -2000), oz0, oz1, 12, office, [(-1310, -1190, oz0, oz0 + 215)])
    b.box(mx0, -714, my0, -1494, oz1, oz1 + 10, S("Concrete_B"))
    top = kit.stair_flight(b, -60, -1230, 0, MEZZ, 140, 180, S("Metal_Grate"), steel, solid=False)
    b.box(-1000, top[0] + 2, -1300, -1160, MEZZ - 20, MEZZ, S("Metal_Grate"))
    b.box(-1000, -980, -1180, -1160, 3, MEZZ - 20, steel)
    kit.railing_run(det, (mx0, -1300, MEZZ), (-1000, -1300, MEZZ))
    kit.railing_run(det, (-1000, -1300, MEZZ), (-1000, -1160, MEZZ))
    kit.railing_run(det, (-60, -1160, 0), (top[0], -1160, MEZZ))
    kit.railing_run(det, (-60, -1300, 0), (top[0], -1300, MEZZ))
    kit.railing_run(det, (-1000, -1160, MEZZ), (top[0], -1160, MEZZ))
    kit.railing_run(det, (-700, -1500, MEZZ), (-700, -1300, MEZZ))
    for y in (-2450, -2150, -1850):
        det.add(kit.window(220, 110, "steel"), (-714, y + 110, oz0 + 95), 90)
        det.add(kit.window(180, 120, "steel"), (WX0 - 15, y + 90, MEZZ + 110), -90)
    for s, e, zb, zt in north[3:]:
        det.add(kit.window(260, 120, "steel"), ((s + e) / 2, WY1 + 15, zb), 180)
    for s, e, zb, zt in south[1:]:
        det.add(kit.window(260, 120, "steel"), ((s + e) / 2, WY0 - 15, zb), 0)
    det.add(kit.door(120, 215, "steel"), (250, WY1 + 15, 0), 180)
    det.add(kit.door(120, 215, "steel"), (1550, WY0 - 15, 0), 0)
    det.add(kit.door(120, 215, "steel"), (WX0 - 15, -1250, 0), -90)
    det.add(kit.door(120, 215, "wood"), (-950, -1494, oz0), 180)
    det.add(kit.door(120, 215, "wood"), (-1250, -1994, oz0), 180)

    # Racks with pallets and boxes: two rows of four bays, aisles between.
    racks = []
    for row_y in (-2250.0, -1650.0):
        for i in range(4):
            x = 20.0 + i * 285.0
            det.add(kit.pallet_rack(), (x, row_y, 3), 0, collision=True, cull=9000)
            racks.append((x, row_y))
    r = lib.rng(31)
    box = old_prop("cardboard_box_01")
    crate = old_prop("wooden_crate_01")
    for x, y in racks:
        for level in (3.0, 140.0, 280.0):
            for k in range(2):
                if r.random() < 0.8:
                    px, py = x + 70 + k * 130, y - 55
                    det.add(kit.pallet(), (px, py, level), r.uniform(-3, 3), collision=True, cull=6000)
                    for j in range(r.randint(1, 4)):
                        det.add(box if r.random() < 0.7 else crate, (px + (j % 2 - 0.5) * 50, py + r.uniform(-8, 8), level + 14.4 + (j // 2) * 38.5),
                                r.uniform(-8, 8) + (j % 2) * 90, 1.1, collision=True, cull=5000)
    # Floor stock: pallet stacks and crates as cover in the open half of the hall.
    for px, py, n in ((600, -1050, 5), (1300, -1050, 3), (-300, -2350, 4), (900, -2450, 2)):
        for k in range(n):
            det.add(kit.pallet(), (px, py, 3 + k * 14.4), r.uniform(-5, 5), collision=True, cull=7000)
    big, small = kit.crate(120, 100, 100), kit.crate(100, 80, 80)
    for px, py in ((-1430, -1310), (-1430, -1180), (200, -790), (330, -800), (1450, -2420), (1655, -2395)):
        det.add(big, (px, py, 3), r.uniform(-6, 6), collision=True)
        det.add(small, (px + 5, py - 5, 103), r.uniform(-20, 20), collision=True)
    for k in range(4):
        det.add(kit.pallet(), (1650, -1600, 3 + k * 14.4), r.uniform(-4, 4), collision=True, cull=7000)
    det.add(big, (1650, -1600, 3 + 4 * 14.4), 3, collision=True)
    det.add(small, (1655, -1605, 3 + 4 * 14.4 + 100), 20, collision=True)
    for k in range(4):
        det.add(kit.pallet(), (-700, -800, 3 + k * 14.4), r.uniform(-4, 4), collision=True, cull=7000)
    det.add(big, (-700, -800, 3 + 4 * 14.4), -4, collision=True)
    det.add(small, (-695, -805, 3 + 4 * 14.4 + 100), 25, collision=True)
    for px, py in ((-400, -900), (1750, -900), (-500, -1700)):
        det.add(big, (px, py, 3), r.uniform(0, 90), collision=True)
        det.add(small, (px + 10, py + 5, 103), r.uniform(0, 90), collision=True)

    # Decals: floor lines, dirt and oil; grime at the wall feet; rust streaks under the eaves.
    for y in (-2350, -1950, -1550, -1100):
        lib.decal(lib.LINE, (150, y, 20), (60, 3300, 64), yaw=90, tint=(1.3, 1.1, 0.35))
    for i in range(10):
        lib.decal(lib.OIL if i % 2 else lib.DIRT, (r.uniform(-1300, 1900), r.uniform(-2500, -700), 20),
                  (60, r.uniform(120, 260), r.uniform(120, 260)), yaw=r.uniform(0, 360), opacity=0.8)
    for x in range(-1500, 2000, 400):
        lib.decal(lib.GRIME, (x, WY1 + 20, 70), (40, 400, 140), pitch=0, yaw=-90, opacity=0.9)
        lib.decal(lib.GRIME, (x, WY0 - 20, 70), (40, 400, 140), pitch=0, yaw=90, opacity=0.9)
    for y in range(-2500, -600, 400):
        lib.decal(lib.RUST, (WX1 + 20, y, 700), (40, 300, 300), pitch=0, yaw=180, opacity=0.7)

    # Lights: cool fluorescent in the hall, lamps over the doors.
    for x in (-600.0, 400.0, 1400.0):
        for y in (-2250.0, -1600.0, -950.0):
            det.add(lib.prop("mounted_fluorescent_lights"), (x, y, EAVES - 70), 90, cull=12000)
            lib.point_light((x, y, EAVES - 120), intensity=1000, radius=1400, color=(0.85, 0.92, 1.0))
    for yc in DOCKS_Y:
        det.add(lib.prop("security_light"), (WX1 + 40, yc, 380), 0, cull=10000)
    for x in (-1100, 1000):
        det.add(lib.prop("security_light"), (x, WY1 + 40, 470), 90, cull=10000)
    # Signs: DEPOT 3 on the north facade, LOADING BAY over the docks, NO SMOKING by the door.
    det.add(kit.sign_panel(), (cx, WY1 + 16, 760), 180, scl=(7.0, 1.0, 7.0), materials=[kit.sign_material(kit.SIGN["depot"]), None])
    det.add(kit.sign_panel(), (WX1 + 16, -1625, 470), 90, scl=(5.0, 1.0, 5.0), materials=[kit.sign_material(kit.SIGN["bays"]), None])
    det.add(kit.sign_panel(), (320, WY1 + 16, 240), 180, scl=(1.2, 1.0, 1.2), materials=[kit.sign_material(kit.SIGN["nosmoking"]), None])
    det.add(lib.prop("exterior_aircon_unit"), (WX0 - 40, -2200, 180), -90, collision=True)
    det.add(lib.prop("power_box_01"), (WX0 - 10, -1000, 120), -90)

    mesh = lib.save_mesh(b, OUT + "/SM_Depot_Warehouse", collision="complex")
    lib.place(mesh, folder="Warehouse", label="Warehouse")
    lib.reverb(WX0, WX1, WY0, WY1, 0, EAVES, "RE_Hall")
    lib.reverb(mx0, -720, my0, -1500, MEZZ, MEZZ + 260, "RE_Room", 0.5)
    lib.log("warehouse: %d triangles" % b.triangles)


# ---------------------------------------------------------------------------
# Docks and the truck apron
# ---------------------------------------------------------------------------

def build_docks(det):
    b = lib.MeshBuilder(ground=ground)
    conc, steel = S("Concrete_Dirty"), kit.paint("Paint_SteelBlue", (0.05, 0.1, 0.18))
    # Platform: the hall floor carried out 4 m, a 1.2 m drop to the apron.
    b.box(WX1, 2400, WY0 - 20, WY1 + 20, APRON - 20, 0, conc)
    b.box(2395, 2405, WY0 - 20, WY1 + 20, -60, 0, kit.paint("Paint_Galvanised", (0.14, 0.145, 0.15), rough=0.45))  # edge angle
    # Stairs down to the apron at the south end and between bays 2 and 3.
    for yc in (WY0 + 40, -1625.0):
        kit.stair_flight(b, 2400 + 7 * 28, yc, APRON, 0, 110, 180, S("Concrete_B"))
        kit.railing_run(det, (2400 + 7 * 28, yc - 58, APRON), (2400, yc - 58, 0))
        kit.railing_run(det, (2400 + 7 * 28, yc + 58, APRON), (2400, yc + 58, 0))
    # Canopy on posts over the platform.
    b.box(WX1, 2720, WY0 - 60, WY1 + 60, 478, 490, kit.tinted("Metal_BoxProfile", "Metal_RoofGrey", (4.5, 4.6, 4.8), desat=1.0))
    b.box(WX1, 2720, WY0 - 60, WY1 + 60, 460, 478, steel, faces="nsewb")
    for y in range(int(WY0) - 40, int(WY1) + 60, 500):
        b.box(2670, 2690, y - 10, y + 10, APRON, 460, steel)
        det.add(lib.prop("security_light"), (2660, y, 440), 180, cull=10000)
        lib.spot_light((2600, y, 440), (-70, 180), intensity=2500, radius=1800, outer=60, color=(1.0, 0.72, 0.42))
    for yc in DOCKS_Y:
        for dy in (-100, 100):
            det.add(kit.dock_bumper(), (2400, yc + dy, -110), 90, collision=True)
    lib.decal(lib.HAZARD, (2400, -1600, -10), (30, 2000, 25), yaw=90, pitch=0)
    mesh = lib.save_mesh(b, OUT + "/SM_Depot_Docks", collision="complex")
    lib.place(mesh, folder="Docks", label="Docks")

    # Trailers: two at the bays, one parked across the apron (cover).
    blue, white = kit.container_colors()[1], kit.container_colors()[4]
    det.add(kit.trailer(), (2420 + 680, -2300, APRON), 180, collision=True, materials=[white])
    det.add(kit.trailer(), (2420 + 680, -1400, APRON), 180, collision=True, materials=[blue])
    det.add(kit.trailer(), (3750, -3000, ground(3750, -3000)), 158, collision=True)
    r = lib.rng(41)
    for i in range(8):
        lib.decal(r.choice([lib.OIL, lib.OIL_SMALL, lib.TYRES]), (r.uniform(2500, 4300), r.uniform(-3300, -800), 0), (200, r.uniform(200, 500), r.uniform(200, 500)),
                  yaw=r.uniform(0, 360), opacity=0.85)
    lib.decal(lib.WET, (2900, -1900, -110), (80, 700, 500), yaw=10, roughness=0.08, opacity=0.8)
    for x, y, yaw in ((4400, -3300, 90), (4450, -2500, 95), (2600, -3350, 0)):
        det.add(lib.prop("concrete_road_barrier_02"), (x, y, ground(x, y)), yaw, collision=True)
    det.add(lib.prop("metal_trash_can"), (2550, -3380, ground(2550, -3380)), 0, collision=True)


# ---------------------------------------------------------------------------
# Tank farm in the south lane: fuel tanks inside a bund wall
# ---------------------------------------------------------------------------

BX0, BX1, BY0, BY1 = -700.0, 900.0, -3450.0, -2850.0


def build_tank_farm(det):
    b = lib.MeshBuilder(ground=ground)
    conc = S("Concrete_Wall")
    tank = kit.paint("Paint_TankWhite", (0.42, 0.42, 0.4), dirt=0.7)
    steel = kit.paint("Paint_RackGrey", (0.07, 0.08, 0.09), dirt=0.6)
    rust = S("Metal_Rusty")
    # Bund wall 100 cm (chest-high cover, not jumpable). Gaps: west and east into the aisle between
    # the tanks, north and south into the passage at the west end - the lane is never a dead end.
    h, t = 100.0, 25.0
    kit.wall_openings(b, (BX0, BY1), (BX1, BY1), 0, h, t, conc, [(-660, -500, 0, h)])
    kit.wall_openings(b, (BX0, BY0), (BX1, BY0), 0, h, t, conc, [(-660, -500, 0, h)])
    kit.wall_openings(b, (BX0, BY0), (BX0, BY1), 0, h, t, conc, [(-3210, -3090, 0, h)])
    kit.wall_openings(b, (BX1, BY0), (BX1, BY1), 0, h, t, conc, [(-3210, -3090, 0, h)])
    b.box(BX0, BX1, BY0, BY1, -5, 3, S("Concrete_Dirty"), faces="t")
    # Two horizontal tanks on concrete saddles, a manway and a walk plank on top.
    for yc in (-2990.0, -3310.0):
        kit.cyl(b, (-470, yc, 175), (540, yc, 175), 100, tank, seg=20)
        for xs in (-330, 35, 400):
            b.box(xs - 22, xs + 22, yc - 85, yc + 85, 0, 95, conc)
            b.box(xs - 4, xs + 4, yc - 100, yc + 100, 90, 110, steel)
        kit.cyl(b, (60, yc, 270), (60, yc, 300), 30, steel, seg=12)
    # A vertical tank in the north-east corner; the south tank's line crosses the aisle overhead (260).
    kit.cyl(b, (790, -2990, 0), (790, -2990, 360), 70, tank, seg=16)
    kit.cyl(b, (790, -2990, 360), (790, -2990, 395), 71, tank, seg=16, r1=20)
    b.tube([(540, -2990, 150), (725, -2990, 150)], 9, rust)
    b.tube([(540, -3310, 150), (620, -3310, 150), (620, -3310, 260), (620, -2990, 260), (725, -2990, 260)], 9, rust)
    b.tube([(850, -2950, 330), (850, -2780, 330), (850, -2780, 520), (850, WY0 - 30, 520)], 9, rust)
    b.box(840, 860, -2790, -2770, 0, 520, steel)
    b.box(835, 865, -2810, -2750, 510, 516, steel)
    lib.place(lib.save_mesh(b, OUT + "/SM_Depot_TankFarm", collision="complex"), folder="TankFarm", label="TankFarm")
    det.add(kit.sign_panel(), (-50, BY1 + 14, 45), 180, scl=(1.3, 1.0, 1.3), materials=[kit.sign_material(kit.SIGN["nosmoking"]), None])
    lib.decal(lib.OIL, (80, -3150, 3), (100, 350, 250), yaw=30, opacity=0.8)
    lib.decal(lib.STENCIL, (100, -2885, 175), (60, 220, 220), pitch=0, yaw=-90, opacity=0.7)
    lib.decal(lib.HAZARD, (-580, BY1, 0), (40, 160, 60), yaw=0, opacity=0.9)
    lib.decal(lib.HAZARD, (-580, BY0, 0), (40, 160, 60), yaw=0, opacity=0.9)
    # Cover in the rest of the lane: pallet stacks, crates, a barrier - broken up, not a straight corridor.
    r = lib.rng(45)
    for x, y, n in ((-1300, -3050, 3), (1300, -3200, 4), (1850, -2900, 2)):
        for k in range(n):
            det.add(kit.pallet(), (x, y, ground(x, y) + k * 14.4), r.uniform(-8, 8), collision=True, cull=7000)
    det.add(kit.crate(120, 100, 100), (1500, -2800, 0), 30, collision=True)
    det.add(kit.crate(100, 80, 80), (1515, -2790, 100), 50, collision=True)
    det.add(lib.prop("metal_trash_can"), (-1100, -3450, 0), 90, collision=True)
    det.add(lib.prop("concrete_road_barrier_02"), (1900, -3050, 0), 10, collision=True)
    det.add(kit.container(606), (1500, -3450, 0), 0, collision=True, materials=[kit.container_colors()[6], None])
    det.add(kit.container(606), (-1250, -2740, 0), 0, collision=True, materials=[kit.container_colors()[1], None])
    det.add(kit.container(606), (-1250, -2740, 259), 0.5, collision=True, materials=[kit.container_colors()[2], None])
    for x, y in ((-1000, -2750), (-900, -2780)):
        det.add(old_prop("Barrel_01"), (x, y, 0), r.uniform(0, 360), collision=True)


# ---------------------------------------------------------------------------
# Upper yard (Alpha): retaining wall with a parapet, stairs, ramp, pump house
# ---------------------------------------------------------------------------

def build_upper_yard(det):
    b = lib.MeshBuilder(ground=ground)
    wall, cap = S("Concrete_Wall_Warm"), S("Concrete_B")
    gaps = [(-2800, -2540), (-900, -640), (800, 1400)]
    y = -Y + 15.0
    sheet = S("Metal_Corrugated")
    post = kit.paint("Paint_RackGrey", (0.07, 0.08, 0.09), dirt=0.6)
    for g0, g1 in gaps + [(2750, 2750)]:
        if g0 - y > 1:
            b.box(WALL_X - 20, WALL_X + 20, y, g0, -40, UPPER + 100, wall)
            b.box(WALL_X - 28, WALL_X + 28, y, g0, UPPER + 100, UPPER + 110, cap)
            b.box(WALL_X - 3, WALL_X + 3, y + 5, g0 - 5, UPPER + 110, UPPER + 230, sheet)
            yp = y + 5
            while yp < g0:
                b.box(WALL_X - 14, WALL_X - 4, yp - 5, yp + 5, UPPER + 100, UPPER + 235, post)
                yp += 300.0
            b.box(WALL_X - 14, WALL_X - 4, g0 - 10, g0, UPPER + 100, UPPER + 235, post)
            yy = y + 200
            while yy < g0 - 100:
                b.box(WALL_X + 20, WALL_X + 45, yy - 25, yy + 25, -40, UPPER + 80, wall)   # pilaster
                yy += 450
        y = g1
    # Stairs through the two gaps (rising west).
    for yc in (-2670.0, -770.0):
        kit.stair_flight(b, WALL_X + 9 * 28, yc, 0, UPPER, 240, 180, S("Concrete_B"))
        b.box(WALL_X - 70, WALL_X + 1, yc - 120, yc + 120, UPPER - 40, UPPER + 1, S("Concrete_B"))
        for dy in (-125, 125):
            kit.railing_run(det, (WALL_X + 9 * 28, yc + dy, 0), (WALL_X, yc + dy, UPPER))
    # Ramp side walls, sloping with the ramp.
    for ys in (795.0, 1405.0):
        kit.beam(b, (WALL_X, ys, UPPER + 40), (-2200, ys, 40), 40, 120, wall)
    # Pump house: brick, flat roof with a parapet, door east, windows, pipes to the water tower.
    brick = S("Brick_Red")
    px0, px1, py0, py1, h = -4300.0, -3500.0, 1500.0, 2200.0, UPPER + 430
    kit.wall_openings(b, (px0, py0), (px1, py0), UPPER - 20, h, 30, brick, [(-4100, -3980, UPPER + 110, UPPER + 250), (-3800, -3680, UPPER + 110, UPPER + 250)])
    kit.wall_openings(b, (px0, py1), (px1, py1), UPPER - 20, h, 30, brick, [])
    kit.wall_openings(b, (px1, py0), (px1, py1), UPPER - 20, h, 30, brick, [(1750, 1870, UPPER, UPPER + 215), (1950, 2070, UPPER + 110, UPPER + 250)])
    kit.wall_openings(b, (px0, py0), (px0, py1), UPPER - 20, h, 30, brick, [])
    b.box(px0 - 15, px1 + 15, py0 - 15, py1 + 15, h, h + 15, S("Concrete_B"))
    b.box(px0 - 15, px1 + 15, py0 - 15, py1 + 15, UPPER - 5, UPPER + 8, S("Concrete_B"), faces="t")
    kit.wall_openings(b, (px0 - 5, py0 - 5), (px1 + 5, py0 - 5), h + 15, h + 75, 20, S("Concrete_B"), [])
    kit.wall_openings(b, (px0 - 5, py1 + 5), (px1 + 5, py1 + 5), h + 15, h + 75, 20, S("Concrete_B"), [])
    rust = S("Metal_Rusty")
    b.tube([(px1 + 20, 2120, UPPER + 260), (-3200, 2120, UPPER + 260), (-2900, 2120, 360), (-2600, 1950, 360), (-2450, 1850, 300)], 16, rust)
    b.tube([(px1 + 20, 2050, UPPER + 60), (-3150, 2050, UPPER + 60), (-3150, 2050, UPPER - 30)], 11, rust)
    for x in (-3300.0, -2750.0):
        b.box(x - 8, x + 8, 2112, 2128, ground(x, 2120), 350, rust)
    det.add(kit.door(120, 215, "steel"), (px1 + 15, 1810, UPPER), 90)
    for wx in (-4040, -3740):
        det.add(kit.window(120, 140), (wx, py0 - 15, UPPER + 110), 0)
    det.add(kit.window(120, 140), (px1 + 15, 2010, UPPER + 110), 90)
    det.add(lib.prop("security_light"), (px1 + 30, 1810, UPPER + 280), 0, cull=9000)
    lib.point_light((px1 + 80, 1810, UPPER + 250), intensity=500, radius=900, color=(1.0, 0.75, 0.45))
    lib.reverb(px0, px1, py0, py1, UPPER, h, "RE_Room", 0.5)
    # Alpha's yard: fuel tanks, a cabin, pallets and barriers as cover.
    cab = lib.MeshBuilder(ground=ground)
    cab_m = kit.paint("Paint_CabinWhite", (0.55, 0.55, 0.52), dirt=0.6)
    kit.wall_openings(cab, (-4500, -2500), (-3900, -2500), UPPER, UPPER + 270, 10, cab_m, [(-4300, -4150, UPPER + 100, UPPER + 200)])
    kit.wall_openings(cab, (-4500, -2250), (-3900, -2250), UPPER, UPPER + 270, 10, cab_m, [(-4110, -3990, UPPER, UPPER + 210)])
    kit.wall_openings(cab, (-4500, -2500), (-4500, -2250), UPPER, UPPER + 270, 10, cab_m, [])
    kit.wall_openings(cab, (-3900, -2500), (-3900, -2250), UPPER, UPPER + 270, 10, cab_m, [(-2420, -2330, UPPER + 100, UPPER + 200)])
    cab.box(-4520, -3880, -2520, -2230, UPPER + 270, UPPER + 285, S("Metal_BoxProfile"))
    cab.box(-4510, -3890, -2510, -2240, UPPER - 20, UPPER + 8, S("Concrete_B"))
    lib.place(lib.save_mesh(cab, OUT + "/SM_Depot_Cabin", collision="complex"), folder="UpperYard", label="Cabin")
    det.add(kit.window(150, 100, "steel"), (-4225, -2510, UPPER + 100), 0)
    det.add(kit.door(120, 210, "steel"), (-4050, -2240, UPPER), 180)
    r = lib.rng(51)
    for x, y in ((-4500, 500), (-4550, 700)):
        det.add(lib.prop("propane_tank"), (x, y, UPPER), r.uniform(0, 360), 1.6, collision=True)
    for x, y, n in ((-3500, 300, 4), (-4200, -900, 3), (-3350, -1700, 5)):
        for k in range(n):
            det.add(kit.pallet(), (x, y, UPPER + k * 14.4), r.uniform(-6, 6), collision=True, cull=8000)
    for x, y, yaw in ((-3650, 1200, 80), (-3650, -2250, 10)):
        det.add(lib.prop("concrete_road_barrier_02"), (x, y, UPPER), yaw, collision=True)
    for x, y in ((-3250, 900), (-4650, -400)):
        det.add(old_prop("Barrel_01"), (x, y, UPPER), r.uniform(0, 360), collision=True)
        det.add(old_prop("Barrel_01"), (x + 70, y + 20, UPPER), r.uniform(0, 360), collision=True)
    # Open storage shed against the west wall: roof on posts, sacks and pallets inside.
    steel = kit.paint("Paint_RackGrey", (0.07, 0.08, 0.09), dirt=0.6)
    b.box(-4780, -4050, -1250, 250, UPPER + 380, UPPER + 392, kit.tinted("Metal_BoxProfile", "Metal_RoofGrey", (4.5, 4.6, 4.8), desat=1.0))
    for y in (-1230.0, -500.0, 230.0):
        b.box(-4080, -4060, y - 10, y + 10, UPPER, UPPER + 380, steel)
    b.box(-4785, -4770, -1250, 250, UPPER, UPPER + 380, S("Metal_Corrugated"))
    # Pipe stack on timber bearers.
    for k, (dy, dz) in enumerate(((0, 0), (42, 0), (84, 0), (21, 36), (63, 36), (42, 72))):
        kit.cyl(b, (-3700, -2900 + dy, UPPER + 20 + dz), (-3100, -2900 + dy, UPPER + 20 + dz), 20, S("Metal_Rusty"), seg=12)
    for x in (-3650, -3150):
        b.box(x - 10, x + 10, -2935, -2780, UPPER, UPPER + 10, S("Wood_Dark"))
    block = S("Concrete_Block")
    for g0_, g1_ in ((-2800.0, -2540.0), (-900.0, -640.0)):
        b.box(-3330, -3300, g0_ - 60, g1_ + 60, UPPER - 5, UPPER + 240, block)
        b.box(-3336, -3294, g0_ - 66, g1_ + 66, UPPER + 240, UPPER + 250, S("Concrete_B"))
    for bx0_, bx1_, by0_, by1_, heap in ((-3450.0, -3250.0, 700.0, 1400.0, "Ground_Gravel"), (-3340.0, -3040.0, -1650.0, -1400.0, "Ground_Dirt")):
        b.box(bx0_, bx1_, by1_ - 30, by1_, UPPER - 5, UPPER + 240, block)
        b.box(bx0_, bx1_, by0_, by0_ + 30, UPPER - 5, UPPER + 240, block)
        b.box(bx1_ - 30, bx1_, by0_ + 30, by1_ - 30, UPPER - 5, UPPER + 240, block)
        b.box(bx0_ + 20, bx1_ - 30, by0_ + 30, by1_ - 30, UPPER, UPPER + 60, S(heap))
    lib.place(lib.save_mesh(b, OUT + "/SM_Depot_UpperYard", collision="complex"), folder="UpperYard", label="UpperYard")
    det.add(kit.crate(120, 100, 100), (-3150, 1500, UPPER), 8, collision=True)
    det.add(kit.crate(100, 80, 80), (-3145, 1495, UPPER + 100), 30, collision=True)
    det.add(kit.container(606), (-3600, -400, UPPER), 0, collision=True, materials=[kit.container_colors()[0], None])
    det.add(kit.container(606), (-4650, -1700, UPPER), 90, collision=True, materials=[kit.container_colors()[3], None])
    for y in (-1000, -700, -250, 100):
        n = r.randint(2, 4)
        for k in range(n):
            det.add(kit.pallet(), (-4400, y, UPPER + k * 14.4), r.uniform(-5, 5), collision=True, cull=8000)
        det.add(old_prop("cardboard_box_01"), (-4400, y, UPPER + n * 14.4), r.uniform(0, 90), 1.4, collision=True, cull=6000)
    for x, y in ((-4600, -3200), (-3350, 2150)):
        det.add(lib.prop("metal_trash_can"), (x, y, UPPER), r.uniform(-5, 5), collision=True)
    for x, y in ((-3300, -1000), (-3300, 1700)):
        det.add(kit.lamp_pole(800), (x, y, UPPER), 180, collision=True)
        det.add(lib.prop("security_light"), (x - 120, y, UPPER + 780), 180, cull=12000)
    for y in (-2100, -300, 1300):
        lib.decal(lib.HAZARD, (-3060, y, UPPER), (100, 60, 400), yaw=0, opacity=0.9)
    lib.decal(lib.STENCIL, (-3900, -1600, UPPER), (100, 300, 300), yaw=0, opacity=0.8)
    for yy in range(-3300, 2400, 600):
        lib.decal(lib.GRIME, (WALL_X + 50, yy, 100), (40, 600, 200), pitch=0, yaw=180, opacity=0.9)
    lib.decal(lib.MOSS, (WALL_X + 50, 1900, 60), (40, 400, 120), pitch=0, yaw=180)
    lib.decal(lib.CRACK, (WALL_X + 45, -1800, 140), (40, 250, 250), pitch=0, yaw=180, opacity=0.9)


# ---------------------------------------------------------------------------
# Tech zone: water tower, transformer compound
# ---------------------------------------------------------------------------

def build_tech(det):
    b = lib.MeshBuilder(ground=ground)
    rust, pad = S("Metal_Rusty"), S("Concrete_B")
    tank_m = kit.paint("Paint_TankGrey", (0.3, 0.36, 0.4), dirt=0.6)
    tx, ty = -2300.0, 1750.0
    gz = ground(tx, ty)
    legs = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            base = (tx + sx * 280, ty + sy * 280, ground(tx + sx * 280, ty + sy * 280) - 20)
            topp = (tx + sx * 210, ty + sy * 210, 1350)
            b.box(base[0] - 45, base[0] + 45, base[1] - 45, base[1] + 45, base[2], base[2] + 45, pad)
            kit.beam(b, (base[0], base[1], base[2] + 40), topp, 26, 26, rust)
            legs.append((base, topp))
    order = [0, 1, 3, 2, 0]
    for level in (0.25, 0.55, 0.82):
        for a, c in zip(order, order[1:]):
            pa = lib.lerp3(legs[a][0], legs[a][1], level)
            pc = lib.lerp3(legs[c][0], legs[c][1], level)
            kit.beam(b, pa, pc, 12, 12, rust)
            pa2 = lib.lerp3(legs[a][0], legs[a][1], level - 0.22 if level > 0.3 else level + 0.2)
            kit.beam(b, pa2, pc, 8, 8, rust)
    kit.cyl(b, (tx, ty, 1350), (tx, ty, 1800), 330, tank_m, seg=28)
    kit.cyl(b, (tx, ty, 1800), (tx, ty, 1960), 335, tank_m, seg=28, r1=40)
    b.sweep([(0, 0), (90, 0), (90, 8), (0, 8)], [(tx + 330 * math.cos(a), ty + 330 * math.sin(a), 1350) for a in [math.tau * k / 24 for k in range(24)]],
            S("Metal_Grate"), closed=True)
    for k in range(24):
        a = math.tau * k / 24
        px, py = tx + 415 * math.cos(a), ty + 415 * math.sin(a)
        b.box(px - 2, px + 2, py - 2, py + 2, 1358, 1460, rust)
    b.tube([(tx + 415 * math.cos(math.tau * k / 24), ty + 415 * math.sin(math.tau * k / 24), 1460) for k in range(25)], 2.5, rust, segments=6)
    kit.ladder(b, tx + 250, ty - 250, gz, 1350, 45, rust)
    lib.place(lib.save_mesh(b, OUT + "/SM_Depot_WaterTower", collision="complex"), folder="Tech", label="WaterTower")
    lib.point_light((tx, ty, 1985), intensity=400, radius=500, color=(1.0, 0.1, 0.05))
    lib.decal(lib.RUST, (tx, ty - 335, 1600), (60, 500, 400), pitch=0, yaw=90, opacity=0.7)

    # Transformer compound: pad, two transformers, cabinets, fence with a gate on the south side.
    t = lib.MeshBuilder(ground=ground)
    cx0, cx1, cy0, cy1 = -1950.0, -850.0, 1150.0, 2050.0
    t.box(cx0, cx1, cy0, cy1, -20, 10, pad, faces="nsewt")
    body = kit.paint("Paint_TransformerGreen", (0.1, 0.17, 0.12), dirt=0.5)
    for xc in (-1650.0, -1150.0):
        t.box(xc - 110, xc + 110, 1500, 1680, 10, 230, body)
        for k in range(9):
            y = 1505 + k * 20
            t.box(xc - 150, xc - 110, y, y + 4, 30, 200, body)
            t.box(xc + 110, xc + 150, y, y + 4, 30, 200, body)
        for dx in (-60, 0, 60):
            kit.cyl(t, (xc + dx, 1590, 230), (xc + dx, 1590, 300), 10, S("Plaster_White"), seg=10)
        kit.cyl(t, (xc - 90, 1640, 265), (xc + 90, 1640, 265), 28, body, seg=14)
    lib.place(lib.save_mesh(t, OUT + "/SM_Depot_Transformers", collision="complex"), folder="Tech", label="Transformers")
    fence_line(det, (cx0, cy0), (-1500, cy0))
    fence_line(det, (-1300, cy0), (cx1, cy0))
    fence_line(det, (cx1, cy0), (cx1, cy1))
    fence_line(det, (cx1, cy1), (cx0, cy1))
    fence_line(det, (cx0, cy1), (cx0, cy0))
    det.add(kit.sign_panel(), (-1600, cy0 - 8, 150), 0, scl=(1.0, 1.0, 1.0), materials=[kit.sign_material(kit.SIGN["voltage"]), None])
    for x, y, yaw in ((-1900, 1250, 0), (-1880, 1900, 0)):
        det.add(lib.prop("power_box_01"), (x, y, 60), yaw, collision=True)
        det.add(lib.prop("utility_box_01"), (x + 120, y, 10), yaw, collision=True)
    det.add(lib.prop("old_military_compressor"), (-1000, 1250, 10), 0, 1.3, collision=True)
    det.add(lib.prop("portable_generator"), (-1000, 1900, 10), 90, 1.4, collision=True)


def fence_line(det, a, c, height_scale=1.0):
    """Chainlink fence (Poly Haven panel + posts) along a straight line."""
    panel = lib.prop("modular_chainlink_fence", "modular_chainlink_fence")
    post = lib.prop("modular_chainlink_fence", "modular_chainlink_post_middle")
    if panel is None or post is None:
        return
    bb = panel.get_bounding_box()
    width = bb.max.x - bb.min.x
    dx, dy = c[0] - a[0], c[1] - a[1]
    length = math.hypot(dx, dy)
    n = max(1, int(round(length / width)))
    yaw = math.degrees(math.atan2(dy, dx))
    step = length / n
    cy_, sy_ = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    off_x = -(bb.min.x + bb.max.x) / 2.0
    for i in range(n):
        m = (a[0] + dx * (i + 0.5) / n, a[1] + dy * (i + 0.5) / n)
        det.add(panel, (m[0] + off_x * cy_, m[1] + off_x * sy_, ground(m[0], m[1]) - bb.min.z), yaw, (step / width, 1.0, height_scale),
                collision=True, cull=9000)
    for i in range(n + 1):
        p = (a[0] + dx * i / n, a[1] + dy * i / n)
        det.add(post, (p[0], p[1], ground(p[0], p[1]) - post.get_bounding_box().min.z), yaw, (1.0, 1.0, height_scale), collision=True, cull=9000)


# ---------------------------------------------------------------------------
# Pipe rack across the yard, with a walkway on it
# ---------------------------------------------------------------------------

def build_pipe_rack(det):
    b = lib.MeshBuilder(ground=ground)
    steel = kit.paint("Paint_RackGrey", (0.07, 0.08, 0.09), dirt=0.6)
    rust = S("Metal_Rusty")
    xs = [-1600.0, -900.0, -200.0, 500.0, 1200.0, 1900.0, 2500.0]
    walk = 440.0
    for x in xs:
        for y in (780.0, 1020.0):
            b.box(x - 13, x + 13, y - 13, y + 13, ground(x, y) - 10, 640, steel)
            b.box(x - 30, x + 30, y - 30, y + 30, ground(x, y) - 10, ground(x, y) + 20, S("Concrete_B"))
        b.box(x - 12, x + 12, 760, 1040, walk - 30, walk - 5, steel)
        b.box(x - 12, x + 12, 760, 1040, 620, 640, steel)
        kit.beam(b, (x, 780, walk - 150), (x, 900, walk - 30), 8, 8, steel)
        kit.beam(b, (x, 1020, walk - 150), (x, 900, walk - 30), 8, 8, steel)
    # Walkway deck and its stringers; the deck runs past the end portals to the stairs.
    WY_S, WY_N = 865.0, 1005.0
    d0, d1 = xs[0] - 160, xs[-1] + 180
    b.box(d0, d1, WY_S, WY_N, walk - 5, walk + 5, S("Metal_Grate"))
    b.box(d0, d1, WY_S - 5, WY_S + 5, walk - 25, walk + 5, steel)
    b.box(d0, d1, WY_N - 5, WY_N + 5, walk - 25, walk + 5, steel)
    # Pipes: on top of the portals (clear of the stairs' headroom) and under the walkway,
    # dropping into the ground beyond the stairs.
    for y, z, radius, mat, x0, x1 in ((790.0, 662.0, 22.0, rust, xs[0] - 230, xs[-1] + 230),
                                      (845.0, 668.0, 28.0, rust, xs[0] - 230, xs[-1] + 230),
                                      (893.0, 654.0, 14.0, S("Metal_Plain"), xs[0] - 230, xs[-1] + 230),
                                      (950.0, walk - 60.0, 12.0, S("Metal_Plain"), xs[0] - 60, xs[-1] + 60),
                                      (985.0, walk - 60.0, 16.0, rust, xs[0] - 60, xs[-1] + 60)):
        path = [(x0, y, ground(x0, y) - 20), (x0, y, z), (x1, y, z), (x1, y, ground(x1, y) - 20)]
        b.tube(path, radius, mat, segments=12)
        for x in xs:
            b.box(x - 4, x + 4, y - radius - 2, y + radius + 2, z - radius - 6, z - radius, steel)
    # Stairs just outside the end portals, 140 cm wide, rising north onto the walkway.
    for x in (xs[0] - 90, xs[-1] + 105):
        n = int(round((walk - ground(x, 400)) / 17.5))
        start_y = WY_S - n * 28
        kit.stair_flight(b, x, start_y, ground(x, start_y), walk, 140, 90, S("Metal_Grate"), steel, solid=False)
        kit.railing_run(det, (x - 73, start_y, ground(x, start_y)), (x - 73, WY_S, walk))
        kit.railing_run(det, (x + 73, start_y, ground(x, start_y)), (x + 73, WY_S, walk))
    kit.railing_run(det, (d0 + 10, WY_N, walk), (d1 - 10, WY_N, walk))
    kit.railing_run(det, (xs[0] - 15, WY_S, walk), (xs[-1] + 30, WY_S, walk))
    kit.railing_run(det, (d0, WY_S, walk), (d0, WY_N, walk))
    kit.railing_run(det, (d1, WY_S, walk), (d1, WY_N, walk))
    vessel = kit.paint("Paint_TankGrey", (0.3, 0.36, 0.4), dirt=0.6)
    kit.cyl(b, (-130, 900, 190), (330, 900, 190), 140, vessel, seg=20)
    for xs_ in (-60, 260):
        b.box(xs_ - 20, xs_ + 20, 790, 1010, ground(xs_, 900) - 5, 80, S("Concrete_B"))
    kit.cyl(b, (100, 900, 330), (100, 900, walk - 60), 14, S("Metal_Plain"), seg=10)
    lib.place(lib.save_mesh(b, OUT + "/SM_Depot_PipeRack", collision="complex"), folder="PipeRack", label="PipeRack")
    # Under the rack: IBC tanks, drums, pallets.
    r = lib.rng(61)
    for x, y in ((-500, 880), (850, 950), (1600, 870)):
        det.add(lib.prop("industrial_pastic_container"), (x, y, ground(x, y)), r.uniform(0, 30), 2.2, collision=True)
    for x, y in ((-700, 650), (1450, 1000), (-1250, 950), (2200, 880)):
        for k in range(r.randint(1, 3)):
            det.add(lib.prop("barrel_03"), (x + k * 70, y + r.uniform(-20, 20), ground(x, y)), r.uniform(0, 360), collision=True)


# ---------------------------------------------------------------------------
# Container yard
# ---------------------------------------------------------------------------

CONTAINERS = [  # (x, y, length, levels, yaw, colour indices bottom..top)
    (3000.0, 1250.0, 1219.0, 2, 0.0, (0, 1)),
    (4150.0, 1250.0, 606.0, 2, 0.0, (2, 5)),
    (4450.0, 1850.0, 606.0, 2, 90.0, (6, 3)),
    (3350.0, 1950.0, 1219.0, 1, 0.0, (3,)),
    (3050.0, 1950.0, 606.0, 1, 3.0, (5,)),    # stacked on the one above (level 2)
    (2650.0, 2650.0, 606.0, 1, 12.0, (7,)),
]


def build_containers(det):
    colors = kit.container_colors()
    for i, (x, y, length, levels, yaw, cols) in enumerate(CONTAINERS):
        z0 = ground(x, y)
        if i == 4:
            z0 = ground(3350, 1950) + 259   # the 20 ft one sits on the long one
        for lvl in range(levels):
            m = colors[cols[min(lvl, len(cols) - 1)]]
            det.add(kit.container(length), (x, y, z0 + lvl * 259), yaw + (lvl * 1.5), collision=True, materials=[m, None], cull=0)
    # Stairs up to the top of the long container (from the gap to its south).
    b = lib.MeshBuilder(ground=ground)
    steel = kit.paint("Paint_RackGrey", (0.07, 0.08, 0.09), dirt=0.6)
    top_z = ground(3350, 1950) + 259
    n = int(round((top_z - ground(3800, 1400)) / 17.5))
    start = 1828 - n * 28
    kit.stair_flight(b, 3800, start, ground(3800, start), top_z, 110, 90, S("Metal_Grate"), steel, solid=False)
    kit.railing_run(det, (3858, start, ground(3800, start)), (3858, 1828, top_z))
    lib.place(lib.save_mesh(b, OUT + "/SM_Depot_ContainerStairs", collision="complex"), folder="Containers", label="ContainerStairs")
    r = lib.rng(71)
    for x, y in ((3700, 1600), (4300, 1600), (2700, 1550)):
        det.add(kit.pallet(), (x, y, ground(x, y)), r.uniform(0, 90), collision=True)
        det.add(old_prop("wooden_crate_02"), (x, y, ground(x, y) + 14.4), r.uniform(0, 90), 1.3, collision=True)
    for i in range(6):
        x, y = r.uniform(2600, 4500), r.uniform(1100, 2300)
        lib.decal(lib.RUST, (x, y, ground(x, y)), (100, 300, 300), yaw=r.uniform(0, 360), opacity=0.6)
    lib.reverb(2600, 4600, 1100, 2400, -50, 520, "RE_Alley", 0.35)


# ---------------------------------------------------------------------------
# Parking with its verge and the guard booth
# ---------------------------------------------------------------------------

def build_parking(det):
    r = lib.rng(81)
    b = lib.MeshBuilder(ground=ground)
    kerb = S("Concrete_Wall")
    b.box(-2950, -1650, -3160, -3140, -5, 15, kerb)                          # verge kerb
    b.box(-2470, -2430, -3140, -1100, -5, 15, kerb)                          # aisle islands
    b.box(-2170, -2130, -3140, -1100, -5, 15, kerb)
    # Guard booth by the road, with a raised barrier arm.
    booth = S("Plaster_White")
    bx0, bx1, by0, by1 = -1900.0, -1650.0, -700.0, -450.0
    kit.wall_openings(b, (bx0, by0), (bx1, by0), 0, 250, 10, booth, [(-1850, -1700, 100, 200)])
    kit.wall_openings(b, (bx0, by1), (bx1, by1), 0, 250, 10, booth, [(-1850, -1700, 100, 200)])
    kit.wall_openings(b, (bx0, by0), (bx0, by1), 0, 250, 10, booth, [(-660, -540, 0, 210)])
    kit.wall_openings(b, (bx1, by0), (bx1, by1), 0, 250, 10, booth, [(-650, -500, 100, 200)])
    b.box(bx0 - 40, bx1 + 40, by0 - 40, by1 + 40, 250, 265, S("Metal_BoxProfile"))
    b.box(bx0, bx1, by0, by1, -5, 5, kerb, faces="t")
    kit.cyl(b, (-1600, -350, 0), (-1600, -350, 110), 12, kit.paint("Paint_SafetyYellow", (0.55, 0.38, 0.03), dirt=0.5))
    kit.beam(b, (-1600, -350, 100), (-1580, 150, 420), 8, 8, kit.paint("Paint_LightRed", (0.6, 0.01, 0.01), rough=0.3, dirt=0.0))
    lib.place(lib.save_mesh(b, OUT + "/SM_Depot_Parking", collision="complex"), folder="Parking", label="Parking")
    for wy in (-700 - 15, -450 + 15):
        det.add(kit.window(150, 100, "steel"), (-1775, wy, 100), 0 if wy < -600 else 180)
    det.add(kit.door(120, 210, "steel"), (bx0 - 5, -600, 0), -90)
    det.add(kit.sign_panel(), (-1775, by1 + 12, 215), 180, scl=(1.4, 1.0, 1.4), materials=[kit.sign_material(kit.SIGN["office"]), None])
    # Cars: covered, slightly skewed, gaps left for cover and lanes.
    car = lib.prop("covered_car")
    for x, y, yaw in ((-2700, -2900, 88), (-2700, -2150, 93), (-2700, -1650, 90), (-1900, -2650, 272), (-1900, -1400, 268)):
        det.add(car, (x, y, ground(x, y)), yaw + r.uniform(-3, 3), collision=True)
    for y in range(-3150, -900, 250):
        for xc in (-2700, -1900):
            lib.decal(lib.LINE, (xc, y, 0), (100, 480, 77), yaw=90, opacity=0.9)
    for x, y in ((-2300, -2700), (-2300, -1500)):
        det.add(kit.lamp_pole(800), (x, y, 0), 0, collision=True)
        det.add(lib.prop("security_light"), (x + 120, y, 780), 0, cull=12000)
        lib.point_light((x + 120, y, 760), intensity=1200, radius=1500, color=(1.0, 0.8, 0.55))
    # Verge: trees, shrubs, grass.
    for i, (x, y) in enumerate(((-2800, -3350), (-2250, -3400), (-1800, -3330))):
        det.add(kit.tree(i % 3), (x, y, ground(x, y) - 10), r.uniform(0, 360), collision=True, cull=0)
    for _ in range(14):
        x, y = r.uniform(-2900, -1700), r.uniform(-3450, -3200)
        det.add(lib.prop("shrub_02", r.choice(["shrub_02_a_LOD0", "shrub_02_b_LOD0", "shrub_02_d_LOD0"])), (x, y, ground(x, y) - 5),
                r.uniform(0, 360), r.uniform(0.5, 0.9), cull=7000)
    kit.grass_patch(det, r, -2300, -3320, 700, 4, ground, keep=lambda x, y: y < -3160)
    for _ in range(6):
        x, y = r.uniform(-2900, -1700), r.uniform(-3100, -1100)
        lib.decal(r.choice([lib.OIL, lib.OIL_SMALL, lib.CRACK]), (x, y, 0), (100, r.uniform(150, 300), r.uniform(150, 300)), yaw=r.uniform(0, 360), opacity=0.8)


# ---------------------------------------------------------------------------
# Rail embankment, perimeter and the world beyond it
# ---------------------------------------------------------------------------

def build_rail(det):
    b = lib.MeshBuilder(ground=ground)
    rail = S("Metal_Rusty")
    for yr in (2928.0, 3072.0):
        b.box(-X - 400, X + 400, yr - 4, yr + 4, 262, 276, rail)
        b.box(-X - 400, X + 400, yr - 7, yr + 7, 256, 262, rail)
    lib.place(lib.save_mesh(b, OUT + "/SM_Depot_Rails", collision="none"), folder="Rail", label="Rails")
    x = -X - 380.0
    while x < X + 400:
        det.add(kit.sleeper(), (x, 3000, 246), 90 + (x % 7) - 3, cull=6000, shadow=False)
        x += 65.0
    # Wagons: two box cars and a flat car with a container - cover and sightline breaks.
    wagon = boxcar()
    for wx, yaw in ((-1600.0, 0.0), (2200.0, 0.0)):
        det.add(wagon, (wx, 3000, 256), yaw, collision=True)
    flat = flatcar()
    det.add(flat, (-4000, 3000, 256), 0, collision=True)
    det.add(kit.container(1219), (-4000, 3000, 256 + 120), 0, collision=True, materials=[kit.container_colors()[1], None])


def boxcar():
    def build():
        b = lib.MeshBuilder(ground=kit.NOGRIME)
        body = kit.tinted("Metal_BoxProfile_Red", "Metal_WagonBrown", (0.7, 0.35, 0.22), dirt=0.6)
        chassis = S("Metal_Rusty")
        L = 1500.0
        b.box(-L / 2, L / 2, -150, 150, 110, 430, body)
        b.box(-L / 2 - 10, L / 2 + 10, -155, 155, 430, 445, chassis)
        for side in (-1, 1):
            b.box(-160, 160, side * 150 - 4 * (1 if side > 0 else -1), side * 150 + 4 * (1 if side > 0 else -1), 120, 400, chassis)
        b.box(-L / 2 + 50, L / 2 - 50, -110, 110, 80, 110, chassis)
        b.box(-L / 2 + 395, L / 2 - 395, -100, 100, 22, 80, chassis)
        b.box(-L / 2 + 395, L / 2 - 395, -58, 58, 2, 22, chassis)
        for bx in (-L / 2 + 250, L / 2 - 250):
            b.box(bx - 135, bx + 135, -58, 58, 2, 45, chassis)
        for bx in (-L / 2 + 250, L / 2 - 250):
            b.box(bx - 140, bx + 140, -120, 120, 45, 85, chassis)
            for ax in (-90, 90):
                for side in (-1, 1):
                    kit.cyl(b, (bx + ax, side * 72 - 6, 45), (bx + ax, side * 72 + 6, 45), 45, chassis, seg=14)
        return lib.save_mesh(b, kit.KIT + "/SM_Kit_Boxcar", collision="simple")
    return kit._once("boxcar", build)


def flatcar():
    def build():
        b = lib.MeshBuilder(ground=kit.NOGRIME)
        chassis = S("Metal_Rusty")
        L = 1400.0
        b.box(-L / 2, L / 2, -140, 140, 95, 120, chassis)
        for bx in (-L / 2 + 230, L / 2 - 230):
            b.box(bx - 140, bx + 140, -120, 120, 45, 95, chassis)
            for ax in (-90, 90):
                for side in (-1, 1):
                    kit.cyl(b, (bx + ax, side * 72 - 6, 45), (bx + ax, side * 72 + 6, 45), 45, chassis, seg=14)
        return lib.save_mesh(b, kit.KIT + "/SM_Kit_Flatcar", collision="simple")
    return kit._once("flatcar", build)


def build_trackside(det):
    """Relay hut by the ditch, dumped containers behind the embankment: breaks for the long east-west lanes."""
    b = lib.MeshBuilder(ground=ground)
    door_m = kit.paint("Paint_DoorGrey", (0.16, 0.19, 0.21))
    for x0, x1, y0, y1 in ((200.0, 500.0, 2410.0, 2600.0), (-1600.0, -1320.0, 2480.0, 2830.0)):
        gmin = min(ground(x, y) for x in (x0, x1) for y in (y0, y1))
        gmax = max(ground(x, y) for x in (x0, x1) for y in (y0, y1))
        top = gmax + 290.0
        b.box(x0, x1, y0, y1, gmin - 20, top, S("Concrete_Block"))
        b.box(x0 - 25, x1 + 25, y0 - 25, y1 + 25, top, top + 14, S("Concrete_B"))
        ym = (y0 + y1) / 2
        b.box(x0 - 5, x0, ym - 45, ym + 45, gmax, gmax + 200, door_m)
        det.add(kit.sign_panel(), (x0 - 8, ym, gmax + 230), -90, scl=(0.9, 1.0, 0.9), materials=[kit.sign_material(kit.SIGN["voltage"]), None])
    # Culvert sections for the drainage works, stacked by the ditch (two rows).
    conc = S("Concrete_B")
    for cx, cz in ((-1400, 0), (-1235, 0), (-1070, 0), (-1317, 1), (-1152, 1)):
        z = min(ground(cx, y) for y in (2180, 2430)) + 80 + cz * 150
        kit.cyl(b, (cx, 2180, z), (cx, 2430, z), 80, conc, seg=16, caps=False)
        kit.cyl(b, (cx, 2180, z), (cx, 2430, z), 66, conc, seg=16, caps=False)
    lib.place(lib.save_mesh(b, OUT + "/SM_Depot_Trackside", collision="complex"), folder="Rail", label="Trackside")
    colors = kit.container_colors()
    rx0, rx1, ry0, ry1 = 800.0, 1100.0, 3165.0, 3590.0
    rtop = ground(950, ry0) + 260.0
    b2 = lib.MeshBuilder(ground=ground)
    b2.box(rx0, rx1, ry0, ry1, min(ground(950, ry1), ground(950, ry0)) - 20, rtop, S("Concrete_Block"))
    b2.box(rx0 - 20, rx1 + 20, ry0 - 20, ry1 - 10, rtop, rtop + 14, S("Concrete_B"))
    b2.box(rx0 - 5, rx0, 3250, 3340, ground(rx0, 3295), ground(rx0, 3295) + 200, door_m)
    lib.place(lib.save_mesh(b2, OUT + "/SM_Depot_RelayRoom", collision="complex"), folder="Rail", label="RelayRoom")
    det.add(kit.sign_panel(), (rx0 - 8, 3295, ground(rx0, 3295) + 230), -90, scl=(0.9, 1.0, 0.9), materials=[kit.sign_material(kit.SIGN["voltage"]), None])
    for lvl in range(2):
        det.add(kit.container(606), (4660, 2500, ground(4660, 2200) + lvl * 259), 90 + lvl * 2, collision=True, materials=[colors[5 - lvl * 5], None])
    for x, y, yaw, col in ((-300, 3380, 3, 0), (2700, 3420, -4, 4)):
        z = min(ground(x + dx, y + dy) for dx in (-300, 300) for dy in (-120, 120))
        det.add(kit.container(606), (x, y, z), yaw, collision=True, materials=[colors[col], None])
        lib.decal(lib.RUST, (x, y, z + 259), (100, 400, 200), yaw=yaw, opacity=0.6)


def build_perimeter(det):
    b = lib.MeshBuilder(ground=ground)
    wall = S("Concrete_Block")
    cap = S("Concrete_B")

    def run(a, c, h=300.0):
        horizontal = abs(c[1] - a[1]) < 1
        lo, hi = (a[0], c[0]) if horizontal else (a[1], c[1])
        s = lo
        while s < hi:
            e = min(hi, s + 400)
            if horizontal:
                g = min(ground(s, a[1]), ground(e, a[1]))
                b.box(s, e, a[1] - 15, a[1] + 15, g - 40, g + h, wall)
                b.box(s - 3, s + 25, a[1] - 22, a[1] + 22, g - 40, g + h + 12, cap)
            else:
                g = min(ground(a[0], s), ground(a[0], e))
                b.box(a[0] - 15, a[0] + 15, s, e, g - 40, g + h, wall)
                b.box(a[0] - 22, a[0] + 22, s - 3, s + 25, g - 40, g + h + 12, cap)
            s = e
    run((-X, -Y), (X, -Y))
    run((-X, -Y), (-X, Y))
    run((X, -Y), (X, Y))
    lib.place(lib.save_mesh(b, OUT + "/SM_Depot_Perimeter", collision="complex"), folder="Perimeter", label="Perimeter")
    # North: a chainlink fence behind the line, the countryside visible through it.
    fence_line(det, (-X, Y), (X, Y), 1.15)
    for x0, x1, y0, y1, z1 in ((-X - 50, X + 50, -Y - 60, -Y + 60, 1200), (-X - 60, -X + 60, -Y, Y, 1200), (X - 60, X + 60, -Y, Y, 1200), (-X, X, Y - 60, Y + 60, 1200)):
        lib.blocking_box(x0, x1, y0, y1, -500, z1 + 800, "Edge")


def build_backdrop(det):
    """Beyond the wall: other depots' sheds, silos and a chimney (landmark), tree lines. No collision."""
    b = lib.MeshBuilder(ground=kit.NOGRIME)
    clad = S("Metal_Corrugated")
    roof = kit.tinted("Metal_BoxProfile", "Metal_RoofGrey", (4.5, 4.6, 4.8), desat=1.0)
    for x0, x1, y0, y1, h in ((-4500, -1500, -7500, -5200, 1300), (-500, 3500, -8000, -5600, 1600), (6000, 8500, -3000, 1000, 1400),
                              (-8200, -6200, -2500, 1800, 1100)):
        b.box(x0, x1, y0, y1, -100, h, clad)
        b.box(x0 - 40, x1 + 40, y0 - 40, y1 + 40, h, h + 30, roof)
    for sx, sy in ((6800, 2600), (7500, 2600), (8200, 2600)):
        kit.cyl(b, (sx, sy, -100), (sx, sy, 2200), 320, S("Concrete_Wall"), seg=20)
        kit.cyl(b, (sx, sy, 2200), (sx, sy, 2450), 325, S("Metal_Plain"), seg=20, r1=60)
    brick = S("Brick_Red")
    kit.cyl(b, (7200, 5200, -100), (7200, 5200, 4200), 230, brick, seg=20, r1=150)
    for z in (3600.0, 3900.0):
        kit.cyl(b, (7200, 5200, z), (7200, 5200, z + 90), 180, S("Plaster_White"), seg=20, r1=175)
    mesh = lib.save_mesh(b, OUT + "/SM_Depot_Backdrop", collision="none")
    lib.place(mesh, folder="Backdrop", label="Backdrop", collision=False)
    lib.point_light((7200, 5200, 4230), intensity=600, radius=900, color=(1.0, 0.1, 0.05))
    r = lib.rng(91)
    for x in range(-6000, 6200, 520):
        y = Y + 900 + r.uniform(-250, 350)
        det.add(kit.tree(r.randint(0, 2)), (x + r.uniform(-150, 150), y, 80), r.uniform(0, 360), r.uniform(1.1, 1.6), cull=0)
    for y in range(-3000, 3400, 650):
        det.add(kit.tree(r.randint(0, 2)), (-X - 900 + r.uniform(-200, 200), y, 0), r.uniform(0, 360), r.uniform(1.2, 1.6), cull=0)


# ---------------------------------------------------------------------------
# The yard: workshop canopy, lamp posts along the road, scatter, decals
# ---------------------------------------------------------------------------

def build_yard(det):
    b = lib.MeshBuilder(ground=ground)
    steel = kit.paint("Paint_RackGrey", (0.07, 0.08, 0.09), dirt=0.6)
    # Workshop canopy: roof on posts, corrugated back wall, a bench.
    sx0, sx1, sy0, sy1 = 200.0, 1400.0, 1700.0, 2150.0
    b.box(sx0 - 30, sx1 + 30, sy0 - 60, sy1 + 30, 400, 412, S("Metal_Corrugated"))
    for x in (sx0, (sx0 + sx1) / 2, sx1):
        b.box(x - 10, x + 10, sy0 - 50, sy0 - 30, ground(x, sy0), 400, steel)
    b.box(sx0 - 20, sx1 + 20, sy1, sy1 + 12, ground(800, sy1) - 10, 400, S("Metal_Corrugated"))
    b.box(sx1 + 8, sx1 + 20, sy0 - 40, sy1, min(ground(sx1, sy0), ground(sx1, sy1)) - 10, 400, S("Metal_Corrugated"))
    b.box(sx0, sx1, sy0 - 40, sy1, ground(800, 1900) - 5, ground(800, 1900) + 8, S("Concrete_Dirty"), faces="t")
    b.box(500, 900, 2070, 2140, 0, 90, S("Wood_Dark"))
    lib.place(lib.save_mesh(b, OUT + "/SM_Depot_Workshop", collision="complex"), folder="Yard", label="Workshop")
    r = lib.rng(101)
    det.add(lib.prop("steel_frame_shelves_01"), (1200, 2090, 3), 180, collision=True)
    det.add(lib.prop("hand_truck"), (1000, 1850, 3), 40, collision=True)
    det.add(lib.prop("old_military_compressor"), (350, 1950, 3), 90, collision=True)
    for k in range(4):
        det.add(lib.prop("old_tyre"), (1300, 1800 + k * 20, 3 + k * 17), r.uniform(0, 360), collision=False, cull=5000)
    for x, y in ((600, 1850), (650, 1880)):
        det.add(lib.prop("plastic_crate_03"), (x, y, 3), r.uniform(0, 90), 1.6, collision=True)
    det.add(lib.prop("propane_tank"), (1350, 2080, 3), 0, 1.5, collision=True)
    det.add(lib.prop("hanging_industrial_lamp"), (800, 1920, 400 - 136), 0, cull=8000)
    lib.point_light((800, 1920, 250), intensity=700, radius=900, color=(1.0, 0.78, 0.5))
    # Lamp posts along the north side of the service road.
    for x in (-2200.0, 0.0, 2000.0, 4000.0):
        det.add(kit.lamp_pole(800), (x, 470, ground(x, 470)), 180, collision=True)
        det.add(lib.prop("security_light"), (x - 120, 470, 780), 180, cull=12000)
        lib.point_light((x - 120, 470, 750), intensity=1200, radius=1600, color=(1.0, 0.78, 0.5))
    # Yard cover: barriers, a crate stack, drums.
    for x, y, yaw in ((500, 1150, 20), (1500, 1400, 100), (2300, 300, 90)):
        det.add(lib.prop("concrete_road_barrier_02"), (x, y, ground(x, y)), yaw, collision=True)
    for x, y in ((-300, 1130), (1900, 1700)):
        det.add(kit.crate(120, 100, 100), (x, y, ground(x, y)), r.uniform(0, 90), collision=True)
        det.add(kit.crate(100, 80, 80), (x + 140, y + 20, ground(x + 140, y + 20)), r.uniform(0, 90), collision=True)

    # Road markings and yard decals.
    for x in range(-2800, 4600, 600):
        lib.decal(lib.LINE, (x, -70, 0), (100, 300, 80), yaw=90)
    for x, yaw in ((-2600, 0), (3900, 180)):
        lib.decal(lib.ARROW, (x, 150, 0), (100, 250, 250), yaw=yaw + 90, opacity=0.9)
    for i in range(26):
        x, y = r.uniform(-2900, 4600), r.uniform(-500, 2300)
        lib.decal(r.choice([lib.CRACK, lib.CRACK_WEB, lib.OIL_SMALL, lib.DIRT, lib.CRACK]), (x, y, ground(x, y)), (120, r.uniform(150, 400), r.uniform(150, 400)),
                  yaw=r.uniform(0, 360), opacity=0.85)
    for x, y in ((600, 100), (-1800, 350), (3300, -250)):
        lib.decal(lib.WET, (x, y, 0), (80, r.uniform(250, 450), r.uniform(200, 350)), yaw=r.uniform(0, 360), roughness=0.08, opacity=0.75)
    for x in (-1000, 1600, 3000):
        lib.decal(lib.TYRES, (x, 0, 0), (100, 600, 500), yaw=r.uniform(-20, 20))


def scatter(det):
    r = lib.rng(111)
    # Grass: the ditch banks, the lawn round the water tower, the pump house lawn.
    for x in range(-2500, 4700, 450):
        kit.grass_patch(det, r, x + r.uniform(-100, 100), 2330 + r.uniform(-60, 60), 260, 5, ground, keep=lambda x, y: layers(x, y)[2] > 0.4)
    kit.grass_patch(det, r, -2300, 1750, 750, 3, ground, keep=lambda x, y: layers(x, y)[2] > 0.4 and (x + 2300) ** 2 + (y - 1750) ** 2 > 400 ** 2)
    kit.grass_patch(det, r, -4300, 1900, 500, 3, ground, keep=lambda x, y: layers(x, y)[2] > 0.4 and not (-4300 < x < -3500 and 1500 < y < 2200))
    # Weeds and stones along wall feet (perimeter and retaining wall).
    spots = []
    for x in range(-X + 100, X - 100, 140):
        for yy in (-Y + 45, Y - 60):
            if r.random() < 0.45:
                spots.append((x + r.uniform(-40, 40), yy + r.uniform(-15, 15)))
    for y in range(-Y + 100, Y - 100, 140):
        for xx in (-X + 45, X - 45, WALL_X + 45):
            if r.random() < 0.4 and not (800 < y < 1400):
                spots.append((xx + r.uniform(-10, 10), y + r.uniform(-40, 40)))
    kit.debris(det, r, spots[::2], ground)
    for x, y in spots[1::2]:
        kind = r.random()
        if kind < 0.5:
            det.add(lib.prop("weed_plant_02", r.choice(["weed_plant_02_a_LOD0", "weed_plant_02_b_LOD0", "weed_plant_02_c_LOD0"])),
                    (x, y, ground(x, y) - 1), r.uniform(0, 360), r.uniform(1.8, 3.0), cull=3000, shadow=False)
        elif kind < 0.8:
            det.add(lib.prop("grass_bermuda_01", r.choice(["grass_bermuda_01_seedling_a", "grass_bermuda_01_seedling_d"])),
                    (x, y, ground(x, y) - 1), r.uniform(0, 360), r.uniform(1.5, 2.2), cull=2500, shadow=False)
        else:
            det.add(lib.prop("dandelion_01", r.choice(["dandelion_01_c_LOD1", "dandelion_01_e_LOD1"])), (x, y, ground(x, y) - 1),
                    r.uniform(0, 360), r.uniform(1.0, 1.4), cull=2500, shadow=False)
    # Shrubs along the north fence and round the compounds; a few trees by the ditch and the pump house.
    for x in range(-X + 200, X - 200, 260):
        if r.random() < 0.55:
            y = Y - 180 + r.uniform(-80, 60)
            det.add(lib.prop("shrub_02", r.choice(["shrub_02_a_LOD0", "shrub_02_b_LOD0", "shrub_02_c_LOD0", "shrub_02_d_LOD0"])),
                    (x, y, ground(x, y) - 5), r.uniform(0, 360), r.uniform(0.6, 1.1), cull=8000)
    for x, y in ((-2080, 1100), (-800, 1650), (-2050, 2100), (-4600, 1500), (-3400, 2300)):
        det.add(lib.prop("shrub_02", r.choice(["shrub_02_a_LOD0", "shrub_02_b_LOD0", "shrub_02_d_LOD0"])), (x, y, ground(x, y) - 5),
                r.uniform(0, 360), r.uniform(0.6, 1.0), cull=8000)
    for i, (x, y) in enumerate(((-4550, 2050), (1350, 2480), (-600, 2470), (3900, 2500))):
        det.add(kit.tree((i + 1) % 3), (x, y, ground(x, y) - 10), r.uniform(0, 360), r.uniform(0.9, 1.15), collision=True, cull=0)


# ---------------------------------------------------------------------------
# The service road: weighbridge checkpoint and a truck queue, so it is never one
# 76 m tube from base to base; a garage in the north yard for close fights
# ---------------------------------------------------------------------------

def build_checkpoint(det):
    b = lib.MeshBuilder(ground=ground)
    kerb, booth = S("Concrete_Wall"), S("Plaster_White")
    trim = kit.paint("Paint_TrimDark", (0.05, 0.055, 0.06), dirt=0.4)
    # Island with the weighbridge booth, barrier arms at both ends.
    ix0, ix1, iy0, iy1 = -1250.0, -250.0, -170.0, 120.0
    b.box(ix0, ix1, iy0, iy1, -5, 15, kerb)
    bx0, bx1, by0, by1, h = -950.0, -600.0, -150.0, 100.0, 330.0
    kit.wall_openings(b, (bx0, by0), (bx1, by0), 15, h, 12, booth, [(-880, -670, 110, 210)])
    kit.wall_openings(b, (bx0, by1), (bx1, by1), 15, h, 12, booth, [(-910, -790, 110, 210), (-740, -620, 15, 225)])
    kit.wall_openings(b, (bx0, by0), (bx0, by1), 15, h, 12, booth, [])
    kit.wall_openings(b, (bx1, by0), (bx1, by1), 15, h, 12, booth, [(-80, 40, 110, 210)])
    b.box(bx0 - 60, bx1 + 60, by0 - 60, by1 + 60, h, h + 14, S("Metal_BoxProfile"))
    b.box(bx0 - 62, bx1 + 62, by0 - 62, by1 + 62, h - 12, h, trim, faces="nsew")
    b.box(bx0, bx1, by0, by1, 15, 18, S("Concrete_Painted"), faces="t")
    b.box(-900, -650, 60, 95, 18, 95, S("Wood_Dark"))                        # desk
    # Weighbridge plate on the south lane.
    b.box(-1100, -400, -480, -220, -2, 4, S("Metal_Plain"))
    yellow = kit.paint("Paint_SafetyYellow", (0.55, 0.38, 0.03), dirt=0.5)
    red = kit.paint("Paint_LightRed", (0.6, 0.01, 0.01), rough=0.3, dirt=0.0)
    for x, arm_to in ((ix0 + 30, (ix0 + 30, -480, 105)), (ix1 - 30, (ix1 - 30, 360, 105))):
        kit.cyl(b, (x, 0, 15), (x, 0, 115), 12, yellow)
        kit.beam(b, (x, 0, 105), arm_to, 8, 8, red)
    for x in (ix0 + 120, ix1 - 120):
        for y in (iy0 + 25, iy1 - 25):
            kit.cyl(b, (x, y, 15), (x, y, 110), 10, yellow, seg=10)
    lib.place(lib.save_mesh(b, OUT + "/SM_Depot_Checkpoint", collision="complex"), folder="Road", label="Checkpoint")
    det.add(kit.window(210, 100, "steel"), (-775, by0 - 6, 110), 0)
    det.add(kit.window(120, 100, "steel"), (-850, by1 + 6, 110), 180)
    det.add(kit.door(120, 210, "steel"), (-680, by1 + 6, 15), 180)
    det.add(kit.sign_panel(), (-850, by1 + 8, 245), 180, scl=(1.4, 1.0, 1.4), materials=[kit.sign_material(kit.SIGN["office"]), None])
    lib.point_light((-775, -25, 270), intensity=400, radius=500, color=(1.0, 0.85, 0.65))
    lib.decal(lib.HAZARD, (-750, -225, 4), (40, 700, 40), yaw=0, opacity=0.9)
    lib.decal(lib.HAZARD, (-750, -475, 4), (40, 700, 40), yaw=0, opacity=0.9)
    # Truck queue: staggered trailers - one per lane, one across the road in front of Bravo.
    colors = kit.container_colors()
    det.add(kit.trailer(), (1200, -330, 0), 3, collision=True, materials=[colors[4]])
    det.add(kit.trailer(), (-2150, -330, 0), 182, collision=True, materials=[colors[2]])
    det.add(kit.trailer(), (3350, 330, ground(3350, 330)), 90, collision=True, materials=[colors[0]])
    for lvl, (cx_, cy_, cyaw, col) in enumerate(((1600, 560, 0, 3), (1600, 560, 2, 5))):
        det.add(kit.container(606), (cx_, cy_, ground(1600, 560) + lvl * 259), cyaw, collision=True, materials=[colors[col], None])
    for lvl, (cyaw, col) in enumerate(((0, 1), (-2, 6))):
        det.add(kit.container(606), (1100, 1160, ground(1100, 1160) + lvl * 259), cyaw, collision=True, materials=[colors[col], None])
    det.add(kit.container(606), (300, -60, 0), 0, collision=True, materials=[colors[6], None])
    det.add(kit.container(606), (2650, -330, 0), 2, collision=True, materials=[colors[2], None])
    # Stock against the warehouse's north wall (the lane along it was 1.5 m wide and 76 m long).
    r = lib.rng(121)
    for x, n in ((1500, 4), (-1350, 3), (600, 2)):
        for k in range(n):
            det.add(kit.pallet(), (x, -530, k * 14.4), r.uniform(-4, 4), collision=True, cull=7000)
    det.add(kit.crate(120, 100, 100), (-1250, -520, 0), 10, collision=True)
    det.add(lib.prop("metal_trash_can"), (1850, -500, 0), 90, collision=True)


def build_kiosks(det):
    """Switchgear kiosks (3 m steel cabinets on plinths): at the warehouse's north-west corner,
    against the retaining wall by the road, and in the terrace lane along the fence."""
    b = lib.MeshBuilder(ground=ground)
    body = kit.paint("Paint_KioskGrey", (0.16, 0.18, 0.18), dirt=0.55)
    trim = kit.paint("Paint_TrimDark", (0.05, 0.055, 0.06), dirt=0.4)
    for x0, x1, y0, y1, z in ((-1725, -1618, -680, -560, 0.0), (-2952, -2852, 500, 620, None), (-3200, -3032, 100, 220, UPPER)):
        g = min(ground(x, y) for x in (x0, x1) for y in (y0, y1)) if z is None else z
        b.box(x0 - 5, x1 + 5, y0 - 5, y1 + 5, g - 10, g + 15, S("Concrete_B"))
        b.box(x0, x1, y0, y1, g + 15, g + 300, body)
        b.box(x0 - 6, x1 + 6, y0 - 6, y1 + 6, g + 300, g + 312, trim)
        det.add(kit.sign_panel(), ((x0 + x1) / 2, y0 - 3, g + 200), 0, scl=(0.7, 1.0, 0.7), materials=[kit.sign_material(kit.SIGN["voltage"]), None])
    lib.place(lib.save_mesh(b, OUT + "/SM_Depot_Kiosks", collision="complex"), folder="Road", label="Kiosks")
    colors = kit.container_colors()
    for lvl in range(2):
        det.add(kit.container(606), (3500, 2400, min(ground(3500 + dx, 2400 + dy) for dx in (-300, 300) for dy in (-120, 120)) + lvl * 259),
                -2 + lvl * 3, collision=True, materials=[colors[(4 + lvl * 3) % 8], None])
    det.add(kit.crate(120, 100, 100), (-500, 2150, ground(-500, 2150)), 12, collision=True)
    det.add(kit.crate(100, 80, 80), (-495, 2155, ground(-500, 2150) + 100), 35, collision=True)


def build_waste_bay(det):
    b = lib.MeshBuilder(ground=ground)
    block = S("Concrete_Block")
    x0, x1, y0, y1 = 3480.0, 3800.0, -720.0, -440.0
    g = min(ground(x, y) for x in (x0, x1) for y in (y0, y1))
    b.box(x0, x0 + 30, y0, y1, g - 20, g + 240, block)
    b.box(x0 + 30, x1, y0, y0 + 30, g - 20, g + 240, block)
    b.box(x0 + 30, x1, y1 - 30, y1, g - 20, g + 240, block)
    lib.place(lib.save_mesh(b, OUT + "/SM_Depot_WasteBay", collision="complex"), folder="Road", label="WasteBay")
    det.add(lib.prop("metal_trash_can"), (3650, -580, g), 90, 1.4, collision=True)
    lib.decal(lib.DIRT, (3640, -580, g), (60, 300, 250), yaw=0, opacity=0.8)


def build_garage(det):
    b = lib.MeshBuilder(ground=ground)
    brick = S("Brick_Red")
    gx0, gx1, gy0, gy1, h = -700.0, 200.0, 1200.0, 1600.0, 420.0
    g = ground(-250, 1425)
    shutter = S("Metal_Shutter")
    kit.wall_openings(b, (gx0, gy0), (gx1, gy0), g - 20, h, 25, brick, [(-600, -300, g - 20, g + 330), (-150, 150, g - 20, g + 330)])
    kit.wall_openings(b, (gx0, gy1), (gx1, gy1), g - 20, h, 25, brick, [(-500, -350, g + 170, g + 270), (0, 150, g + 170, g + 270)])
    kit.wall_openings(b, (gx0, gy0), (gx0, gy1), g - 20, h, 25, brick, [(1320, 1440, g - 20, g + 215)])
    kit.wall_openings(b, (gx1, gy0), (gx1, gy1), g - 20, h, 25, brick, [(1410, 1530, g - 20, g + 215)])
    b.box(gx0 - 20, gx1 + 20, gy0 - 20, gy1 + 20, h, h + 18, S("Concrete_B"))
    kit.wall_openings(b, (gx0 - 8, gy0 - 8), (gx1 + 8, gy0 - 8), h + 18, h + 70, 16, S("Concrete_B"), [])
    b.box(gx0, gx1, gy0, gy1, g - 5, g + 8, S("Concrete_Dirty"), faces="t")
    b.box(-150, 150, gy0 - 5, gy0 + 5, g + 210, g + 330, shutter)          # second door half down
    for x0, x1 in ((-610, -290), (-160, 160)):
        b.box(x0, x1, gy0 - 40, gy0 - 12, g + 330, g + 380, kit.paint("Paint_TrimDark", (0.05, 0.055, 0.06), dirt=0.4))
    lib.place(lib.save_mesh(b, OUT + "/SM_Depot_Garage", collision="complex"), folder="Garage", label="Garage")
    for wx in (-425, 75):
        det.add(kit.window(150, 100, "steel"), (wx, gy1 + 13, g + 170), 180)
    det.add(kit.door(120, 215, "steel"), (gx0 - 13, 1380, g), -90)
    det.add(kit.door(120, 215, "steel"), (gx1 + 13, 1470, g), 90)
    det.add(lib.prop("covered_car"), (-450, 1440, g), 92, collision=True)
    det.add(lib.prop("steel_frame_shelves_01"), (100, 1560, g), 180, collision=True)
    det.add(lib.prop("old_tyre"), (-80, 1540, g), 0, cull=5000)
    det.add(kit.crate(100, 80, 80), (0, 1350, g), 20, collision=True)
    det.add(lib.prop("hanging_industrial_lamp"), (-250, 1425, h - 136), 0, cull=8000)
    lib.point_light((-250, 1425, g + 300), intensity=500, radius=700, color=(1.0, 0.78, 0.5))
    lib.reverb(gx0, gx1, gy0, gy1, g, h, "RE_Room", 0.5)
    lib.decal(lib.OIL, (-450, 1440, g), (60, 250, 200), yaw=0, opacity=0.8)


# ---------------------------------------------------------------------------
# Gameplay: spawns, ammo machines, navigation; light; views
# ---------------------------------------------------------------------------

ALPHA = [(-4400, -1400), (-3950, -1500), (-3550, -1250), (-4450, -450), (-3550, -650), (-3850, 100), (-3950, 250), (-4300, 850), (-3800, 700), (-4300, -1900)]
BRAVO = [(3800, 100), (4200, 100), (4600, 150), (3800, 480), (4250, 420), (4600, 480), (3700, 820), (4100, 800), (4450, 950), (4650, -150)]
FREE = [(200, -1000, 0), (1500, -1950, 0), (-1200, -1800, MEZZ), (2200, -1625, 0), (3300, -2650, None), (4200, -1500, None), (-2300, -2300, None),
        (-2300, -1200, None), (-1400, 1350, 10), (-2150, 1150, None), (-3000, 2980, None), (600, 3000, None), (3800, 2990, None),
        (3500, 1600, None), (500, 1650, None), (-1450, 150, None), (-4300, -3100, None)]


def gameplay():
    for x, y in ALPHA:
        lib.start("Alpha", x, y, 0, UPPER)
    for x, y in BRAVO:
        lib.start("Bravo", x, y, 180, ground(x, y))
    for x, y, z in FREE:
        lib.start("", x, y, (math.degrees(math.atan2(-y, -x)) if (x or y) else 0), ground(x, y) if z is None else z)
    lib.machine(-4700, -800, 0, UPPER)
    lib.machine(4700, 700, 180, ground(4700, 700))
    lib.machine(-100, -660, -90, 3)
    lib.machine(800, 2100, -90, ground(800, 2100))
    lib.navmesh(-X, X, -Y, Y, -400, 1200)


def views():
    e = 165.0
    v = [("alpha_spawn", (-4200, -600, UPPER + e), -2, 0), ("parapet", (-3100, 200, UPPER + e), -4, 15),
         ("ramp", (-2250, 1100, e + 5), -3, 180), ("yard", (-500, 600, e), -2, 20), ("pipe_walk", (0, 960, 440 + e), -8, 5),
         ("warehouse_in", (1800, -1000, e), -3, 200), ("mezzanine", (-1450, -1400, MEZZ + e), -8, 5), ("dock", (2250, -650, e), -5, -95),
         ("apron", (4300, -2900, ground(4300, -2900) + e), -2, 150), ("containers_top", (3600, 1950, 259 + e), -6, 200),
         ("tech", (-1400, 900, e), -2, 115), ("parking", (-2300, -3000, e), -2, 80), ("rail", (-3200, 2980, 250 + e), -3, 4),
         ("ditch", (800, 2330, ground(800, 2330) + e), -3, 5), ("bravo_spawn", (4300, 200, e), -2, 180),
         ("east_stair", (2605, -20, e), 12, 90), ("west_stair", (-2700, 960, 60 + e), 5, 0), ("walk_end", (2450, 935, 440 + e), -10, 180), ("tank_farm", (-1500, -2850, e), -3, -8), ("road", (-2700, -100, e), -2, 0), ("garage", (-250, 1100, e), -2, 90), ("tank_inside", (-150, -3150, e), -2, 5),
         ("over_sw", (-6800, -5800, 3600), -30, 40), ("over_ne", (6800, 5600, 3600), -30, 220), ("over_top", (0, -5500, 5200), -48, 90)]
    lib.write_views("depot", v)
    # First-person walk along the four routes and the high ground (eye 165 above the floor).
    def fp(label, x, y, yaw, z=None, pitch=-3):
        return (label, (x, y, (ground(x, y) if z is None else z) + e), pitch, yaw)
    lib.write_views("depot_fp", [
        fp("gapN_top", -3200, -770, 0, UPPER), fp("gapN_down", -2700, -770, 20), fp("road_w", -2500, 100, 0),
        fp("checkpoint", -1500, 150, 0), fp("road_mid", 0, 250, 0), fp("road_e", 2200, 0, 0), fp("bravo_exit", 3700, 1060, 180),
        fp("ramp", -2900, 1100, 0), fp("tech", -2100, 1150, 30), fp("ditch_w", -1800, 2300, 0), fp("ditch_mid", 300, 2280, 0),
        fp("workshop", 800, 1850, 180), fp("containers", 3700, 1450, 90), fp("gapS", -2700, -2670, -20),
        fp("parking_n", -2300, -700, -90), fp("south_lane", -1500, -3100, 0), fp("tank_aisle", -400, -3150, 0),
        fp("apron", 2600, -3000, 30), fp("west_door", -1450, -1250, 0, 3), fp("mezz", -900, -1400, -90, MEZZ),
        fp("hall", 500, -1800, 0, 3), fp("dock2", 2300, -1850, 0, 0), fp("walkway", 0, 935, 0, 440, -8),
        fp("container_top", 3500, 1950, 180, ground(3350, 1950) + 259), fp("crest", 0, 3000, 180, 250), fp("behind_crest", -2000, 3350, 0)])


def build():
    lib.new_level(LEVEL)
    build_terrain()
    det = lib.Cluster("DepotDetail")
    build_warehouse(det)
    build_docks(det)
    build_tank_farm(det)
    build_upper_yard(det)
    build_tech(det)
    build_pipe_rack(det)
    build_containers(det)
    build_parking(det)
    build_rail(det)
    build_trackside(det)
    build_perimeter(det)
    build_backdrop(det)
    build_yard(det)
    build_checkpoint(det)
    build_kiosks(det)
    build_waste_bay(det)
    build_garage(det)
    scatter(det)
    det.build()
    lib.log("detail instances: %d in %d groups" % (det.count(), len(det.groups)))
    gameplay()
    lib.environment(sun_pitch=-30, sun_yaw=45, sun_lux=7.5, sun_color=(1.0, 0.8, 0.58), sky_intensity=1.4, sky_tint=(1.0, 0.96, 0.9),
                    fog_density=0.015, fog_falloff=0.2, exposure=(0.9, 2.2, 0.3), contrast=1.05, saturation=1.05, temperature=5600.0)
    views()
    lib.save_level()


if __name__ == "__main__" or True:
    try:
        build()
        lib.log("done")
    except Exception as ex:
        import traceback
        lib.log("FAILED: %s\n%s" % (ex, traceback.format_exc()))
    lib.flush_log("depot.txt")
