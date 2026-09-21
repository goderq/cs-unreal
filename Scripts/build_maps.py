"""
v1.1 maps: Lvl_Depot and Lvl_OldTown, built from Poly Haven surfaces and
props (CC0, imported by Scripts/import_polyhaven.py).

    UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script=Scripts/build_maps.py

Both maps are regenerated from scratch every run (the level assets are
replaced), so this file IS the level design - review it like code.

Conventions the game relies on:
  * PlayerStarts tagged "Alpha" / "Bravo" are team spawns (team modes);
    untagged ones are extra free-for-all spawns. Deathmatch uses all of them.
  * CSPickupSpawnPoint markers place ammo, armor and medkits (weapons are
    bought in v1.1, never lying around).
  * One NavMeshBoundsVolume over the playable area; the navmesh is generated
    at runtime (RuntimeGeneration=Dynamic in DefaultEngine.ini).
  * Lights are movable (Lumen), nothing to bake.
Units: centimetres, Z up, floor at Z = 0.
"""

import math
import os
import unreal

EAL = unreal.EditorAssetLibrary
LEVELS = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
LOG_PATH = os.path.join(PROJECT_DIR, "Saved", "Logs", "build_maps.txt")

SURF = "/Game/Environment/Surfaces/MI_"
PROPS = "/Game/Environment/Props/"
TINTS_DIR = "/Game/Environment/Surfaces/Tinted"
GAME_MODE = "/Game/Characters/BP_CSGameMode"

_log = []


def log(msg):
    line = "[CS-Maps] " + str(msg)
    _log.append(line)
    unreal.log(line)


def flush_log():
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
    with open(LOG_PATH, "w") as f:
        f.write("\n".join(_log))


_cache = {}


def asset(path):
    if path not in _cache:
        _cache[path] = EAL.load_asset(path)
        if _cache[path] is None:
            log("MISSING asset " + path)
    return _cache[path]


def surface(name):
    return asset(SURF + name)


def tinted(base, name, rgb):
    """Child of a surface instance with a colour tint (containers, awnings)."""
    path = TINTS_DIR + "/MI_" + name
    if EAL.does_asset_exist(path):
        mi = EAL.load_asset(path)
    else:
        if not EAL.does_directory_exist(TINTS_DIR):
            EAL.make_directory(TINTS_DIR)
        mi = ASSET_TOOLS.create_asset("MI_" + name, TINTS_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, surface(base))
    MEL.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    EAL.save_loaded_asset(mi)
    return mi


class Builder:
    """Places geometry into the currently open level."""

    def __init__(self, name):
        self.name = name
        self.cube = unreal.load_object(None, "/Engine/BasicShapes/Cube.Cube")
        self.cylinder_mesh = unreal.load_object(None, "/Engine/BasicShapes/Cylinder.Cylinder")
        self.count = 0
        self.starts = {"Alpha": 0, "Bravo": 0, "": 0}

    def _mesh_actor(self, mesh, loc, rot, scale, material, folder, label):
        actor = ACTORS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*loc), rot)
        comp = actor.static_mesh_component
        comp.set_static_mesh(mesh)
        actor.set_actor_scale3d(unreal.Vector(*scale))
        if material:
            for i in range(comp.get_num_materials()):
                comp.set_material(i, material)
        actor.set_folder_path(folder)
        actor.set_actor_label(label)
        self.count += 1
        return actor

    def box(self, x0, x1, y0, y1, z0, z1, material, folder="Geometry", label="Box", yaw=0.0):
        """Axis-aligned block by its extents (before the optional yaw about its centre)."""
        cx, cy, cz = (x0 + x1) / 2.0, (y0 + y1) / 2.0, (z0 + z1) / 2.0
        size = (abs(x1 - x0) / 100.0, abs(y1 - y0) / 100.0, abs(z1 - z0) / 100.0)
        return self._mesh_actor(self.cube, (cx, cy, cz), unreal.Rotator(0, 0, yaw), size, material, folder, label)

    def slab(self, center, size, rot, material, folder="Geometry", label="Slab"):
        """Block by centre, size and full rotation (ramps, roofs)."""
        return self._mesh_actor(self.cube, center, rot, (size[0] / 100.0, size[1] / 100.0, size[2] / 100.0), material, folder, label)

    def cylinder(self, x, y, z0, radius, height, material, folder="Geometry", label="Cylinder"):
        return self._mesh_actor(self.cylinder_mesh, (x, y, z0 + height / 2.0), unreal.Rotator(0, 0, 0),
                                (radius / 50.0, radius / 50.0, height / 100.0), material, folder, label)

    def ramp(self, x0, x1, y0, y1, z_low, z_high, material, along="x", folder="Geometry"):
        """Walkable ramp rising along +X (along='x') or +Y from z_low to z_high."""
        run = (x1 - x0) if along == "x" else (y1 - y0)
        rise = z_high - z_low
        length = math.hypot(run, rise)
        angle = math.degrees(math.atan2(rise, run))
        cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
        cz = (z_low + z_high) / 2.0 - 10.0
        if along == "x":
            return self.slab((cx, cy, cz), (length, abs(y1 - y0), 20), unreal.Rotator(roll=0, pitch=angle, yaw=0), material, folder, "Ramp")
        return self.slab((cx, cy, cz), (abs(x1 - x0), length, 20), unreal.Rotator(roll=-angle, pitch=0, yaw=0), material, folder, "Ramp")

    def wall(self, a, b, z0, z1, thickness, material, openings=(), folder="Walls", label="Wall"):
        """Straight wall from a to b (axis-aligned) with openings (start, end, bottom, top) along it."""
        (ax, ay), (bx, by) = a, b
        horizontal = abs(by - ay) < 1.0
        lo, hi = (min(ax, bx), max(ax, bx)) if horizontal else (min(ay, by), max(ay, by))
        fixed = ay if horizontal else ax
        t = thickness / 2.0

        def piece(s, e, zb, zt):
            if e - s < 1.0 or zt - zb < 1.0:
                return
            if horizontal:
                self.box(s, e, fixed - t, fixed + t, zb, zt, material, folder, label)
            else:
                self.box(fixed - t, fixed + t, s, e, zb, zt, material, folder, label)

        cursor = lo
        for (s, e, zb, zt) in sorted(openings):
            piece(cursor, s, z0, z1)       # solid run before the opening
            piece(s, e, z0, zb)            # below it (windows)
            piece(s, e, zt, z1)            # above it (lintel)
            cursor = e
        piece(cursor, hi, z0, z1)

    def prop(self, name, x, y, z=0.0, yaw=0.0, scale=1.0, folder="Props"):
        mesh = asset(PROPS + name + "/SM_" + name)
        if not mesh:
            return None
        box = mesh.get_bounding_box()
        # Sit it on the surface whatever its pivot.
        base = -box.min.z * scale
        return self._mesh_actor(mesh, (x, y, z + base), unreal.Rotator(0, 0, yaw), (scale, scale, scale), None, folder, name)

    def start(self, team, x, y, yaw, z=0.0):
        actor = ACTORS.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(x, y, z + 100.0), unreal.Rotator(0, 0, yaw))
        if team:
            actor.set_editor_property("player_start_tag", unreal.Name(team))
        key = team
        actor.set_actor_label("Start_%s_%02d" % (team or "FFA", self.starts[key]))
        actor.set_folder_path("Spawns/" + (team or "FFA"))
        self.starts[key] += 1
        return actor

    def loot(self, item, x, y, z=0.0):
        marker = ACTORS.spawn_actor_from_class(unreal.CSPickupSpawnPoint, unreal.Vector(x, y, z + 30.0), unreal.Rotator(0, 0, 0))
        marker.set_editor_property("item_id", item)
        marker.set_actor_label("Loot_" + item)
        marker.set_folder_path("Loot")

    def light(self, x, y, z, intensity=5000.0, radius=1600.0, color=(1.0, 0.85, 0.65), folder="Lights"):
        actor = ACTORS.spawn_actor_from_class(unreal.PointLight, unreal.Vector(x, y, z), unreal.Rotator(0, 0, 0))
        comp = actor.get_editor_property("point_light_component")
        comp.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        comp.set_editor_property("intensity", intensity)
        comp.set_editor_property("attenuation_radius", radius)
        comp.set_editor_property("light_color", unreal.Color(int(color[2] * 255), int(color[1] * 255), int(color[0] * 255), 255))
        actor.set_folder_path(folder)
        return actor

    def environment(self, sun_pitch, sun_yaw, sun_intensity, fog_density, sun_color=(1.0, 0.95, 0.85)):
        sun = ACTORS.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 3000), unreal.Rotator(0, sun_pitch, sun_yaw))
        comp = sun.get_editor_property("directional_light_component")
        comp.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        comp.set_editor_property("intensity", sun_intensity)
        comp.set_editor_property("atmosphere_sun_light", True)
        comp.set_editor_property("light_color", unreal.Color(int(sun_color[2] * 255), int(sun_color[1] * 255), int(sun_color[0] * 255), 255))
        sun.set_folder_path("Environment")

        sky = ACTORS.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 2800), unreal.Rotator(0, 0, 0))
        sky_comp = sky.get_editor_property("light_component")
        sky_comp.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        sky_comp.set_editor_property("real_time_capture", True)
        sky.set_folder_path("Environment")

        atmosphere = ACTORS.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
        atmosphere.set_folder_path("Environment")

        fog = ACTORS.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, -200), unreal.Rotator(0, 0, 0))
        fog_comp = fog.get_editor_property("component")
        fog_comp.set_editor_property("fog_density", fog_density)
        fog.set_folder_path("Environment")

        try:
            clouds = ACTORS.spawn_actor_from_class(unreal.VolumetricCloud, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
            clouds.set_folder_path("Environment")
        except Exception as e:
            log("  no volumetric clouds: %s" % e)

        post = ACTORS.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
        post.set_editor_property("unbound", True)
        settings = post.get_editor_property("settings")
        settings.set_editor_property("override_auto_exposure_min_brightness", True)
        settings.set_editor_property("auto_exposure_min_brightness", 0.6)
        settings.set_editor_property("override_auto_exposure_max_brightness", True)
        settings.set_editor_property("auto_exposure_max_brightness", 2.0)
        settings.set_editor_property("override_bloom_intensity", True)
        settings.set_editor_property("bloom_intensity", 0.4)
        settings.set_editor_property("override_vignette_intensity", True)
        settings.set_editor_property("vignette_intensity", 0.3)
        post.set_editor_property("settings", settings)
        post.set_folder_path("Environment")

    def navmesh(self, x0, x1, y0, y1, z0, z1):
        vol = ACTORS.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), unreal.Rotator(0, 0, 0))
        # The default brush is a 200 cm cube.
        vol.set_actor_scale3d(unreal.Vector((x1 - x0) / 200.0, (y1 - y0) / 200.0, (z1 - z0) / 200.0))
        vol.set_folder_path("Environment")


def new_level(path):
    if EAL.does_asset_exist(path):
        # Reuse the asset (it cannot be deleted while registered) and clear it.
        LEVELS.load_level(path)
        for actor in ACTORS.get_all_level_actors():
            if not isinstance(actor, (unreal.WorldSettings, unreal.Brush)) or isinstance(actor, unreal.Volume):
                ACTORS.destroy_actor(actor)
    else:
        LEVELS.new_level(path)
    world = unreal.EditorLevelLibrary.get_editor_world()
    gm = asset(GAME_MODE)
    if world and gm:
        world.get_world_settings().set_editor_property("default_game_mode", gm.generated_class())


# ---------------------------------------------------------------------------
# DEPOT - industrial yard, 96 x 72 m
# ---------------------------------------------------------------------------

def build_depot():
    new_level("/Game/Maps/Lvl_Depot")
    b = Builder("Depot")
    X, Y = 4800, 3600
    asphalt = surface("asphalt_02")
    concrete_floor = surface("concrete_floor_worn_001")
    concrete_wall = surface("concrete_wall_003")
    iron = surface("corrugated_iron")
    plate = surface("metal_plate")
    painted = surface("painted_concrete")
    rust = surface("rusty_painted_metal")
    container_colors = [
        tinted("rusty_painted_metal", "Container_Red", (0.75, 0.22, 0.16)),
        tinted("rusty_painted_metal", "Container_Blue", (0.22, 0.38, 0.7)),
        tinted("rusty_painted_metal", "Container_Green", (0.3, 0.52, 0.3)),
        tinted("rusty_painted_metal", "Container_Orange", (0.9, 0.5, 0.18)),
        tinted("rusty_painted_metal", "Container_Grey", (0.55, 0.57, 0.6)),
    ]
    yellow_paint = tinted("painted_concrete", "Paint_Yellow", (1.0, 0.8, 0.25))

    # Ground and perimeter.
    b.box(-X, X, -Y, Y, -60, 0, asphalt, "Ground", "Asphalt")
    for (a, c) in (((-X, Y), (X, Y)), ((-X, -Y), (X, -Y)), ((-X, -Y), (-X, Y)), ((X, -Y), (X, Y))):
        b.wall(a, c, 0, 600, 60, concrete_wall, folder="Perimeter")

    # ---- Central hangar: 26 x 20 m, 9 m high, big doors west/east ----
    hx, hy, hh = 1300, 1000, 900
    b.box(-hx, hx, -hy, hy, 0, 4, concrete_floor, "Hangar", "HangarFloor")
    b.wall((-hx, -hy), (-hx, hy), 0, hh, 30, iron, [(-420, 420, 0, 520)], "Hangar")          # west doors
    b.wall((hx, -hy), (hx, hy), 0, hh, 30, iron, [(-420, 420, 0, 520)], "Hangar")            # east doors
    b.wall((-hx, hy), (hx, hy), 0, hh, 30, iron, [(-200, 200, 0, 300)], "Hangar")             # north side door
    b.wall((-hx, -hy), (hx, -hy), 0, hh, 30, iron, [(500, 900, 0, 300), (-900, -500, 0, 300)], "Hangar")
    b.box(-hx - 40, hx + 40, -hy - 40, hy + 40, hh, hh + 25, plate, "Hangar", "HangarRoof")
    # Catwalk along the north wall, 3 m up, reached by a ramp from the west.
    b.box(-900, 1100, 620, 985, 300, 320, plate, "Hangar", "Catwalk")
    for x in (-800, -200, 400, 1000):
        b.box(x - 12, x + 12, 620, 644, 0, 300, rust, "Hangar", "CatwalkPost")
    b.wall((-900, 620), (1100, 620), 320, 420, 8, rust, [(-100, 100, 320, 420)], "Hangar", "Railing")
    b.ramp(-1250, -900, 620, 985, 4, 320, plate, "x", "Hangar")
    # Cover inside the hangar.
    for (x, y, yaw) in ((-500, -300, 0), (0, 200, 90), (500, -400, 0), (-300, 500, 15), (800, 300, 90)):
        b.prop("old_military_crate", x, y, 4, yaw)
    for (x, y) in ((-700, 0), (650, 0), (0, -650)):
        b.box(x - 110, x + 110, y - 110, y + 110, 4, 170, plate, "Hangar", "PalletStack")
        b.prop("wooden_crate_02", x, y, 170, 30)
    for (x, y) in ((-1150, -850), (-1080, -800), (1150, 850), (1100, -850), (1040, -800)):
        b.prop("Barrel_01", x, y, 4)
    b.light(-600, 0, 850, 5000, 2200)
    b.light(600, 0, 850, 5000, 2200)
    b.light(0, 800, 700, 5000, 1400)

    # ---- Container yard, north: lanes between rows, some stacked ----
    rows = [1650, 2350, 3050]
    for ri, y in enumerate(rows):
        x = -3000
        k = 0
        while x < 3000:
            if abs(x + 1800) < 350 or abs(x) < 350 or abs(x - 1800) < 350:
                x += 700  # north-south lanes
                continue
            mat = container_colors[(ri * 3 + k) % len(container_colors)]
            b.box(x, x + 610, y - 122, y + 122, 0, 260, mat, "Containers", "Container")
            if (ri + k) % 3 == 0:
                b.box(x + 10, x + 600, y - 118, y + 118, 260, 520, container_colors[(ri + k + 2) % len(container_colors)], "Containers", "Container")
            k += 1
            x += 650
    # A container turned across a lane as a crossing cover point.
    b.box(-250, 250, 2700, 2700 + 610, 0, 260, container_colors[3], "Containers", "Container")
    for (x, y) in ((-2500, 2000), (2600, 2000), (-1200, 2700), (1300, 2700)):
        b.prop("concrete_road_barrier", x, y, 0, 90)

    # ---- Office building, south-west: two rooms, doors and windows ----
    ox0, ox1, oy0, oy1, oh = -2700, -700, -3300, -1900, 380
    b.box(ox0, ox1, oy0, oy1, 0, 6, concrete_floor, "Office", "OfficeFloor")
    win = 120, 240
    b.wall((ox0, oy1), (ox1, oy1), 0, oh, 25, painted, [(-2450, -2250, 0, 230), (-2000, -1700, win[0], win[1]), (-1350, -1050, win[0], win[1])], "Office")
    b.wall((ox0, oy0), (ox1, oy0), 0, oh, 25, painted, [(-2200, -1900, win[0], win[1])], "Office")
    b.wall((ox0, oy0), (ox0, oy1), 0, oh, 25, painted, [(-2800, -2500, win[0], win[1])], "Office")
    b.wall((ox1, oy0), (ox1, oy1), 0, oh, 25, painted, [(-2700, -2450, 0, 230)], "Office")
    b.wall((-1600, oy0), (-1600, oy1), 0, oh, 20, painted, [(-2700, -2450, 0, 230)], "Office")   # dividing wall
    b.box(ox0 - 20, ox1 + 20, oy0 - 20, oy1 + 20, oh, oh + 25, concrete_wall, "Office", "OfficeRoof")
    for (x, y, yaw) in ((-2400, -3050, 0), (-1900, -2200, 90), (-1100, -3000, 0)):
        b.prop("wooden_crate_01", x, y, 6, yaw)
    b.prop("utility_box_02", -800, -2100, 6, -90)
    b.prop("cardboard_box_01", -2550, -2150, 6, 20)
    b.prop("cardboard_box_01", -2500, -2120, 40, 70)
    b.light(-2150, -2600, 350, 2500, 900)
    b.light(-1150, -2600, 350, 2500, 900)

    # ---- Loading dock, south-east: raised platform, ramp, parked truck ----
    b.box(900, 3200, -3300, -2500, 0, 120, painted, "Dock", "DockPlatform")
    b.box(900, 3200, -2520, -2500, 0, 120, yellow_paint, "Dock", "DockEdge")
    b.ramp(3200, 3700, -3300, -2900, 0, 120, painted, "x", "Dock")
    for (x, y) in ((1300, -2900), (2000, -3050), (2700, -2850)):
        b.prop("wooden_crate_02", x, y, 120, 20)
    # Truck: trailer, cab and wheels.
    b.box(1200, 2600, -2150, -1900, 110, 420, rust, "Dock", "Trailer")
    b.box(2600, 3050, -2150, -1900, 60, 330, container_colors[1], "Dock", "Cab")
    for x in (1350, 1650, 2400, 2850):
        for y in (-2170, -1880):
            b.box(x - 55, x + 55, y - 18, y + 18, 0, 110, rust, "Dock", "Wheel")

    # ---- Roads, barriers and scattered cover in the open ----
    for (x, y, yaw) in ((-3000, 600, 0), (-3000, -600, 0), (3000, 600, 0), (3000, -600, 0),
                        (-2000, 1100, 90), (2000, 1100, 90), (-2000, -1300, 90), (2000, -1300, 90),
                        (0, -1600, 0), (0, 1300, 0)):
        b.prop("concrete_road_barrier", x, y, 0, yaw)
    for (x, y) in ((-1800, -300), (-1750, -250), (1800, 300), (1850, 250), (-3600, 2900), (3600, -2900),
                   (-600, -1500), (700, 1400)):
        b.prop("Barrel_01", x, y)
    for (x, y, yaw) in ((-2400, 300, 0), (2400, -300, 180), (-1500, -2400, 0), (1500, 2400, 0)):
        b.prop("utility_box_02", x, y, 0, yaw)
    for (x, y, yaw) in ((-2300, -800, 30), (2300, 800, 210), (-600, 2000, 0), (600, -2000, 0)):
        b.prop("wooden_crate_02", x, y, 0, yaw)

    # ---- Bases: west (Alpha) and east (Bravo), each with a shelter ----
    for side, team in ((-1, "Alpha"), (1, "Bravo")):
        sx = side * 4150
        b.box(sx - 500, sx + 500, -700, 700, 0, 4, concrete_floor, "Base" + team, "BaseFloor")
        b.box(sx - 520, sx + 520, -720, 720, 420, 440, plate, "Base" + team, "ShelterRoof")
        for px in (sx - 480, sx + 480):
            for py in (-680, 680):
                b.box(px - 15, px + 15, py - 15, py + 15, 0, 420, rust, "Base" + team, "ShelterPost")
        b.prop("concrete_road_barrier", sx - side * 800, -400, 0, 90)
        b.prop("concrete_road_barrier", sx - side * 800, 400, 0, 90)
        yaw = 0 if side < 0 else 180
        for i in range(10):
            b.start(team, sx + (i // 5 - 0.5) * 220 * -side, -600 + (i % 5) * 300, yaw)

    # ---- Free-for-all spawns all over the map ----
    for (x, y, yaw) in ((-2600, 3300, -45), (2600, 3300, -135), (0, 3350, -90), (-1800, 2000, -90),
                        (1800, 2000, -90), (-1000, 800, 0), (1000, -800, 180), (-2200, -2600, 45),
                        (-1100, -2400, 90), (2000, -2800, 90), (3300, 1500, 180), (-3300, -1500, 0),
                        (-3600, 3200, -45), (3600, -3200, 135), (0, -2900, 90), (-500, 200, 0)):
        b.start("", x, y, yaw)

    # ---- Pickups (ammo, armor, medkits) ----
    for (item, x, y, z) in (("ammo_rifle", 0, 0, 4), ("ammo_smg", -1000, 0, 4), ("ammo_shells", 1000, 0, 4),
                            ("medkit", 0, 800, 320), ("armor", 200, 800, 320), ("ammo_sniper", -700, 800, 320),
                            ("medkit", -2150, -2600, 6), ("armor", -1150, -2600, 6), ("ammo_rifle", 2000, -2900, 120),
                            ("medkit", 0, 2350, 0), ("ammo_smg", -1800, 2700, 0), ("ammo_shells", 1800, 2700, 0),
                            ("ammo_rifle", -3000, 0, 0), ("ammo_rifle", 3000, 0, 0)):
        b.loot(item, x, y, z)

    b.environment(sun_pitch=-38, sun_yaw=35, sun_intensity=9.0, fog_density=0.012, sun_color=(1.0, 0.93, 0.82))
    b.navmesh(-X, X, -Y, Y, -200, 900)
    LEVELS.save_current_level()
    log("Lvl_Depot: %d meshes, starts %s" % (b.count, b.starts))


# ---------------------------------------------------------------------------
# OLD TOWN - streets and a market square, 90 x 80 m
# ---------------------------------------------------------------------------

WEDGE_PATH = "/Game/Environment/Meshes/SM_Wedge"


def ensure_wedge():
    """Triangular prism: triangle (-50,0) (50,0) (0,100) in local XY, extruded 100 along local Z (centred)."""
    if EAL.does_asset_exist(WEDGE_PATH):
        return EAL.load_asset(WEDGE_PATH)
    mesh = unreal.DynamicMesh()
    options = unreal.GeometryScriptPrimitiveOptions()
    tri = [unreal.Vector2D(-50, 0), unreal.Vector2D(50, 0), unreal.Vector2D(0, 100)]
    unreal.GeometryScript_Primitives.append_simple_extrude_polygon(
        mesh, options, unreal.Transform(unreal.Vector(0, 0, -50), unreal.Rotator(0, 0, 0), unreal.Vector(1, 1, 1)),
        tri, 100.0, 0, True, unreal.GeometryScriptPrimitiveOriginMode.BASE)
    folder = "/Game/Environment/Meshes"
    if not EAL.does_directory_exist(folder):
        EAL.make_directory(folder)
    opts = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    opts.set_editor_property("enable_recompute_normals", True)
    opts.set_editor_property("enable_collision", True)
    result = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(mesh, WEDGE_PATH, opts)
    sm = result[0] if isinstance(result, (tuple, list)) else result
    EAL.save_loaded_asset(sm)
    log("created SM_Wedge")
    return sm


def gable_prism(b, cx, cy, h, span, rise, length, along_x, material, folder):
    """Fills the space under a pitched roof: base on the wall tops, apex under the ridge."""
    wedge = ensure_wedge()
    if along_x:
        rot = unreal.MathLibrary.make_rot_from_xz(unreal.Vector(0, 1, 0), unreal.Vector(1, 0, 0))
    else:
        rot = unreal.MathLibrary.make_rot_from_xz(unreal.Vector(-1, 0, 0), unreal.Vector(0, 1, 0))
    return b._mesh_actor(wedge, (cx, cy, h), rot, (span / 100.0, rise / 100.0, length / 100.0), material, folder, "Gable")


def house(b, x0, x1, y0, y1, h, wall_mat, roof_mat, doors=(), enterable=False, roof="pitched", folder="Houses", trim=None):
    """A house block. doors: list of (side, centre, width) with side in N/S/E/W."""
    if not enterable:
        b.box(x0, x1, y0, y1, 0, h, wall_mat, folder, "House")
    else:
        t = 25
        openings = {"N": [], "S": [], "E": [], "W": []}
        for side, centre, width in doors:
            openings[side].append((centre - width / 2.0, centre + width / 2.0, 0, 240))
        # Windows on every long side, above head height of the doors.
        for side in "NS":
            span = x1 - x0
            for k in range(1, int(span // 400)):
                cx = x0 + k * span / int(span // 400)
                if all(abs(cx - (s + e) / 2.0) > 180 for s, e, _, _ in openings[side]):
                    openings[side].append((cx - 60, cx + 60, 130, 230))
        b.box(x0, x1, y0, y1, 0, 6, surface("weathered_planks"), folder, "HouseFloor")
        b.wall((x0, y1), (x1, y1), 0, h, t, wall_mat, openings["N"], folder)
        b.wall((x0, y0), (x1, y0), 0, h, t, wall_mat, openings["S"], folder)
        b.wall((x0, y0), (x0, y1), 0, h, t, wall_mat, openings["W"], folder)
        b.wall((x1, y0), (x1, y1), 0, h, t, wall_mat, openings["E"], folder)
        b.box(x0, x1, y0, y1, h, h + 20, surface("weathered_planks"), folder, "Ceiling")
        b.light((x0 + x1) / 2.0, (y0 + y1) / 2.0, h - 40, 1800, 700, (1.0, 0.78, 0.5), folder)
    if trim:
        b.box(x0 - 8, x1 + 8, y0 - 8, y1 + 8, 0, 90, trim, folder, "Plinth")
    # Roof.
    if roof == "flat":
        b.box(x0 - 20, x1 + 20, y0 - 20, y1 + 20, h, h + 30, roof_mat, folder, "Roof")
        return
    along_x = (x1 - x0) >= (y1 - y0)
    span = (y1 - y0) if along_x else (x1 - x0)
    length = (x1 - x0) if along_x else (y1 - y0)
    pitch = 32.0
    half = span / 2.0 + 30
    slope = half / math.cos(math.radians(pitch))
    rise = math.tan(math.radians(pitch)) * (span / 2.0)
    cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
    for s in (-1, 1):
        if along_x:
            center = (cx, cy + s * span / 4.0, h + rise / 2.0 + 8)
            b.slab(center, (length + 60, slope, 16), unreal.Rotator(roll=s * pitch, pitch=0, yaw=0), roof_mat, folder, "RoofSide")
        else:
            center = (cx + s * span / 4.0, cy, h + rise / 2.0 + 8)
            b.slab(center, (slope, length + 60, 16), unreal.Rotator(roll=0, pitch=-s * pitch, yaw=0), roof_mat, folder, "RoofSide")
    # Gable ends so the roof is closed: a prism in the wall colour under the tiles.
    gable_prism(b, cx, cy, h, span, rise, length, along_x, wall_mat, folder)


def build_oldtown():
    new_level("/Game/Maps/Lvl_OldTown")
    b = Builder("OldTown")
    X, Y = 4500, 4000
    cobble = surface("cobblestone_floor_08")
    tiles = surface("stone_tiles_02")
    roof = surface("clay_roof_tiles_02")
    brick = surface("red_brick_03")
    castle = surface("castle_brick_02_red")
    plaster = surface("plastered_wall_02")
    white = surface("white_plaster_02")
    yellow = surface("yellow_plaster")
    planks = surface("weathered_planks")
    concrete = surface("concrete_wall_003")
    awnings = [tinted("weathered_planks", "Awning_Red", (0.8, 0.25, 0.2)),
               tinted("weathered_planks", "Awning_Blue", (0.3, 0.45, 0.8)),
               tinted("weathered_planks", "Awning_Green", (0.35, 0.6, 0.35)),
               tinted("weathered_planks", "Awning_Yellow", (0.95, 0.8, 0.35))]
    pastel = [white, yellow, plaster, tinted("white_plaster_02", "Plaster_Rose", (0.95, 0.75, 0.7)),
              tinted("white_plaster_02", "Plaster_Mint", (0.75, 0.9, 0.8)), tinted("white_plaster_02", "Plaster_Sky", (0.75, 0.82, 0.95))]

    b.box(-X, X, -Y, Y, -60, 0, cobble, "Ground", "Cobblestone")
    # Town wall around everything.
    for (a, c) in (((-X, Y), (X, Y)), ((-X, -Y), (X, -Y)), ((-X, -Y), (-X, Y)), ((X, -Y), (X, Y))):
        b.wall(a, c, 0, 700, 80, castle, folder="TownWall")

    # Market square in the centre: x -1300..1300, y -1100..1100, raised stone tiles.
    b.box(-1300, 1300, -1100, 1100, 0, 8, tiles, "Square", "SquareTiles")
    # Fountain.
    b.cylinder(0, 0, 8, 330, 70, tiles, "Square", "FountainBasin")
    b.cylinder(0, 0, 70, 290, 12, surface("metal_plate"), "Square", "FountainWater")
    b.cylinder(0, 0, 8, 70, 260, castle, "Square", "FountainColumn")
    b.cylinder(0, 0, 260, 130, 30, tiles, "Square", "FountainBowl")
    # Market stalls around the fountain.
    stalls = [(-850, -650, 0), (850, -650, 0), (-850, 650, 0), (850, 650, 0), (0, -850, 90), (0, 850, 90)]
    for i, (sx, sy, yaw) in enumerate(stalls):
        w, d = (360, 180) if yaw == 0 else (180, 360)
        b.box(sx - w / 2, sx + w / 2, sy - d / 2, sy + d / 2, 8, 100, planks, "Square", "StallTable")
        for px in (sx - w / 2 + 10, sx + w / 2 - 10):
            for py in (sy - d / 2 + 10, sy + d / 2 - 10):
                b.box(px - 6, px + 6, py - 6, py + 6, 100, 260, planks, "Square", "StallPost")
        b.box(sx - w / 2 - 30, sx + w / 2 + 30, sy - d / 2 - 30, sy + d / 2 + 30, 260, 272, awnings[i % len(awnings)], "Square", "Awning")
        b.prop("wooden_crate_01", sx + (40 if yaw == 0 else 0), sy + (0 if yaw == 0 else 40), 100, 10 * i)
    for (x, y) in ((-1150, -950), (1150, 950), (-1150, 950), (1150, -950)):
        b.prop("planter_box_01", x, y, 8, 45)
    for (x, y, yaw) in ((-500, -1000, 0), (500, 1000, 180), (-1200, 0, 90), (1200, 0, -90)):
        b.prop("painted_wooden_bench", x, y, 8, yaw)
    for (x, y) in ((-400, 600), (420, -620), (-1000, 300), (1000, -300)):
        b.prop("wine_barrel_01", x, y, 8)

    # Streets: main N-S (x -500..500) and E-W (y -450..450) through the square;
    # houses fill the four quarters with alleys between them.
    ci = 0

    def pick():
        nonlocal ci
        ci += 1
        return pastel[ci % len(pastel)]

    # North-west quarter.
    house(b, -4400, -3300, 1600, 2800, 1000, castle, roof, roof="flat", folder="Houses/NW", trim=concrete)   # tower base
    b.box(-4150, -3550, 1850, 2550, 1000, 2100, castle, "Houses/NW", "BellTower")
    house(b, -4250, -3450, 1950, 2450, 2100, castle, roof, folder="Houses/NW")
    house(b, -3100, -1900, 1400, 2300, 700, pick(), roof, [("S", -2500, 180), ("E", 1850, 160)], True, folder="Houses/NW")
    house(b, -1700, -700, 1500, 2400, 900, pick(), roof, folder="Houses/NW", trim=brick)
    house(b, -3100, -1500, 2700, 3700, 800, brick, roof, folder="Houses/NW")
    house(b, -1300, -700, 2700, 3700, 1100, pick(), roof, folder="Houses/NW")
    house(b, -4300, -3500, 500, 1300, 600, pick(), roof, [("E", 900, 180)], True, folder="Houses/NW")
    house(b, -3200, -1700, 600, 1100, 650, pick(), roof, folder="Houses/NW", trim=brick)
    # North-east quarter.
    house(b, 700, 1800, 1400, 2400, 800, pick(), roof, [("S", 1250, 180), ("W", 1900, 160)], True, folder="Houses/NE")
    house(b, 2100, 3300, 1400, 2200, 1000, brick, roof, folder="Houses/NE")
    house(b, 3500, 4400, 1300, 2600, 700, pick(), roof, [("W", 1900, 200)], True, folder="Houses/NE")
    house(b, 700, 1600, 2700, 3700, 900, pick(), roof, folder="Houses/NE", trim=brick)
    house(b, 1900, 3200, 2600, 3700, 750, pick(), roof, [("S", 2550, 180)], True, folder="Houses/NE")
    house(b, 1700, 3200, 600, 1100, 600, pick(), roof, folder="Houses/NE")
    house(b, 3500, 4400, 400, 1000, 900, castle, roof, folder="Houses/NE")
    # South-west quarter.
    house(b, -3200, -1900, -2400, -1400, 800, pick(), roof, [("N", -2550, 180), ("E", -1900, 160)], True, folder="Houses/SW")
    house(b, -1700, -700, -2400, -1500, 1000, brick, roof, folder="Houses/SW")
    house(b, -4400, -3400, -2600, -1300, 700, pick(), roof, folder="Houses/SW", trim=brick)
    house(b, -3000, -1600, -3700, -2700, 900, pick(), roof, folder="Houses/SW")
    house(b, -1300, -700, -3700, -2700, 700, pick(), roof, [("E", -3200, 180)], True, folder="Houses/SW")
    house(b, -4300, -3400, -1000, -500, 600, pick(), roof, folder="Houses/SW")
    house(b, -3200, -1700, -1100, -600, 650, pick(), roof, folder="Houses/SW", trim=brick)
    # South-east quarter.
    house(b, 700, 1800, -2400, -1400, 900, pick(), roof, [("N", 1250, 180), ("W", -1900, 160)], True, folder="Houses/SE")
    house(b, 2100, 3200, -2300, -1400, 700, pick(), roof, folder="Houses/SE", trim=brick)
    house(b, 3500, 4400, -2600, -1300, 1100, brick, roof, folder="Houses/SE")
    house(b, 700, 1700, -3700, -2700, 800, pick(), roof, folder="Houses/SE")
    house(b, 2000, 3300, -3700, -2700, 700, pick(), roof, [("N", 2650, 200)], True, folder="Houses/SE")
    house(b, 1700, 3200, -1100, -600, 600, pick(), roof, folder="Houses/SE")
    house(b, 3500, 4400, -1000, -400, 850, castle, roof, folder="Houses/SE")

    # Arches over the main street at both ends of the square.
    for y in (-1300, 1300):
        b.wall((-700, y), (700, y), 0, 900, 120, castle, [(-450, 450, 0, 600)], "Arches")

    # Sidewalk strips along the main street.
    for x0, x1 in ((-700, -500), (500, 700)):
        b.box(x0, x1, -Y + 100, -1100, 0, 15, tiles, "Street", "Sidewalk")
        b.box(x0, x1, 1100, Y - 100, 0, 15, tiles, "Street", "Sidewalk")

    # Street clutter as cover.
    for (x, y, yaw) in ((-250, -2200, 0), (250, 2200, 0), (-300, -3200, 20), (300, 3200, -20), (2400, 200, 90), (-2400, -200, 90)):
        b.prop("wooden_crate_02", x, y, 0, yaw)
    for (x, y) in ((300, -1800), (-300, 1800), (-2000, 250), (2000, -250), (3900, 1150), (-3900, -1150)):
        b.prop("wine_barrel_01", x, y)
    for (x, y, yaw) in ((-150, -2700, 90), (150, 2700, 90), (-3000, 250, 0), (3000, -250, 0)):
        b.prop("old_military_crate", x, y, 0, yaw)
    for (x, y, yaw) in ((-600, -1500, 0), (600, 1500, 180), (4100, 3000, 0), (-4100, -3000, 0)):
        b.prop("cardboard_box_01", x, y, 0, yaw)
    for (x, y) in ((-3800, 3800 - 300), (3800, -3500)):
        b.prop("utility_box_02", x, y, 0, 180)

    # ---- Spawns: Alpha south, Bravo north, free-for-all spread out ----
    for i in range(10):
        b.start("Alpha", -450 + (i % 5) * 225, -3650 + (i // 5) * 200, 90)
        b.start("Bravo", -450 + (i % 5) * 225, 3650 - (i // 5) * 200, -90)
    for (x, y, yaw) in ((-2500, -1900, 0), (2500, 1800, 180), (-2500, 1850, 0), (2500, -1900, 180),
                        (-3900, 900, 0), (3900, -700, 180), (-900, 0, 0), (900, 0, 180),
                        (0, -2000, 90), (0, 2000, -90), (-3900, -3600, 45), (3900, 3600, -135),
                        (1250, 1900, -90), (-1250, -1900, 90), (-1000, -3200, 0), (2650, -3200, 90)):
        b.start("", x, y, yaw)

    # ---- Pickups ----
    for (item, x, y) in (("ammo_rifle", 0, -1100), ("ammo_rifle", 0, 1100), ("medkit", -2500, -1900), ("medkit", 2500, 1800),
                         ("armor", -1250, 1900), ("armor", 1250, -1900), ("ammo_smg", -3900, 900), ("ammo_shells", 3900, -700),
                         ("ammo_sniper", -2550, 1850), ("ammo_sniper", 2550, -1900), ("medkit", 0, 0), ("ammo_smg", 0, -3000),
                         ("ammo_shells", 0, 3000)):
        b.loot(item, x, y, 8)

    b.environment(sun_pitch=-28, sun_yaw=-55, sun_intensity=8.0, fog_density=0.018, sun_color=(1.0, 0.86, 0.68))
    b.navmesh(-X, X, -Y, Y, -200, 1200)
    LEVELS.save_current_level()
    log("Lvl_OldTown: %d meshes, starts %s" % (b.count, b.starts))


def main():
    build_depot()
    build_oldtown()
    log("done")
    flush_log()


try:
    main()
except Exception as e:
    import traceback
    log("FAILED: %s\n%s" % (e, traceback.format_exc()))
    flush_log()
