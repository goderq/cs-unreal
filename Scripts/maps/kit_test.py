"""
v2.1 map rework: a small test level for the building blocks (lib.py) - winding,
UVs, terrain layers, grime, clusters, decals, lights. Not part of the game.

    UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script=Scripts/maps/kit_test.py
    UnrealEditor.exe CSFusion.uproject /Game/Maps/Dev/Lvl_KitTest -game -cstestviews=kittest
"""

import math
import os
import sys

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal  # noqa: E402
import lib  # noqa: E402
import kit  # noqa: E402

LEVEL = "/Game/Maps/Dev/Lvl_KitTest"
OUT = "/Game/Environment/Generated/KitTest"


def terrain_material(name, variants):
    """Terrain instance from surface variants: layer i takes variant i's textures and tile size."""
    mel = unreal.MaterialEditingLibrary
    folder = OUT
    lib.ensure_dir(folder)
    path = folder + "/" + name
    mi = lib.EAL.load_asset(path) if lib.EAL.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, lib.load(lib.V21 + "/Materials/M_CS_Terrain"))
    for i, v in enumerate(variants):
        src = lib.surface(v)
        for p in ("Diffuse", "Normal", "ARM"):
            mel.set_material_instance_texture_parameter_value(mi, "%s%d" % (p, i), mel.get_material_instance_texture_parameter_value(src, p))
        mel.set_material_instance_scalar_parameter_value(mi, "Tile%d" % i, mel.get_material_instance_scalar_parameter_value(src, "TileSize"))
    lib.EAL.save_loaded_asset(mi, only_if_is_dirty=False)
    return mi


def ground(x, y):
    hill = 220.0 * math.exp(-((x - 800) ** 2 + (y - 700) ** 2) / (2 * 450.0 ** 2))
    ditch = -60.0 * math.exp(-((y + 900) ** 2) / (2 * 90.0 ** 2)) if x < 600 else 0.0
    return hill + ditch


def layers(x, y):
    h = ground(x, y)
    dirt = max(0.0, min(1.0, (abs(y + 900) < 250) * 1.0 + h / 120.0 * 0.5))
    grass = max(0.0, min(1.0, h / 90.0))
    gravel = 1.0 if (x < -1200 and y > 400) else 0.0
    return (dirt, grass, gravel)


def wall_with_openings(b, a, c, z0, z1, t, mat, openings, trim_mat):
    """Straight axis-aligned wall a->c (outside on the right of a->c) with openings (s, e, bottom, top)."""
    horizontal = abs(c[1] - a[1]) < 1
    lo, hi = (min(a[0], c[0]), max(a[0], c[0])) if horizontal else (min(a[1], c[1]), max(a[1], c[1]))
    fixed = a[1] if horizontal else a[0]

    def piece(s, e, zb, zt):
        if e - s < 1 or zt - zb < 1:
            return
        if horizontal:
            b.box(s, e, fixed - t / 2, fixed + t / 2, zb, zt, mat)
        else:
            b.box(fixed - t / 2, fixed + t / 2, s, e, zb, zt, mat)

    cursor = lo
    for s, e, zb, zt in sorted(openings):
        piece(cursor, s, z0, z1)
        piece(s, e, z0, zb)
        piece(s, e, zt, z1)
        # A deep reveal frame and a sill in the trim material.
        if horizontal:
            b.box(s - 6, e + 6, fixed - t / 2 - 4, fixed - t / 2, zt, zt + 12, trim_mat)
            if zb > z0 + 1:
                b.box(s - 8, e + 8, fixed - t / 2 - 8, fixed - t / 2, zb - 6, zb, trim_mat)
        else:
            b.box(fixed + t / 2, fixed + t / 2 + 4, s - 6, e + 6, zt, zt + 12, trim_mat)
            if zb > z0 + 1:
                b.box(fixed + t / 2, fixed + t / 2 + 8, s - 8, e + 8, zb - 6, zb, trim_mat)
        cursor = e
    piece(cursor, hi, z0, z1)


def build():
    lib.new_level(LEVEL)
    tmat = terrain_material("MI_Terrain_KitTest", ["Asphalt_Old", "Ground_Dirt", "Ground_Grass", "Ground_Gravel"])
    dm, mats, tris = lib.terrain_mesh(-2000, 2000, -2000, 2000, 50.0, ground, layers, tmat)
    terrain = lib.save_mesh(dm, OUT + "/SM_Terrain", mats, collision="complex")
    lib.place(terrain, folder="Terrain", label="Terrain")
    lib.log("terrain %d triangles" % tris)

    # A two-storey block: plaster over a stone plinth, windows with reveals, cornice, parapet.
    plaster, stone, trim, roof = lib.surface("Plaster_Ochre"), lib.surface("Stone_Blocks"), lib.surface("Plaster_White"), lib.surface("Concrete_A")
    b = lib.MeshBuilder(ground=ground)
    x0, x1, y0, y1, h = -1400, -600, -400, 200, 700
    windows = [(x0 + 120, x0 + 240, 90, 230), (x0 + 400, x0 + 520, 90, 230), (x0 + 120, x0 + 240, 400, 560), (x0 + 400, x0 + 520, 400, 560),
               (x0 + 600, x0 + 700, 0, 240)]
    wall_with_openings(b, (x0, y0), (x1, y0), 60, h, 30, plaster, [(s, e, max(60, zb), zt) for s, e, zb, zt in windows], trim)
    b.box(x0 - 5, x1 + 5, y0 - 20, y1 + 5, -40, 60, stone)  # plinth
    b.box(x0, x1, y1 - 30, y1, 60, h, plaster)
    b.box(x0, x0 + 30, y0, y1, 60, h, plaster)
    b.box(x1 - 30, x1, y0, y1, 60, h, plaster)
    b.box(x0, x1, y0, y1, h - 20, h, roof, faces="t")
    cornice = [(0, 0), (12, 0), (18, 8), (18, 20), (8, 24), (0, 24)]
    b.sweep(cornice, [(x0, y0, h - 40), (x1, y0, h - 40), (x1, y1, h - 40), (x0, y1, h - 40)], trim, closed=True)
    b.box(x0, x1, y0, y0 + 20, h, h + 90, plaster)  # parapet
    bld = lib.save_mesh(b, OUT + "/SM_TestBuilding", collision="complex")
    lib.place(bld, folder="Buildings", label="TestBuilding")
    lib.log("building %d triangles" % b.triangles)

    # Glass in the windows: one quad each, instanced.
    pipes = lib.MeshBuilder(ground=ground)
    pipes.tube([(-500, -900, 250), (0, -900, 250), (200, -700, 250), (200, -200, 250)], 18, lib.surface("Metal_Rusty"), segments=12)
    for x in (-400, -100):
        pipes.box(x - 8, x + 8, -908, -892, 0, 250, lib.surface("Metal_Plain"))
    lib.place(lib.save_mesh(pipes, OUT + "/SM_TestPipe", collision="simple"), folder="Props", label="Pipe")

    detail = lib.Cluster("KitTestDetail")
    r = lib.rng(3)
    kit.grass_patch(detail, r, 800, 700, 420, 55, ground, keep=lambda x, y: layers(x, y)[1] > 0.35)
    for i, (x, y) in enumerate([(700, 500), (1000, 900), (500, 950)]):
        detail.add(kit.tree(i), (x, y, ground(x, y) - 10), r.uniform(0, 360), collision=True, cull=12000)
    for _ in range(12):
        x, y = r.uniform(400, 1200), r.uniform(300, 1100)
        detail.add(lib.prop("shrub_02", r.choice(["shrub_02_a_LOD0", "shrub_02_b_LOD0", "shrub_02_d_LOD0"])), (x, y, ground(x, y) - 5),
                   r.uniform(0, 360), r.uniform(0.6, 1.0), cull=6000)
    red = kit.tinted("Metal_Container_Grey", "Metal_Container_RedT", (1.25, 0.32, 0.22))
    blue = kit.tinted("Metal_Container_Grey", "Metal_Container_BlueT", (0.35, 0.55, 1.0))
    detail.add(kit.container(606), (1300, -1300, 0), 0, collision=True, materials=[red, red])
    detail.add(kit.container(606), (1300, -1300, 259), 8, collision=True, materials=[blue, blue])
    detail.add(kit.container(1219), (1300, -1650, 0), 0, collision=True)
    detail.add(kit.trailer(), (-100, -1500, 0), 90, collision=True)
    for i in range(4):
        detail.add(kit.pallet(), (600, -1200, i * 14.4), r.uniform(-4, 4), collision=True)
    for x in (400, 520, 640):
        detail.add(kit.bollard(), (x, -700, 0), 0, collision=True)
    for x in (-1300, -1100):
        detail.add(kit.dock_bumper(), (x, -405, 20), 0, collision=True)
    # Windows and a door on the test building's south face (y0 = -400, outer face at -415).
    for wx, wz in ((x0 + 180, 90), (x0 + 460, 90), (x0 + 180, 400), (x0 + 460, 400)):
        detail.add(kit.window(120, 140), (wx, -415, wz), 0)
        detail.add(kit.shutters(120, 140), (wx, -415, wz), 0)
    detail.add(kit.door(100, 215), (x0 + 650, -415, 60), 0)
    detail.add(kit.lamp_pole(800), (200, 1200, 0), 0, collision=True)
    detail.add(lib.prop("security_light"), (320, 1200, 780), 0)
    detail.add(kit.sign_panel(), (-1000, -416, 600), 0, scl=(2.0, 1.0, 2.0), materials=[kit.sign_material(kit.SIGN["caffe"]), None])
    detail.add(lib.prop("covered_car"), (300, -300, 0), 12, collision=True)
    detail.add(lib.prop("exterior_aircon_unit"), (-700, -432, 420), 0, collision=True)
    steps = lib.MeshBuilder(ground=ground)
    top = kit.stair_flight(steps, -300, 600, 0, 280, 120, 0, lib.surface("Metal_Grate"), lib.surface("Metal_Rusty"), solid=False)
    steps.box(top[0], top[0] + 200, 540, 660, 270, 280, lib.surface("Metal_Grate"))
    kit.ladder(steps, 20, 700, 0, 280, 0, lib.surface("Metal_Rusty"))
    lib.place(lib.save_mesh(steps, OUT + "/SM_TestStairs", collision="complex"), folder="Props", label="Stairs")
    kit.railing_run(detail, (-300, 660, 0), (top[0], 660, 280))
    kit.railing_run(detail, (top[0], 660, 280), (top[0] + 200, 660, 280))
    detail.build()
    lib.log("detail instances: %d" % detail.count())

    lib.decal(lib.OIL, (400, -300, 50), (100, 250, 250), yaw=20)
    lib.decal(lib.CRACK, (0, -500, 50), (100, 400, 400), yaw=70)
    lib.decal(lib.TYRES, (-200, 400, 50), (100, 500, 500))
    lib.decal(lib.LINE, (-200, -100, 50), (100, 150, 800), yaw=0)
    lib.decal(lib.GRIME, (-1000, -420, 100), (40, 400, 200), pitch=0, yaw=90)

    # Material board: one 150 cm panel per variant, in this order from -X to +X.
    board = lib.MeshBuilder(ground=ground)
    names = ["Asphalt_Old", "Stone_Blocks", "Plaster_Ochre", "Concrete_A", "Ground_Grass", "Metal_Container_Red", "Brick_Mixed", "Roof_Terracotta"]
    for i, n in enumerate(names):
        x = -1800 + i * 170
        board.box(x, x + 150, 1700, 1720, 0, 150, lib.surface(n))
    lib.place(lib.save_mesh(board, OUT + "/SM_Board", collision="none"), folder="Board", label="Board")
    lib.log("board order: " + ", ".join(names))

    lib.environment(sun_pitch=-32, sun_yaw=60, sun_lux=9.0, sun_color=(1.0, 0.86, 0.66), fog_density=0.02, exposure=(0.8, 2.0, 0.2))
    lib.start("", -200, 800, -90)
    lib.navmesh(-2000, 2000, -2000, 2000, -300, 800)
    lib.write_views("kittest", [
        ("facade", (-1000, -1500, 170), -2, 90), ("corner", (-300, -1100, 170), -5, 130), ("hill", (-200, 200, 170), 0, 40),
        ("ground", (200, -600, 160), -25, 180), ("top", (-2500, -2500, 1800), -35, 45), ("board", (-1200, 1100, 150), 0, 90), ("kit", (500, -2300, 400), -12, 70), ("trees", (300, 200, 170), 5, 40), ("stairs", (-700, 300, 200), -5, 30)])
    lib.save_level()


try:
    build()
    lib.log("done")
except Exception as e:
    import traceback
    lib.log("FAILED: %s\n%s" % (e, traceback.format_exc()))
lib.flush_log("kit_test.txt")
