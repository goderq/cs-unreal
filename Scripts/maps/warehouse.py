"""
v2.1 Warehouse - a modern logistics complex on an overcast noon (docs/MAPS_REWORK.md 5).
80 x 64 m, Alpha in the west (parking), Bravo in the east (between the container yard and
the tech zone).

    UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script=Scripts/maps/warehouse.py

Zones (x east, y north, cm):
  main hall        x -2200..2200, y 300..2600: racks in two blocks, a cross aisle, steel mezzanine
                   along the north wall (+350), a catwalk over the cross aisle, overhead crane
  docks            six dock doors on the hall's south wall, platform at 0, truck yard sunk to -130
                   (y -1700..100) with ramps at both ends, trailers
  office           podium +120 in the south-west, glass office, external stair to the roof (+820)
  parking          west, Alpha; grass berm with a drainage ditch along the west edge
  storage units    a row of units with shutters along the north edge, service road behind the hall
  tech zone        north-east: transformers, chillers, ducts, generator, narrow passages
  container yard   south-east: stacks of 1-2, stairs to the top
"""

import math
import os
import sys

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal  # noqa: E402,F401
import lib  # noqa: E402
import kit  # noqa: E402
from lib import surface as S  # noqa: E402

LEVEL = "/Game/Maps/Lvl_Warehouse"
OUT = lib.GEN + "/Warehouse"
X, Y = 4000, 3200
YARD = -130.0
POD = 120.0
HX0, HX1, HY0, HY1 = -2200.0, 2200.0, 300.0, 2600.0
EAVES = 1100.0
MEZZ = 350.0
DOCKS_X = [-1750.0, -1050.0, -350.0, 350.0, 1050.0, 1750.0]
OX0, OX1, OY0, OY1 = -3600.0, -2600.0, -2800.0, -1600.0     # office on the podium
PX0, PX1, PY0, PY1 = -3800.0, -2300.0, -3000.0, -1200.0     # podium


def smooth(e0, e1, v):
    t = max(0.0, min(1.0, (v - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def ground(x, y):
    # Truck yard: sunk in front of the docks, ramps at both ends (the step at y 100 is the dock wall).
    if -1700 <= y < 100 and -2600 < x < 2600:
        if -2000 <= x <= 2000:
            return YARD
        if x < -2000:
            return YARD * (x + 2600) / 600.0
        return YARD * (2600 - x) / 600.0
    if PX0 <= x <= PX1 and PY0 <= y <= PY1:
        return POD
    # West berm and the ditch at its foot.
    if x < -3650:
        return 150.0 * smooth(-3650, -3900, x) - 50.0 * math.exp(-((x + 3600) ** 2) / (2 * 50.0 ** 2))
    return -50.0 * math.exp(-((x + 3600) ** 2) / (2 * 50.0 ** 2)) if -3750 < x < -3450 and y > -1100 else 0.0


def layers(x, y):
    """(asphalt, gravel, grass) over the concrete base."""
    asphalt = gravel = grass = 0.0
    if -2600 < x < 2600 and -3200 < y < 100:
        asphalt = 1.0                                    # truck yard and the lot south of it
    if -3500 < x < -2300 and -1000 < y < 2600:
        asphalt = 1.0                                    # parking
    if -2300 < x < 2300 and 2600 < y < 2900:
        asphalt = 1.0                                    # service road
    if x > 2300 and y > 500:
        gravel = 1.0                                     # tech zone
    if x < -3450:
        grass = 1.0                                      # berm and ditch
    for tx, ty in PARK_TREES:
        if math.hypot(x - tx, y - ty) < 170:
            grass = 1.0
            asphalt = 0.0
    return asphalt, gravel, grass


PARK_TREES = [(-2950, -300), (-2950, 1100), (-2950, 2200)]


def terrain_material():
    mel = unreal.MaterialEditingLibrary
    lib.ensure_dir(OUT)
    path = OUT + "/MI_Terrain_Warehouse"
    mi = lib.EAL.load_asset(path) if lib.EAL.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "MI_Terrain_Warehouse", OUT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, lib.load(lib.V21 + "/Materials/M_CS_Terrain"))
    for i, v in enumerate(["Concrete_Pavers", "Asphalt_New", "Ground_Gravel", "Ground_Grass"]):
        src = S(v)
        for p in ("Diffuse", "Normal", "ARM"):
            mel.set_material_instance_texture_parameter_value(mi, "%s%d" % (p, i), mel.get_material_instance_texture_parameter_value(src, p))
        mel.set_material_instance_scalar_parameter_value(mi, "Tile%d" % i, mel.get_material_instance_scalar_parameter_value(src, "TileSize"))
    mel.set_material_instance_scalar_parameter_value(mi, "MacroVariation", 0.2)
    lib.EAL.save_loaded_asset(mi, only_if_is_dirty=False)
    return mi


def build_terrain():
    dm, mats, tris = lib.terrain_mesh(-X - 600, X + 600, -Y - 600, Y + 600, 50.0, ground, layers, terrain_material(), skirt=500.0)
    lib.place(lib.save_mesh(dm, OUT + "/SM_Warehouse_Terrain", mats, collision="complex"), folder="Terrain", label="Terrain")
    far = lib.MeshBuilder(ground=kit.NOGRIME)
    F, ix, iy = 40000.0, X + 500.0, Y + 500.0
    grass = S("Ground_GrassDry")
    for a, b_, c, d in (((-F, -F), (F, -F), (F, -iy), (-F, -iy)), ((-F, iy), (F, iy), (F, F), (-F, F)),
                        ((-F, -iy), (-ix, -iy), (-ix, iy), (-F, iy)), ((ix, -iy), (F, -iy), (F, iy), (ix, iy))):
        far.quad(a + (-40,), b_ + (-40,), c + (-40,), d + (-40,), grass, uv_scale=0.25)
    lib.place(lib.save_mesh(far, OUT + "/SM_Warehouse_FarGround", collision="none"), folder="Terrain", label="FarGround", collision=False, shadow=False)
    lib.log("terrain: %d triangles" % tris)


def landing(b, x0, x1, y0, y1, z, mat):
    b.box(x0, x1, y0, y1, z - 40, z + 1, mat)


def old_prop(name):
    return lib.load("/Game/Environment/Props/%s/SM_%s" % (name, name))


# ---------------------------------------------------------------------------
# Main hall
# ---------------------------------------------------------------------------

def build_hall(det):
    b = lib.MeshBuilder(ground=ground)
    plinth = S("Concrete_Wall")
    clad = kit.tinted("Metal_Cladding_Blue", "Metal_SandwichGreyBlue", (0.78, 0.86, 0.95), desat=0.4)
    roof = kit.tinted("Metal_BoxProfile", "Metal_RoofSilver", (1.6, 1.65, 1.7), desat=1.0)
    steel = kit.paint("Paint_SteelGrey", (0.12, 0.13, 0.14), dirt=0.3)
    yellow = kit.paint("Paint_SafetyYellow", (0.55, 0.38, 0.03), dirt=0.5)
    rubber = kit.paint("Paint_Rubber", (0.025, 0.025, 0.025), rough=0.95, dirt=0.2)
    floor = S("Concrete_Painted")
    shutter = S("Metal_Shutter")
    gl = kit.glass("Skylight", tint=(0.1, 0.12, 0.14))
    t = 30.0

    def wall(a, c, openings):
        kit.wall_openings(b, a, c, -60, 120, t, plinth, openings)
        kit.wall_openings(b, a, c, 120, EAVES, t, clad, openings)

    south = [(x - 150, x + 150, 0, 320) for x in DOCKS_X] + [(x - 140, x + 140, 700, 820) for x in (-1400, 0, 1400)]
    north = [(-1200, -800, 0, 420), (800, 1200, 0, 420), (-100, 20, 0, 215)]
    west = [(900, 1300, 0, 420), (1800, 1920, 0, 215)]
    east = [(1000, 1120, 0, 215), (1800, 2200, 0, 420)]
    wall((HX0, HY0 + t / 2), (HX1, HY0 + t / 2), south)
    wall((HX0, HY1 - t / 2), (HX1, HY1 - t / 2), north)
    wall((HX0 + t / 2, HY0), (HX0 + t / 2, HY1), west)
    wall((HX1 - t / 2, HY0), (HX1 - t / 2, HY1), east)
    # Low-pitch roof with skylight strips, parapet band.
    b.box(HX0 - 20, HX1 + 20, HY0 - 20, HY1 + 20, EAVES, EAVES + 20, roof)
    for yy in (900, 1450, 2000):
        b.box(HX0 + 200, HX1 - 200, yy - 90, yy + 90, EAVES + 20, EAVES + 60, gl)
    b.box(HX0 - 25, HX1 + 25, HY0 - 25, HY1 + 25, EAVES - 60, EAVES, steel, faces="nsew")
    b.box(HX0 + t / 2, HX1 - t / 2, HY0 + t / 2, HY1 - t / 2, -10, 3, floor, faces="t")
    # Columns and roof trusses.
    for x in (-1100.0, 0.0, 1100.0):
        for y in (1300.0,):
            pass
    for x in (-1650.0, -550.0, 550.0, 1650.0):
        b.box(x - 15, x + 15, HY0, HY1, EAVES - 90, EAVES - 50, steel)
    # Shutters: docks 1, 3, 6 half down (cover on the platform), the others open.
    for (s, e, zb, zt), down in zip(south[:6], (160.0, 0.0, 200.0, 0.0, 0.0, 160.0)):
        b.box(s - 10, e + 10, HY0 - 30, HY0, zt, zt + 50, steel)
        if down > 0:
            b.box(s, e, HY0 + 10, HY0 + 16, zt - down, zt, shutter)
        b.box(s - 30, s, HY0 - 30, HY0, 0, zt + 20, rubber)
        b.box(e, e + 30, HY0 - 30, HY0, 0, zt + 20, rubber)
    for (s, e, zb, zt) in north[:2]:
        b.box(s - 10, e + 10, HY1, HY1 + 30, zt, zt + 50, steel)
    # Dock platform (outside the hall, level 0) with its wall to the sunk yard, bumpers, canopy.
    b.box(-2050, 2050, 100, HY0, YARD - 20, 0, S("Concrete_B"))
    b.box(-2050, 2050, 95, 105, -60, 0, yellow)
    for x in DOCKS_X:
        for dx in (-110, 110):
            det.add(kit.dock_bumper(), (x + dx, 100, YARD + 20), 0, collision=True)
    b.box(-2150, 2150, -200, HY0, 520, 532, roof)
    for x in range(-2100, 2200, 700):
        b.box(x - 10, x + 10, -190, -170, YARD, 520, steel)
    for x in (-800.0, 1000.0):
        kit.stair_flight(b, x, 100 - 8 * 28, YARD, 0, 140, 90, S("Concrete_B"))
        landing(b, x - 70, x + 70, 100, 180, 0, S("Concrete_B"))
    # Racks: two blocks of N-S rows, a cross aisle between them, a central aisle.
    r = lib.rng(11)
    box = old_prop("cardboard_box_01")
    for row_x in (-1400.0, -600.0, 600.0, 1400.0):
        for y0_, y1_ in ((550.0, 1150.0), (1450.0, 2050.0)):
            y = y0_
            while y + 270 <= y1_ + 1:
                det.add(kit.pallet_rack(), (row_x, y, 3), 90, collision=True, cull=10000)
                for level in (3.0, 140.0, 280.0):
                    for k in range(2):
                        if r.random() < 0.75:
                            py = y + 70 + k * 130
                            det.add(kit.pallet(), (row_x + 55, py, level), 90 + r.uniform(-3, 3), collision=True, cull=6000)
                            for j in range(r.randint(1, 3)):
                                det.add(box, (row_x + 55 + (j % 2 - 0.5) * 40, py, level + 14.4 + (j // 2) * 38.5), r.uniform(-8, 8), 1.1,
                                        collision=True, cull=5000)
                y += 270
            b.box(row_x + 110, row_x + 114, y0_, y, 0, 450, kit.paint("Paint_RackSheet", (0.25, 0.27, 0.28), dirt=0.3))
    big, small = kit.crate(120, 100, 100), kit.crate(100, 80, 80)

    def stack(x, y, yaw=0.0, pallets=3):
        for k in range(pallets):
            det.add(kit.pallet(), (x, y, k * 14.4), yaw + r.uniform(-4, 4), collision=True, cull=7000)
        det.add(big, (x, y, pallets * 14.4), yaw + r.uniform(-5, 5), collision=True)
        det.add(small, (x + 5, y - 5, pallets * 14.4 + 100), yaw + r.uniform(-15, 15), collision=True)
    for x in (-1400.0, 0.0, 1400.0):
        stack(x, 200, 90)
    for x, y in ((-1950, 1200), (1700, 2000), (-1000, 2380), (1000, 2380), (-1700, 1860), (1950, 1060), (60, 2400),
                 (-1000, 1200), (1000, 1340)):
        stack(x, y, r.uniform(0, 90))
    # Mezzanine along the north wall, catwalk over the cross aisle, connectors, stairs.
    grate = S("Metal_Grate")
    b.box(HX0 + t, HX1 - t, 2160, HY1 - t, MEZZ - 20, MEZZ, grate)
    b.box(HX0 + t, HX1 - t, 2160, 2180, MEZZ - 45, MEZZ - 20, steel)
    for x in range(int(HX0) + 300, int(HX1), 700):
        b.box(x - 10, x + 10, 2165, 2185, 3, MEZZ - 45, steel)
    b.box(HX0 + t, HX1 - t, 1230, 1370, MEZZ - 20, MEZZ, grate)                 # catwalk
    for x in range(int(HX0) + 300, int(HX1), 700):
        b.box(x - 8, x + 8, 1290, 1310, 3, MEZZ - 20, steel)
    for x0 in (HX0 + t, HX1 - t - 140):
        b.box(x0, x0 + 140, 1370, 2160, MEZZ - 20, MEZZ, grate)                 # connectors
    for sx in (-1900.0, 1900.0):
        n = int(round(MEZZ / 17.5))
        kit.stair_flight(b, sx, 2160 - n * 28, 0, MEZZ, 140, 90, grate, steel, solid=False)
        kit.railing_run(det, (sx - 73, 2160 - n * 28, 0), (sx - 73, 2160, MEZZ))
        kit.railing_run(det, (sx + 73, 2160 - n * 28, 0), (sx + 73, 2160, MEZZ))
    kit.railing_run(det, (-1820, 2160, MEZZ), (1820, 2160, MEZZ))
    for y in (1230.0, 1370.0):
        kit.railing_run(det, (HX0 + t + 140, y, MEZZ), (HX1 - t - 140, y, MEZZ))
    for x in (HX0 + t + 140, HX1 - t - 140):
        kit.railing_run(det, (x, 1370, MEZZ), (x, 2160, MEZZ))
    # Offices on the mezzanine: glass-fronted boxes (cover, close quarters up there).
    for ox0, ox1 in ((-1500.0, -700.0), (700.0, 1500.0)):
        kit.wall_openings(b, (ox0, 2310), (ox1, 2310), MEZZ, MEZZ + 260, 12, S("Plaster_White"),
                          [(ox0 + 100, ox0 + 220, MEZZ, MEZZ + 215), (ox0 + 300, ox1 - 80, MEZZ + 100, MEZZ + 220)])
        kit.wall_openings(b, (ox0, 2310), (ox0, HY1 - t), MEZZ, MEZZ + 260, 12, S("Plaster_White"), [])
        kit.wall_openings(b, (ox1, 2310), (ox1, HY1 - t), MEZZ, MEZZ + 260, 12, S("Plaster_White"), [])
        b.box(ox0, ox1, 2310, HY1 - t, MEZZ + 260, MEZZ + 270, S("Concrete_B"))
        b.box(ox0 + 300, ox1 - 80, 2306, 2314, MEZZ + 100, MEZZ + 220, kit.glass("Office", tint=(0.08, 0.1, 0.12)))
    # Overhead crane: runway beams along the long walls, the bridge girder, hoist and hook.
    craneY = kit.paint("Paint_CraneYellow", (0.6, 0.42, 0.02), rough=0.4, dirt=0.25)
    for y in (HY0 + 60, HY1 - 60):
        b.box(HX0 + t, HX1 - t, y - 20, y + 20, 820, 870, steel)
    cx = 300.0
    b.box(cx - 40, cx + 40, HY0 + 40, HY1 - 40, 870, 950, craneY)
    b.box(cx - 90, cx + 90, 1350, 1550, 820, 870, craneY)
    kit.cyl(b, (cx, 1450, 820), (cx, 1450, 520), 3, S("Metal_Plain"), seg=6, caps=False)
    b.box(cx - 25, cx + 25, 1425, 1475, 470, 520, craneY)
    lib.place(lib.save_mesh(b, OUT + "/SM_Warehouse_Hall", collision="complex"), folder="Hall", label="Hall")
    # Doors, lamps, signs, floor markings.
    det.add(kit.door(120, 215, "steel"), (-40, HY1 + 15, 0), 180)
    det.add(kit.door(120, 215, "steel"), (HX0 - 15, 1860, 0), -90)
    det.add(kit.door(120, 215, "steel"), (HX1 + 15, 1060, 0), 90)
    for x in (-1650.0, -550.0, 550.0, 1650.0):
        for y in (700.0, 1450.0, 2200.0):
            det.add(lib.prop("mounted_fluorescent_lights"), (x, y, EAVES - 110), 0, cull=12000)
            lib.point_light((x, y, EAVES - 160), intensity=1600, radius=1600, color=(0.9, 0.95, 1.0))
    for x in DOCKS_X:
        det.add(lib.prop("security_light"), (x, HY0 - 40, 380), -90, cull=10000)
        lib.point_light((x, HY0 - 120, 350), intensity=600, radius=700, color=(1.0, 0.55, 0.2))   # orange dock lights
    det.add(kit.sign_panel(), (0, HY0 - 16, 900), 0, scl=(7.0, 1.0, 7.0), materials=[kit.sign_material(kit.SIGN["nordlog"]), None])
    for i, x in enumerate(DOCKS_X):
        det.add(kit.sign_panel(), (x, HY0 - 16, 400), 0, scl=(1.4, 1.0, 1.4), materials=[kit.sign_material(kit.SIGN["dock"]), None])
    for y in (1150.0, 1450.0):
        lib.decal(lib.LINE, (0, y, 3), (60, 4200, 64), yaw=0, tint=(1.3, 1.1, 0.35))
    for x in (-200.0, 200.0):
        lib.decal(lib.LINE, (x, 1450, 3), (60, 2200, 64), yaw=90, tint=(1.3, 1.1, 0.35))
    for i in range(10):
        lib.decal(r.choice([lib.OIL_SMALL, lib.DIRT, lib.TYRES]), (r.uniform(-2000, 2000), r.uniform(400, 2500), 3),
                  (60, r.uniform(150, 300), r.uniform(150, 300)), yaw=r.uniform(0, 360), opacity=0.8)
    lib.reverb(HX0, HX1, HY0, HY1, 0, EAVES, "RE_Hall", 0.7)


# ---------------------------------------------------------------------------
# Truck yard, lot, office
# ---------------------------------------------------------------------------

def build_yard(det):
    b = lib.MeshBuilder(ground=ground)
    conc = S("Concrete_Wall")
    # Yard south wall (retaining the lot at 0), with a gap at the middle for a stair.
    b.box(-2000, -300, -1740, -1700, YARD - 60, 90, conc)
    b.box(300, 2000, -1740, -1700, YARD - 60, 90, conc)
    n = int(round(-YARD / 17.5))
    kit.stair_flight(b, 0, -1700 + n * 28, YARD, 0, 560, -90, S("Concrete_B"))
    landing(b, -280, 280, -1780, -1700, 0, S("Concrete_B"))
    lib.place(lib.save_mesh(b, OUT + "/SM_Warehouse_Yard", collision="complex"), folder="Yard", label="Yard")
    colors = kit.container_colors()
    det.add(kit.trailer(), (-1050, 100 - 680 - 40, YARD), 90, collision=True, materials=[colors[4]])
    det.add(kit.trailer(), (1050, 100 - 680 - 40, YARD), 90, collision=True, materials=[colors[1]])
    det.add(kit.trailer(), (100, -1400, YARD), 8, collision=True, materials=[colors[3]])
    for lvl in range(2):
        det.add(kit.container(606), (-1300, -1550, YARD + lvl * 259), lvl * 2.0, collision=True, materials=[colors[(2 + lvl * 3) % 8], None])
    r = lib.rng(21)
    for i in range(12):
        lib.decal(r.choice([lib.OIL, lib.TYRES, lib.OIL_SMALL]), (r.uniform(-1900, 1900), r.uniform(-1600, 0), YARD),
                  (100, r.uniform(200, 450), r.uniform(200, 400)), yaw=r.uniform(0, 360), opacity=0.8)
    for x in range(-1800, 1900, 500):
        lib.decal(lib.LINE, (x, -700, YARD), (100, 1200, 70), yaw=90, tint=(1.3, 1.3, 1.3), opacity=0.85)
    lib.decal(lib.WET, (-400, -900, YARD), (80, 600, 450), yaw=15, roughness=0.08, opacity=0.8)
    # The lot south of the yard: a guard booth with a barrier, a fuel island, parked trucks' trailers.
    lot = lib.MeshBuilder(ground=ground)
    booth = S("Plaster_White")
    bx0, bx1, by0, by1 = 1600.0, 1900.0, -2900.0, -2600.0
    kit.wall_openings(lot, (bx0, by0), (bx1, by0), 0, 260, 10, booth, [(1650, 1850, 100, 200)])
    kit.wall_openings(lot, (bx0, by1), (bx1, by1), 0, 260, 10, booth, [(1650, 1850, 100, 200)])
    kit.wall_openings(lot, (bx0, by0), (bx0, by1), 0, 260, 10, booth, [(-2800, -2680, 0, 215)])
    kit.wall_openings(lot, (bx1, by0), (bx1, by1), 0, 260, 10, booth, [])
    lot.box(bx0 - 40, bx1 + 40, by0 - 40, by1 + 40, 260, 275, S("Metal_BoxProfile"))
    mx0, mx1, my0, my1 = 300.0, 1500.0, -2450.0, -1950.0
    blk = kit.tinted("Metal_Cladding_Blue", "Metal_SandwichGreyBlue", (0.78, 0.86, 0.95), desat=0.4)
    kit.wall_openings(lot, (mx0, my1), (mx1, my1), -20, 420, 20, blk, [(600, 1000, 0, 360)])
    kit.wall_openings(lot, (mx0, my0), (mx1, my0), -20, 420, 20, blk, [(700, 820, 0, 215)])
    kit.wall_openings(lot, (mx0, my0), (mx0, my1), -20, 420, 20, blk, [(-2300, -2100, 150, 250)])
    kit.wall_openings(lot, (mx1, my0), (mx1, my1), -20, 420, 20, blk, [(-2300, -2100, 150, 250)])
    lot.box(mx0 - 30, mx1 + 30, my0 - 30, my1 + 30, 420, 440, S("Metal_BoxProfile"))
    lot.box(mx0, mx1, my0, my1, -5, 3, S("Concrete_Painted"), faces="t")
    lot.box(600, 1000, my1 - 6, my1 + 6, 200, 360, S("Metal_Shutter"))
    lot.box(-900, 100, -2600, -2300, -5, 20, S("Concrete_B"))                  # fuel island
    lot.box(-950, 150, -2650, -2250, 450, 470, S("Metal_BoxProfile"))
    for x in (-850.0, 50.0):
        for y in (-2600.0, -2300.0):
            lot.box(x - 12, x + 12, y - 12, y + 12, 20, 450, kit.paint("Paint_SteelGrey", (0.12, 0.13, 0.14)))
    for x in (-600.0, -200.0):
        lot.box(x - 50, x + 50, -2480, -2420, 20, 210, kit.paint("Paint_PumpRed", (0.45, 0.03, 0.02), rough=0.4))
    lib.place(lib.save_mesh(lot, OUT + "/SM_Warehouse_Lot", collision="complex"), folder="Lot", label="Lot")
    det.add(kit.trailer(), (-1400, -2900, 0), 2, collision=True, materials=[colors[0]])
    det.add(kit.trailer(), (-1300, -2150, 0), 178, collision=True, materials=[colors[6]])
    det.add(kit.door(120, 215, "steel"), (760, my0 - 10, 0), 0)
    for k in range(3):
        det.add(kit.pallet(), (1200, -2250, k * 14.4), 0, collision=True)
    det.add(kit.crate(120, 100, 100), (1200, -2250, 43.2), 10, collision=True)
    for x, y, yaw in ((-2100, -2000, 90), (1700, -2200, 20), (2100, -1900, 90)):
        det.add(lib.prop("concrete_road_barrier_02"), (x, y, 0), yaw, collision=True)
    det.add(kit.window(150, 100, "steel"), (1750, by0 - 6, 100), 0)
    det.add(kit.door(120, 215, "steel"), (bx0 - 6, -2740, 0), -90)


def build_office(det):
    b = lib.MeshBuilder(ground=ground)
    conc = S("Concrete_Wall")
    # Podium with a ramp (east) and a stair (north).
    b.box(PX0, PX1, PY0, PY1, -60, POD, conc, faces="nsew")
    b.box(PX0, PX1, PY0, PY1, POD - 5, POD, S("Concrete_Pavers"), faces="t")
    kit.stair_flight(b, -2550, PY1 + 7 * 28, 0, POD, 300, -90, S("Concrete_B"))
    landing(b, -2700, -2400, PY1 - 80, PY1, POD, S("Concrete_B"))
    for x in (PX0, PX1):
        pass
    # Office: two floors, glass bands on the east and north faces, a door east.
    f1, f2, roof = POD, POD + 350.0, POD + 700.0
    plaster, frame = S("Plaster_White"), kit.paint("Paint_FrameGrey", (0.1, 0.11, 0.12))
    glass = kit.glass("OfficeGlass", tint=(0.07, 0.09, 0.11))
    t = 25.0
    bands = [(f1 + 90, f1 + 300), (f2 + 90, f2 + 300)]
    ew = [(y, y + 280, lo, hi) for y in range(int(OY0) + 100, int(OY1) - 300, 320) for lo, hi in bands]
    ew = [o for o in ew if not (o[0] <= -2250 <= o[1] and o[2] < f2)]
    ew.append((-2330, -2170, f1, f1 + 230))                                            # door
    nw = [(x, x + 280, lo, hi) for x in range(int(OX0) + 100, int(OX1) - 300, 320) for lo, hi in bands]
    kit.wall_openings(b, (OX1 - t / 2, OY0), (OX1 - t / 2, OY1), f1, roof, t, plaster, ew)
    kit.wall_openings(b, (OX0, OY1 - t / 2), (OX1, OY1 - t / 2), f1, roof, t, plaster, nw)
    kit.wall_openings(b, (OX0, OY0 + t / 2), (OX1, OY0 + t / 2), f1, roof, t, plaster, [])
    kit.wall_openings(b, (OX0 + t / 2, OY0), (OX0 + t / 2, OY1), f1, roof, t, plaster, [])
    # Glass panes (solid) in every window band, mullions in front.
    for s, e, lo, hi in ew:
        if lo > f1:
            b.box(OX1 - 14, OX1 - 10, s, e, lo, hi, glass)
    for s, e, lo, hi in nw:
        b.box(s, e, OY1 - 14, OY1 - 10, lo, hi, glass)
    for s, e, lo, hi in ew + nw:
        pass
    # Floors: ground floor, first floor slab, roof slab with a parapet.
    b.box(OX0 + t, OX1 - t, OY0 + t, OY1 - t, f1 - 5, f1 + 3, S("Stone_Tiles"), faces="t")
    b.box(OX0 + t, OX1 - t, OY0 + t, OY1 - t, f2 - 20, f2, S("Concrete_B"))
    b.box(OX0 - 10, OX1 + 10, OY0 - 10, OY1 + 10, roof, roof + 20, S("Concrete_B"))
    for (a, c), gaps in ((((OX0, OY0 + 10), (OX1, OY0 + 10)), []), (((OX0, OY1 - 10), (OX1, OY1 - 10)), [(-3300, -3160, roof + 20, roof + 120)]),
                         (((OX0 + 10, OY0), (OX0 + 10, OY1)), []), (((OX1 - 10, OY0), (OX1 - 10, OY1)), [])):
        kit.wall_openings(b, a, c, roof + 20, roof + 120, 20, plaster, gaps)
    # Plant screen on the roof's east and south edges: the roof overlooks the parking and the
    # podium, not the whole map (a look from 8.5 m over the east would run 80 m).
    screen = kit.tinted("Metal_Cladding_Blue", "Metal_SandwichGreyBlue", (0.78, 0.86, 0.95), desat=0.4)
    b.box(OX1 - 30, OX1 - 10, OY0, OY1, roof + 120, roof + 240, screen)
    b.box(OX0, OX1, OY0 + 10, OY0 + 30, roof + 120, roof + 240, screen)
    b.box(-3160, OX1, OY1 - 30, OY1 - 10, roof + 120, roof + 240, screen)
    # Lobby partition facing the door: no look straight through the office door.
    b.box(-2870, -2850, -2450, -2050, f1, f1 + 250, plaster)
    # Fire stair from the parking up to the office roof: two straight flights with a landing,
    # arriving through a gap in the roof's north parapet (the office's high position).
    steel = kit.paint("Paint_SteelGrey", (0.12, 0.13, 0.14), dirt=0.3)
    grate = S("Metal_Grate")
    fx = -3230.0
    top = roof + 20
    half = top / 2
    n = int(round(half / 17.5))
    y1 = OY1 + 2 * n * 28 + 140                      # the first flight starts here, on the parking
    kit.stair_flight(b, fx, y1, 0, half, 140, -90, grate, steel, solid=False)
    b.box(fx - 70, fx + 70, y1 - n * 28 - 140, y1 - n * 28, half - 20, half, grate)
    kit.stair_flight(b, fx, y1 - n * 28 - 140, half, top, 140, -90, grate, steel, solid=False)
    for yy in (y1 - n * 28 - 140, y1 - n * 28):
        for xx in (fx - 62, fx + 62):
            b.box(xx - 7, xx + 7, yy - 7, yy + 7, ground(xx, yy), half - 20, steel)
    lib.place(lib.save_mesh(b, OUT + "/SM_Warehouse_Office", collision="complex"), folder="Office", label="Office")
    for xx in (fx - 73, fx + 73):
        kit.railing_run(det, (xx, y1, 0), (xx, y1 - n * 28, half))
        kit.railing_run(det, (xx, y1 - n * 28 - 140, half), (xx, OY1, top))
    # Interior: desks (crates), a counter; HVAC units on the roof.
    r = lib.rng(31)
    for x, y in ((-3300, -2400), (-3000, -2400), (-3300, -2000), (-3000, -2000)):
        det.add(kit.crate(140, 70, 75), (x, y, f1 + 3), r.uniform(-5, 5), collision=True)
    for x, y in ((-3400, -2500), (-2900, -1900), (-3200, -2200)):
        det.add(lib.prop("exterior_aircon_unit"), (x, y, roof + 20), r.choice([0, 90, 180]), 2.2, collision=True)
    det.add(kit.sign_panel(), (OX1 + 14, -2000, f2 + 380), 90, scl=(4.0, 1.0, 4.0), materials=[kit.sign_material(kit.SIGN["office"]), None])
    det.add(kit.door(160, 230, "steel"), (OX1 + 12, -2250, f1), 90)
    lib.reverb(OX0, OX1, OY0, OY1, f1, roof, "RE_Room", 0.5)


# ---------------------------------------------------------------------------
# Parking, berm, storage units, tech zone, container yard
# ---------------------------------------------------------------------------

def build_parking(det):
    r = lib.rng(41)
    b = lib.MeshBuilder(ground=ground)
    kerb = S("Concrete_Wall")
    for tx, ty in PARK_TREES:
        b.box(tx - 170, tx + 170, ty - 170, ty + 170, -5, 15, kerb, faces="nsew")
    b.box(-3500, -3480, -1000, 2600, -5, 15, kerb)
    # A bike shelter and a smoking shelter: cover for Alpha's exits.
    b.box(-3350, -2750, 2350, 2550, 0, 240, kit.paint("Paint_SteelGrey", (0.12, 0.13, 0.14)), faces="n")
    b.box(-3370, -2730, 2330, 2570, 240, 254, S("Metal_BoxProfile"))
    for x in (-3340, -3050, -2760):
        b.box(x - 6, x + 6, 2340, 2352, 0, 240, kit.paint("Paint_SteelGrey", (0.12, 0.13, 0.14)))
    lib.place(lib.save_mesh(b, OUT + "/SM_Warehouse_Parking", collision="complex"), folder="Parking", label="Parking")
    car = lib.prop("covered_car")
    for x, y, yaw in ((-3200, 400, 90), (-3200, 1700, 92), (-2650, 800, 270), (-2650, 1500, 268), (-2650, -600, 272)):
        det.add(car, (x, y, 0), yaw + r.uniform(-3, 3), collision=True)
    for tx, ty in PARK_TREES:
        det.add(kit.tree(r.randint(0, 2)), (tx, ty, 10), r.uniform(0, 360), r.uniform(0.9, 1.1), collision=True, cull=0)
    for x, y in ((-3450, -500), (-3450, 900), (-3450, 2100)):
        det.add(kit.lamp_pole(800), (x, y, 0), 0, collision=True)
        det.add(lib.prop("security_light"), (x + 120, y, 780), 0, cull=12000)
    for y in range(-800, 2400, 250):
        for xc in (-3200, -2650):
            lib.decal(lib.LINE, (xc, y, 0), (100, 480, 77), yaw=90, opacity=0.9)
    kit.grass_patch(det, r, -3800, 1000, 900, 3, ground, keep=lambda x, y: x < -3500)
    for _ in range(18):
        x, y = r.uniform(-3980, -3700), r.uniform(-3100, 3100)
        det.add(lib.prop("shrub_02", r.choice(["shrub_02_a_LOD0", "shrub_02_b_LOD0", "shrub_02_d_LOD0"])), (x, y, ground(x, y) - 5),
                r.uniform(0, 360), r.uniform(0.6, 1.0), cull=8000)


def build_storage(det):
    """Storage units along the north edge, the service road behind the hall."""
    b = lib.MeshBuilder(ground=ground)
    wall, shutter = S("Concrete_Block"), S("Metal_Shutter")
    roof = S("Metal_BoxProfile")
    units = [(-2200 + i * 550, -2200 + i * 550 + 520) for i in range(8)]
    for i, (x0, x1) in enumerate(units):
        y0 = 2900.0 if i not in (2, 5) else 2750.0                    # two units step forward: no 80 m road
        b.box(x0, x1, y0, 3150, -20, 320, wall, faces="nsew")
        b.box(x0 - 15, x1 + 15, y0 - 30, 3160, 320, 335, roof)
        b.box(x0 + 60, x1 - 60, y0 - 6, y0 - 2, 0, 280, shutter)
        det.add(kit.sign_panel(), ((x0 + x1) / 2, y0 - 8, 300), 0, scl=(0.8, 1.0, 0.8), materials=[kit.sign_material(kit.SIGN["personnel"]), None])
    lib.place(lib.save_mesh(b, OUT + "/SM_Warehouse_Storage", collision="complex"), folder="Storage", label="Storage")
    r = lib.rng(51)
    for x, y in ((-1700, 2700), (300, 2760), (1600, 2700)):
        det.add(lib.prop("metal_trash_can"), (x, y, 0), r.uniform(-10, 10), 1.5, collision=True)
    det.add(kit.container(606), (0, 2730, 0), 2, collision=True, materials=[kit.container_colors()[3], None])


def build_tech(det):
    """Tech zone: transformers, chillers, ducts, a generator container, narrow service passages."""
    b = lib.MeshBuilder(ground=ground)
    pad, steel = S("Concrete_B"), kit.paint("Paint_SteelGrey", (0.12, 0.13, 0.14), dirt=0.4)
    green = kit.paint("Paint_TransformerGreen", (0.1, 0.17, 0.12), dirt=0.5)
    white = kit.paint("Paint_ChillerWhite", (0.5, 0.52, 0.52), dirt=0.4)
    # Transformers on a pad behind a fence.
    b.box(2500, 3700, 2300, 3100, -10, 10, pad, faces="nsewt")
    for x in (2800.0, 3400.0):
        b.box(x - 150, x + 150, 2550, 2850, 10, 260, green)
        for k in range(9):
            y = 2560 + k * 30
            b.box(x - 190, x - 150, y, y + 5, 30, 230, green)
            b.box(x + 150, x + 190, y, y + 5, 30, 230, green)
    # Chillers: big boxes with fan rings on top, in a row with passages between.
    for x0 in (2500.0, 3050.0, 3600.0):
        b.box(x0, x0 + 380, 1400, 1900, 0, 230, white)
        for fx in (x0 + 95, x0 + 285):
            for fy in (1525, 1775):
                kit.cyl(b, (fx, fy, 230), (fx, fy, 260), 80, steel, seg=12)
    # Ducts along the hall's east wall and over to the chillers.
    duct = S("Metal_Plain")
    b.box(HX1 + 30, HX1 + 150, 600, 2500, 450, 570, duct)
    b.box(HX1 + 150, 2500, 1600, 1720, 450, 570, duct)
    for y in (700, 1300, 2000):
        b.box(HX1 + 80, HX1 + 100, y - 10, y + 10, 0, 450, steel)
    # Generator container.
    lib.place(lib.save_mesh(b, OUT + "/SM_Warehouse_Tech", collision="complex"), folder="Tech", label="Tech")
    det.add(kit.container(606), (3300, 900, 0), 0, collision=True, materials=[kit.container_colors()[0], None])
    det.add(lib.prop("portable_generator"), (2700, 900, 0), 90, 1.4, collision=True)
    for x, y in ((2600, 2250), (3700, 2250)):
        det.add(lib.prop("power_box_01"), (x, y, 60), 0, collision=True)
    kit_fence = lib.prop("modular_chainlink_fence", "modular_chainlink_fence")
    if kit_fence:
        for x in range(2550, 3700, 93):
            if not (2950 <= x <= 3150):
                det.add(kit_fence, (x, 2300, 0), 0, collision=True, cull=9000)
    lib.reverb(2400, X, 500, Y, 0, 600, "RE_Alley", 0.35)


CONTAINERS = [  # (x, y, length, levels, yaw, colours)
    (2800.0, -2600.0, 1219.0, 2, 0.0, (1, 5)),
    (3400.0, -1800.0, 606.0, 1, 90.0, (2,)),
    (2700.0, -1300.0, 606.0, 2, 3.0, (6, 0)),
    (3600.0, -2900.0, 606.0, 1, 0.0, (3,)),
    (2600.0, -700.0, 1219.0, 1, 0.0, (4,)),
]


def build_containers(det):
    colors = kit.container_colors()
    for x, y, length, levels, yaw, cols in CONTAINERS:
        for lvl in range(levels):
            det.add(kit.container(length), (x, y, lvl * 259), yaw + lvl * 1.5, collision=True, materials=[colors[cols[lvl]], None], cull=0)
    b = lib.MeshBuilder(ground=ground)
    steel = kit.paint("Paint_SteelGrey", (0.12, 0.13, 0.14), dirt=0.4)
    n = int(round(259 / 17.5))
    kit.stair_flight(b, 3300, -2478 + 2 + 244 + n * 28, 0, 259, 140, -90, S("Metal_Grate"), steel, solid=False)
    lib.place(lib.save_mesh(b, OUT + "/SM_Warehouse_ContainerStairs", collision="complex"), folder="Containers", label="ContainerStairs")
    r = lib.rng(61)
    for x, y in ((3100, -2100), (2400, -1900), (3700, -1300)):
        det.add(kit.pallet(), (x, y, 0), r.uniform(0, 90), collision=True)
        det.add(kit.crate(120, 100, 100), (x, y, 14.4), r.uniform(0, 90), collision=True)
    lib.reverb(2300, X, -3200, -500, 0, 520, "RE_Alley", 0.3)


def build_perimeter(det):
    b = lib.MeshBuilder(ground=ground)
    wall = S("Concrete_Block")
    for (x0, x1, y0, y1) in ((-X, X, -Y - 30, -Y), (-X, X, Y, Y + 30), (X, X + 30, -Y, Y), (-X - 30, -X, -Y, Y)):
        b.box(x0, x1, y0, y1, -60, 360, wall)
    lib.place(lib.save_mesh(b, OUT + "/SM_Warehouse_Perimeter", collision="complex"), folder="Perimeter", label="Perimeter")
    for x0, x1, y0, y1 in ((-X - 60, X + 60, -Y - 60, -Y + 60), (-X - 60, -X + 60, -Y, Y), (X - 60, X + 60, -Y, Y), (-X, X, Y - 60, Y + 60)):
        lib.blocking_box(x0, x1, y0, y1, -500, 2600, "Edge")


def build_backdrop(det):
    b = lib.MeshBuilder(ground=kit.NOGRIME)
    r = lib.rng(71)
    clad = [S("Metal_Cladding_Blue"), S("Metal_Cladding"), S("Metal_Corrugated")]
    for i in range(18):
        side = i % 4
        if side == 0:
            x, y = r.uniform(-9000, 9000), r.uniform(-9000, -5000)
        elif side == 1:
            x, y = r.uniform(-9000, 9000), r.uniform(5000, 9000)
        elif side == 2:
            x, y = r.uniform(-10000, -5500), r.uniform(-6000, 6000)
        else:
            x, y = r.uniform(5500, 10000), r.uniform(-6000, 6000)
        w, d, hgt = r.uniform(2000, 4500), r.uniform(1500, 3000), r.uniform(900, 1600)
        b.box(x - w / 2, x + w / 2, y - d / 2, y + d / 2, -100, hgt, r.choice(clad))
        b.box(x - w / 2 - 30, x + w / 2 + 30, y - d / 2 - 30, y + d / 2 + 30, hgt, hgt + 25, S("Metal_BoxProfile"))
    kit.cyl(b, (7500, 6500, -100), (7500, 6500, 3500), 200, S("Concrete_Wall"), seg=16, r1=160)      # exhaust stack
    lib.place(lib.save_mesh(b, OUT + "/SM_Warehouse_Backdrop", collision="none"), folder="Backdrop", label="Backdrop", collision=False)
    lib.point_light((7500, 6500, 3550), intensity=600, radius=900, color=(1.0, 0.1, 0.05))


# ---------------------------------------------------------------------------
# Gameplay, light, views
# ---------------------------------------------------------------------------

ALPHA = [(-2950, 250), (-3200, 900), (-3200, 1200), (-2950, 500), (-3000, 1400), (-2650, 200), (-2650, 1150), (-2650, 1900),
         (-3200, 2000), (-2950, -800)]
BRAVO = [(3200, -200), (3600, -200), (3900, 0), (3200, 200), (3600, 250), (3900, 400), (2800, 0), (3400, -500),
         (3800, -500), (2900, 350)]
FREE = [(0, 1300, 0), (-1000, 1800, 0), (1000, 800, 0), (-1600, 2400, MEZZ), (1600, 2400, MEZZ), (-1000, 1300, MEZZ),
        (-1500, -800, None), (1500, -300, None), (-400, -2200, None), (1300, -2700, None), (-3150, -2200, POD),
        (2900, -2000, None), (3300, 1200, None), (3000, 2100, None), (-2000, 2750, None), (1300, 2750, None), (-3700, 2800, None)]


def gameplay():
    for x, y in ALPHA:
        lib.start("Alpha", x, y, 0, ground(x, y))
    for x, y in BRAVO:
        lib.start("Bravo", x, y, 180, ground(x, y))
    for x, y, z in FREE:
        lib.start("", x, y, math.degrees(math.atan2(-y, -x)) if (x or y) else 0, ground(x, y) if z is None else z)
    lib.machine(-3450, 300, 0, ground(-3450, 300))
    lib.machine(3950, -800, 180, 0)
    lib.machine(-100, 2560, -90, 0)
    lib.machine(1900, -1650, 90, YARD)
    lib.navmesh(-X, X, -Y, Y, -400, 1500)


def views():
    e = 165.0

    def v(label, x, y, yaw, z=None, pitch=-3):
        return (label, (x, y, (ground(x, y) if z is None else z) + e), pitch, yaw)
    lib.write_views("warehouse", [
        v("alpha", -2900, 800, 0), v("hall_w", -2000, 1300, 0), v("hall_aisle", 0, 700, 90), v("mezz", -1200, 2350, 0, MEZZ),
        v("catwalk", -1500, 1300, 0, MEZZ, -8), v("dock", -2300, 200, 0), v("yard", -2300, -800, 0, None), v("lot", -1800, -2500, 30),
        v("office_roof", -3100, -2300, 45, POD + 720, -6), v("office_in", -2750, -2250, 180, POD), v("service", -2100, 2750, 0),
        v("tech", 2600, 2100, -90), v("containers", 2400, -1600, 0), v("bravo", 3500, 0, 180), v("berm", -3850, 0, 90),
        ("over_s", (0, -8500, 3500), -24, 90), ("over_n", (0, 8500, 3800), -26, -90), ("over_top", (-6000, -6000, 5000), -38, 45)])


def build():
    lib.new_level(LEVEL)
    build_terrain()
    det = lib.Cluster("WarehouseDetail")
    build_hall(det)
    build_yard(det)
    build_office(det)
    build_parking(det)
    build_storage(det)
    build_tech(det)
    build_containers(det)
    build_perimeter(det)
    build_backdrop(det)
    det.build()
    lib.log("detail instances: %d in %d groups" % (det.count(), len(det.groups)))
    gameplay()
    lib.environment(sun_pitch=-55, sun_yaw=150, sun_lux=3.2, sun_color=(0.93, 0.95, 1.0), sky_intensity=2.0, sky_tint=(0.92, 0.95, 1.0),
                    fog_density=0.02, fog_falloff=0.2, exposure=(0.9, 2.4, 0.3), contrast=1.0, saturation=0.92, temperature=6800.0)
    views()
    lib.save_level()


if __name__ == "__main__" or True:
    try:
        build()
        lib.log("done")
    except Exception as ex:
        import traceback
        lib.log("FAILED: %s\n%s" % (ex, traceback.format_exc()))
    lib.flush_log("warehouse.txt")
