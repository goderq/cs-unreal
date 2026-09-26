"""
The maps: Lvl_Depot and Lvl_OldTown (v1.1) and Lvl_Warehouse, the logistics
complex (v2.0 phase 4, AUDIT K3), built from Poly Haven surfaces and props
(CC0, imported by Scripts/import_polyhaven.py).

    UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script=Scripts/build_maps.py
    set CS_MAPS=warehouse      (optional: only these maps, comma-separated)

Every map is regenerated from scratch each run (the level assets are
replaced), so this file IS the level design - review it like code. The
in-game map audit (-cstestmapaudit, docs/MAPS.md) checks the result:
spawns, routes, sightlines, height, ammo machines, reverb and budgets.

Conventions the game relies on:
  * PlayerStarts tagged "Alpha" / "Bravo" are team spawns (team modes);
    untagged ones are extra free-for-all spawns. Deathmatch uses all of them.
  * Ammo machines (ACSAmmoMachine) are the only ammo on a map (v2.0, C19):
    one at each base and two in the middle, the use side facing +X of the
    actor.
  * AudioVolumes with a reverb effect over every interior (phase 4, C9).
  * One NavMeshBoundsVolume over the playable area; the navmesh is generated
    at runtime (RuntimeGeneration=Dynamic in DefaultEngine.ini). Ramps stay
    under 35 degrees so the navmesh (and bots) can climb them.
  * Lights are movable (Lumen), nothing to bake. Each map has its own eye
    adaptation range and exposure bias (C20), checked on the tour screenshots
    with Scripts/measure_exposure.py.
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
REVERB_DIR = "/Game/Audio/Reverb"
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


# Reverb presets (phase 4, moved here from C9): decay in seconds, and how loud
# the early reflections and the tail are. Hall: the high-bay warehouse and the
# Depot hangar, big bare metal. Room: offices and houses.
REVERBS = {
    "RE_Hall": dict(density=1.0, diffusion=0.85, gain=0.32, gain_hf=0.6, decay_time=2.6, decay_hf_ratio=0.7,
                    reflections_gain=0.25, reflections_delay=0.03, late_gain=1.1, late_delay=0.03),
    "RE_Room": dict(density=0.9, diffusion=0.9, gain=0.3, gain_hf=0.8, decay_time=0.7, decay_hf_ratio=0.8,
                    reflections_gain=0.5, reflections_delay=0.008, late_gain=0.9, late_delay=0.012),
}


def reverb_effect(name):
    path = REVERB_DIR + "/" + name
    if EAL.does_asset_exist(path):
        effect = EAL.load_asset(path)
    else:
        if not EAL.does_directory_exist(REVERB_DIR):
            EAL.make_directory(REVERB_DIR)
        effect = ASSET_TOOLS.create_asset(name, REVERB_DIR, unreal.ReverbEffect, unreal.ReverbEffectFactory())
    for key, value in REVERBS[name].items():
        effect.set_editor_property(key, value)
    EAL.save_loaded_asset(effect)
    return effect


def ensure_nanite():
    """Poly Haven props are scans (the road barrier alone is 80k triangles):
    Nanite draws them at the detail the screen needs. Without it Depot put
    7 million triangles on screen (map audit, phase 4)."""
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    for data in reg.get_assets_by_path(PROPS[:-1], recursive=True):
        if str(data.asset_class_path.asset_name) != "StaticMesh":
            continue
        mesh = EAL.load_asset(str(data.package_name))
        if mesh.get_num_triangles(0) < 2000:
            continue  # the ammo machine and such: not worth it
        settings = mesh.get_editor_property("nanite_settings")
        if not settings.enabled:
            mesh.modify()
            settings.enabled = True
            mesh.set_editor_property("nanite_settings", settings)
            # The package is not marked dirty from Python: save regardless.
            saved = EAL.save_loaded_asset(mesh, only_if_is_dirty=False)
            log("Nanite on %s (%d triangles)%s" % (data.package_name, mesh.get_num_triangles(0), "" if saved else " - SAVE FAILED"))


def truck(b, x, y0, y1, cab_at_low_end, trailer_mat, cab_mat, folder):
    """Trailer (and cab) parked along Y, 2.5 m wide, centred on x."""
    w = 125
    cab = 300
    t0, t1 = (y0 + cab, y1) if cab_at_low_end else (y0, y1 - cab)
    b.box(x - w, x + w, t0, t1, 110, 420, trailer_mat, folder, "Trailer")
    c0, c1 = (y0, t0) if cab_at_low_end else (t1, y1)   # no gap to see through
    b.box(x - w + 10, x + w - 10, c0, c1, 60, 330, cab_mat, folder, "Cab")
    for yy in (t0 + 150, t0 + 450, t1 - 150, c0 + 100, c1 - 100):
        for side in (-1, 1):
            b.box(x + side * (w + 5) - 18, x + side * (w + 5) + 18, yy - 55, yy + 55, 0, 110, surface("rusty_painted_metal"), folder, "Wheel")


class Builder:
    """Places geometry into the currently open level."""

    def __init__(self, name):
        self.name = name
        self.cube = unreal.load_object(None, "/Engine/BasicShapes/Cube.Cube")
        self.cylinder_mesh = unreal.load_object(None, "/Engine/BasicShapes/Cylinder.Cylinder")
        self.count = 0
        self.starts = {"Alpha": 0, "Bravo": 0, "": 0}
        self.machines = 0

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

    def machine(self, x, y, yaw, z=0.0):
        """Ammo machine; the player uses it from 80 cm in front (+X of the actor after yaw)."""
        actor = ACTORS.spawn_actor_from_class(unreal.CSAmmoMachine, unreal.Vector(x, y, z), unreal.Rotator(0, 0, yaw))
        actor.set_actor_label("AmmoMachine_%d" % self.machines)
        actor.set_folder_path("AmmoMachines")
        self.machines += 1
        return actor

    def reverb(self, x0, x1, y0, y1, z0, z1, effect, volume=0.6, folder="Audio"):
        """Reverb over a box (an interior). The default volume brush is a 200 cm cube."""
        vol = ACTORS.spawn_actor_from_class(unreal.AudioVolume, unreal.Vector((x0 + x1) / 2.0, (y0 + y1) / 2.0, (z0 + z1) / 2.0), unreal.Rotator(0, 0, 0))
        vol.set_actor_scale3d(unreal.Vector(abs(x1 - x0) / 200.0, abs(y1 - y0) / 200.0, abs(z1 - z0) / 200.0))
        settings = vol.get_editor_property("settings")
        settings.set_editor_property("apply_reverb", True)
        settings.set_editor_property("reverb_effect", effect)
        settings.set_editor_property("volume", volume)
        settings.set_editor_property("fade_time", 0.4)
        vol.set_editor_property("settings", settings)
        vol.set_actor_label("Reverb_" + effect.get_name())
        vol.set_folder_path(folder)
        return vol

    def light(self, x, y, z, intensity=5000.0, radius=1600.0, color=(1.0, 0.85, 0.65), folder="Lights"):
        actor = ACTORS.spawn_actor_from_class(unreal.PointLight, unreal.Vector(x, y, z), unreal.Rotator(0, 0, 0))
        comp = actor.get_editor_property("point_light_component")
        comp.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        comp.set_editor_property("intensity", intensity)
        comp.set_editor_property("attenuation_radius", radius)
        comp.set_editor_property("light_color", unreal.Color(int(color[2] * 255), int(color[1] * 255), int(color[0] * 255), 255))
        actor.set_folder_path(folder)
        return actor

    def environment(self, sun_pitch, sun_yaw, sun_intensity, fog_density, sun_color=(1.0, 0.95, 0.85),
                    exposure=(0.6, 2.0, 0.0)):
        """exposure: (min, max, bias). Eye adaptation stays within [min, max] scene
        luminance (legacy units: ExtendDefaultLuminanceRange is off); bias in stops (C20)."""
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
        settings.set_editor_property("auto_exposure_min_brightness", exposure[0])
        settings.set_editor_property("override_auto_exposure_max_brightness", True)
        settings.set_editor_property("auto_exposure_max_brightness", exposure[1])
        settings.set_editor_property("override_auto_exposure_bias", True)
        settings.set_editor_property("auto_exposure_bias", exposure[2])
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
    # The scan is near white: in sun next to shade it blew out (AUDIT C20).
    concrete_floor = tinted("concrete_floor_worn_001", "ConcreteFloor_Grey", (0.6, 0.6, 0.6))
    concrete_wall = surface("concrete_wall_003")
    iron = tinted("corrugated_iron", "Hall_Grey", (0.62, 0.66, 0.7))      # the scans are near white
    plate = tinted("metal_plate", "Plate_Grey", (0.7, 0.7, 0.72))
    painted = surface("painted_concrete")
    rust = surface("rusty_painted_metal")
    planks = surface("weathered_planks")
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
    # Catwalk along the north wall, 3 m up, a ramp at each end (26 and 29
    # degrees: the old single ramp was 42 and the navmesh never climbed it).
    b.box(-350, 650, 620, 985, 300, 320, plate, "Hangar", "Catwalk")
    for x in (-330, 150, 630):
        b.box(x - 12, x + 12, 620, 644, 0, 300, rust, "Hangar", "CatwalkPost")
    b.wall((-350, 620), (650, 620), 320, 420, 8, rust, [(50, 250, 320, 420)], "Hangar", "Railing")
    b.ramp(-1000, -350, 620, 985, 4, 320, plate, "x", "Hangar")
    b.ramp(650, 1270, 620, 985, 320, 4, plate, "x", "Hangar")
    # Cover inside the hangar.
    for (x, y, yaw) in ((-500, -300, 0), (0, 200, 90), (500, -400, 0), (-300, 500, 15), (350, 350, 90)):
        b.prop("old_military_crate", x, y, 4, yaw)
    for (x, y) in ((-550, 0), (550, 0), (0, -650)):
        b.box(x - 110, x + 110, y - 110, y + 110, 4, 170, plate, "Hangar", "PalletStack")
        b.prop("wooden_crate_02", x, y, 170, 30)
    # Shelving inside the west and east doors (map audit: lines through the
    # doors and the south doors ran 67-89 m). Walk round either end: 2.8 m to
    # the south wall, 1.7 m to the catwalk ramp.
    for s in (-1, 1):
        x0, x1 = sorted((s * 900, s * 1000))
        b.box(x0, x1, -700, 450, 4, 300, rust, "Hangar", "Shelving")
        b.box(x0 - 10, x1 + 10, -700, 450, 150, 160, plate, "Hangar", "Shelf")
    for (x, y) in ((-1150, -850), (-1080, -800), (1100, -850), (1040, -800)):
        b.prop("Barrel_01", x, y, 4)
    b.light(-600, 0, 850, 5000, 2200)
    b.light(600, 0, 850, 5000, 2200)
    b.light(0, 800, 700, 5000, 1400)

    # ---- Container yard, north: lanes between rows, some stacked ----
    # Containers stand end to end (they used to leave 40 cm slits that showed
    # the whole yard); three north-south lanes cut each row.
    rows = [1650, 2350, 3050]
    row_x = (-3000, -2390, -1100, 490, 1780, 2390)
    for ri, y in enumerate(rows):
        for k, x in enumerate(row_x):
            mat = container_colors[(ri * 3 + k) % len(container_colors)]
            b.box(x, x + 610, y - 122, y + 122, 0, 260, mat, "Containers", "Container")
            if (ri + k) % 3 == 0:
                b.box(x + 10, x + 600, y - 118, y + 118, 260, 520, container_colors[(ri + k + 2) % len(container_colors)], "Containers", "Container")
    # A container turned across a lane as a crossing cover point.
    b.box(-250, 250, 2700, 2700 + 610, 0, 260, container_colors[3], "Containers", "Container")
    for (x, y) in ((-1200, 2700), (1300, 2700)):
        b.prop("concrete_road_barrier", x, y, 0, 90)

    # ---- Cover against the long east-west lines (phase 4 map audit) ----
    # Every lane that ran the full 96 m gets short containers at +-2400 and
    # +-900 (or +-1150) covering opposite halves of it: no line is longer than
    # the stretch between two of them, and the lane still zigzags through.
    def stub(x, y0, y1, colour, label="ShortContainer"):
        b.box(x - 122, x + 122, y0, y1, 0, 260, container_colors[colour % len(container_colors)], "Cover", label)

    for s in (-1, 1):
        stub(s * 2400, 1015, 1300, 0)          # north of the hangar
        stub(s * 900, 1250, 1528, 1)
        stub(s * 2400, 1772, 2050, 2)          # between container rows
        stub(s * 900, 1950, 2228, 3)
        stub(s * 2400, 2472, 2700, 4)
        stub(s * 2400, 3172, 3400, 0)          # along the north wall
        stub(s * 900, 3330, 3570, 1)
        stub(s * 2400, -1900, -1290, 2)        # south of the hangar, two high:
        stub(s * 1150, -1700, -1015, 3)        # the dock looks over single ones
        for (sx, y0, y1, c) in ((2400, -1900, -1290, 3), (1150, -1700, -1015, 4)):
            b.box(s * sx - 118, s * sx + 118, y0 + 4, y1 - 4, 260, 520, container_colors[c], "Cover", "ShortContainer")
        b.box(s * 2400 - 150, s * 2400 + 150, -3570, -3326, 0, 260, container_colors[4], "Cover", "ShortContainer")
        # North-south lines along the base sides.
        # Stacked two high: the dock and the catwalk look over single ones.
        for (xa, xb, ya, yb, c) in ((4760, 4150, 1900, 2144, 1), (4300, 3690, -2100, -1856, 3), (3800, 3000, 2600, 2844, 0),
                                    (3700, 2900, -2400, -2156, 2)):
            b.box(s * xa, s * xb, ya, yb, 0, 260, container_colors[c], "Cover", "Container")
            b.box(s * xa, s * xb, ya + 4, yb - 4, 260, 520, container_colors[c + 1], "Cover", "Container")
        # Crate stacks in the north-south lanes between the rows.
        x0, x1 = sorted((s * 1780, s * 1440))
        b.box(x0, x1, 1790, 2210, 0, 240, planks, "Cover", "CrateStack")
        x0, x1 = sorted((s * 1440, s * 1100))
        b.box(x0, x1, 2490, 2910, 0, 240, planks, "Cover", "CrateStack")

    # ---- Office building, south-west: two rooms, doors and windows ----
    ox0, ox1, oy0, oy1, oh = -2700, -700, -3300, -1900, 380
    b.box(ox0, ox1, oy0, oy1, 0, 6, concrete_floor, "Office", "OfficeFloor")
    win = 120, 240
    b.wall((ox0, oy1), (ox1, oy1), 0, oh, 25, painted, [(-2450, -2250, 0, 230), (-1350, -1050, win[0], win[1])], "Office")
    b.wall((ox0, oy0), (ox1, oy0), 0, oh, 25, painted, [(-2200, -1900, win[0], win[1])], "Office")
    b.wall((ox0, oy0), (ox0, oy1), 0, oh, 25, painted, [(-3100, -2900, win[0], win[1])], "Office")
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
    b.ramp(3200, 3700, -3300, -2900, 120, 0, painted, "x", "Dock")   # up towards the platform
    for (x, y) in ((1300, -2900), (2000, -3050), (2750, -3100)):
        b.prop("wooden_crate_02", x, y, 120, 20)
    b.box(2200, 2440, -2820, -2420, 120, 360, planks, "Dock", "PalletStack")
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
        # Blast wall between the base and the hangar doors: before it, 40 pairs
        # of Alpha and Bravo starts saw each other through the hangar (audit).
        wx = sx - side * 700
        b.box(wx - 30, wx + 30, -900, 900, 0, 350, concrete_wall, "Base" + team, "BlastWall")
        b.box(wx - 32, wx + 32, -902, 902, 0, 40, yellow_paint, "Base" + team, "BlastWallFoot")
        yaw = 0 if side < 0 else 180
        for i in range(10):
            b.start(team, sx + (i // 5 - 0.5) * 220 * -side, -600 + (i % 5) * 300, yaw)

    # ---- Free-for-all spawns all over the map ----
    for (x, y, yaw) in ((-3100, 3450, -10), (3100, 3450, -170), (0, 3350, -90), (-2000, 2000, 0),
                        (2000, 2000, 180), (-1150, 750, 0), (800, -600, 180), (-2200, -2600, 45),
                        (-1100, -2400, 90), (3300, 1500, 180), (-3300, -1500, 0),
                        (-3600, 3200, -45), (3900, -2600, 135), (0, -2900, 90), (-500, 200, 0)):
        b.start("", x, y, yaw)
    b.start("", 2000, -2800, 90, 120)   # on the loading dock

    # ---- Ammo machines (C19): one per base, two in the middle ----
    b.machine(-4720, 0, 0)
    b.machine(4720, 0, 180)
    b.machine(0, -940, 90, 4)    # inside the hangar, south wall
    b.machine(0, 3520, -90)      # container yard, north wall

    # ---- Reverb ----
    b.reverb(-hx, hx, -hy, hy, 0, hh, reverb_effect("RE_Hall"))
    b.reverb(ox0, ox1, oy0, oy1, 0, oh, reverb_effect("RE_Room"))

    b.environment(sun_pitch=-38, sun_yaw=35, sun_intensity=9.0, fog_density=0.012, sun_color=(1.0, 0.93, 0.82),
                  exposure=(0.6, 2.0, -0.1))
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
        b.reverb(x0, x1, y0, y1, 0, h, reverb_effect("RE_Room"), folder=folder)
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
    # Stone terraces in the four corners of the square, 1.5 m up, a ramp each
    # (map audit: the square had no height at all). Planters on top.
    for mx in (-1, 1):
        for my in (-1, 1):
            t0, t1 = sorted((mx * 1300, mx * 900))
            u0, u1 = sorted((my * 800, my * 1100))
            b.box(t0, t1, u0, u1, 0, 158, tiles, "Square", "Terrace")
            r0, r1 = sorted((mx * 900, mx * 500))
            v0, v1 = sorted((my * 850, my * 1050))
            b.ramp(r0, r1, v0, v1, 158 if mx < 0 else 8, 8 if mx < 0 else 158, tiles, "x", "Square")
            b.prop("planter_box_01", mx * 1200, my * 1000, 158, 45)
    for (x, y, yaw) in ((-150, -1000, 0), (150, 1000, 180)):
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
    # Houses on the town wall reach it: a 60 cm gap along the wall showed 79 m of street.
    house(b, -4460, -3300, 1600, 2800, 1000, castle, roof, roof="flat", folder="Houses/NW", trim=concrete)   # tower base
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
    house(b, 3500, 4460, 1300, 2600, 700, pick(), roof, [("W", 1900, 200)], True, folder="Houses/NE")
    house(b, 700, 1600, 2700, 3700, 900, pick(), roof, folder="Houses/NE", trim=brick)
    house(b, 1900, 3200, 2600, 3700, 750, pick(), roof, [("S", 2550, 180)], True, folder="Houses/NE")
    house(b, 1700, 3200, 600, 1100, 600, pick(), roof, folder="Houses/NE")
    house(b, 3500, 4460, 400, 1000, 900, castle, roof, folder="Houses/NE")
    # South-west quarter.
    house(b, -3200, -1900, -2400, -1400, 800, pick(), roof, [("N", -2550, 180), ("E", -1900, 160)], True, folder="Houses/SW")
    house(b, -1700, -700, -2400, -1500, 1000, brick, roof, folder="Houses/SW")
    house(b, -4460, -3400, -2600, -1300, 700, pick(), roof, folder="Houses/SW", trim=brick)
    house(b, -3000, -1600, -3700, -2700, 900, pick(), roof, folder="Houses/SW")
    house(b, -1300, -700, -3700, -2700, 700, pick(), roof, [("N", -1000, 180)], True, folder="Houses/SW")   # door off the gatehouse ramp
    house(b, -4300, -3400, -1000, -500, 600, pick(), roof, folder="Houses/SW")
    house(b, -3200, -1700, -1100, -600, 650, pick(), roof, folder="Houses/SW", trim=brick)
    # South-east quarter.
    house(b, 700, 1800, -2400, -1400, 900, pick(), roof, [("N", 1250, 180), ("W", -1900, 160)], True, folder="Houses/SE")
    house(b, 2100, 3200, -2300, -1400, 700, pick(), roof, folder="Houses/SE", trim=brick)
    house(b, 3500, 4460, -2600, -1300, 1100, brick, roof, folder="Houses/SE")
    house(b, 700, 1700, -3700, -2700, 800, pick(), roof, folder="Houses/SE")
    house(b, 2000, 3300, -3700, -2700, 700, pick(), roof, [("N", 2650, 200)], True, folder="Houses/SE")
    house(b, 1700, 3200, -1100, -600, 600, pick(), roof, folder="Houses/SE")
    house(b, 3500, 4460, -1000, -400, 850, castle, roof, folder="Houses/SE")

    # Arches over the main street at both ends of the square.
    for y in (-1300, 1300):
        b.wall((-700, y), (700, y), 0, 900, 120, castle, [(-450, 450, 0, 600)], "Arches")

    # Sidewalk strips along the main street.
    for x0, x1 in ((-700, -500), (500, 700)):
        b.box(x0, x1, -Y + 100, -1100, 0, 15, tiles, "Street", "Sidewalk")
        b.box(x0, x1, 1100, Y - 100, 0, 15, tiles, "Street", "Sidewalk")

    # Gatehouses at both ends of the main street (phase 4). Before them the
    # street ran 80 m in a straight line from base to base and 56 pairs of
    # Alpha and Bravo starts saw each other (map audit). The flat roof, reached
    # by a ramp from the base side, looks up the street from behind a parapet.
    for side in (-1, 1):
        g0, g1 = sorted((side * 2850, side * 3250))
        b.box(-700, 420, g0, g1, 0, 450, castle, "Gatehouses", "Gatehouse")
        b.box(680, 700, g0, g1, 0, 450, castle, "Gatehouses", "Gatehouse")
        b.box(420, 680, g0, g1, 300, 450, castle, "Gatehouses", "GateLintel")
        b.box(-720, 720, g0 - 20, g1 + 20, 450, 470, tiles, "Gatehouses", "GatehouseRoof")
        street = g1 if side < 0 else g0       # the face towards the square
        p0, p1 = sorted((street, street + side * 30))
        b.box(-720, 720, p0, p1, 470, 570, castle, "Gatehouses", "Parapet")
        # Ramp on the base side, 2 m wide, rising 4.7 m over 10 m (25 degrees),
        # its foot 1 m clear of the house so the navmesh joins it to the street.
        r0, r1 = sorted((side * 3250, side * 3450))
        b.ramp(-600, 400, r0, r1, 0, 470, tiles, "x", "Gatehouses")
        b.box(400, 720, r0, r1, 450, 470, tiles, "Gatehouses", "RampLanding")
        b.light(0, side * 3050, 420, 1500, 900, (1.0, 0.8, 0.55), "Gatehouses")

    # Kiosks across the east-west street: the three of them block every line
    # along it (80 m from wall to wall before), and mirror north and south.
    for (k0, k1, x) in ((-600, -150, 2000), (150, 600, 2000), (-150, 150, -2000)):
        b.box(x - 180, x + 180, k0, k1, 0, 260, planks, "Street", "Kiosk")
        b.box(x - 220, x + 220, k0 - 40, k1 + 40, 260, 275, awnings[(x > 0) + (k0 > 0)], "Street", "KioskRoof")

    # Crate stacks and carts against the other long lines (map audit): in the
    # alleys by the square, the alleys along the town wall, the east street and
    # the main street. Mirrored north and south.
    def stack(x0, x1, y0, y1, h=220):
        b.box(x0, x1, y0, y1, 0, h, planks, "Street", "CrateStack")

    for m in (-1, 1):
        stack(-1900, -1700, *sorted((m * 1100, m * 1420)))
        stack(1900, 2100, *sorted((m * 1250, m * 1500)))
        stack(-1600, -1400, *sorted((m * 3830, m * 3960)))
        stack(1400, 1600, *sorted((m * 3700, m * 3830)))
        stack(3300, 3420, *sorted((m * 2200, m * 2400)))
        stack(3380, 3500, *sorted((m * 600, m * 800)))
        stack(-3330, -3170, *sorted((m * 1150, m * 1400)))   # the street along x = -3250
        # Cart in the main street, in line with the gate tunnels.
        c0, c1 = sorted((m * 2400, m * 2700))
        b.box(150, 700, c0, c1, 50, 190, planks, "Street", "Cart")
        for wx in (200, 650):
            b.cylinder(wx, (c0 + c1) / 2.0, 0, 50, 60, planks, "Street", "CartWheel")

    # Street clutter as cover.
    for (x, y, yaw) in ((-250, -2200, 0), (250, 2200, 0), (-300, -3200, 20), (300, 3200, -20), (2400, 200, 90), (-2400, -200, 90)):
        b.prop("wooden_crate_02", x, y, 0, yaw)
    for (x, y) in ((300, -1800), (-300, 1800), (3900, 1150), (-3900, -1150)):
        b.prop("wine_barrel_01", x, y)
    for (x, y, yaw) in ((-3000, 250, 0), (3000, -250, 0)):
        b.prop("old_military_crate", x, y, 0, yaw)
    for (x, y, yaw) in ((-600, -1500, 0), (600, 1500, 180), (4100, 3000, 0), (-4100, -3000, 0)):
        b.prop("cardboard_box_01", x, y, 0, yaw)
    for (x, y) in ((-3800, 3800 - 300), (3800, -3500)):
        b.prop("utility_box_02", x, y, 0, 180)

    # ---- Spawns: Alpha south, Bravo north, free-for-all spread out ----
    for i in range(10):
        b.start("Alpha", -450 + (i % 5) * 225, -3850 + (i // 5) * 200, 90)
        b.start("Bravo", -450 + (i % 5) * 225, 3850 - (i // 5) * 200, -90)
    for (x, y, yaw) in ((-2500, -1900, 0), (2500, 2400, 180), (-2500, 1850, 0), (2500, -2500, 180),
                        (-3900, 900, 0), (3900, -1150, 180), (-900, 0, 0), (900, 0, 180),
                        (0, -2000, 90), (0, 2000, -90), (-3900, -3600, 45), (3900, 3600, -135),
                        (1250, 1900, -90), (-1250, -1300, 90), (-1000, -3200, 0), (2650, -3200, 90)):
        b.start("", x, y, yaw)

    # ---- Ammo machines (C19): one per base, two on the square ----
    b.machine(-640, -3620, 0, 15)
    b.machine(-640, 3620, 0, 15)
    b.machine(-1240, 0, 0, 8)
    b.machine(1240, 0, 180, 8)

    b.environment(sun_pitch=-28, sun_yaw=-55, sun_intensity=8.0, fog_density=0.018, sun_color=(1.0, 0.86, 0.68),
                  exposure=(0.35, 2.0, 0.0))
    b.navmesh(-X, X, -Y, Y, -200, 1200)
    LEVELS.save_current_level()
    log("Lvl_OldTown: %d meshes, starts %s" % (b.count, b.starts))


# ---------------------------------------------------------------------------
# WAREHOUSE - logistics complex, 80 x 64 m (v2.0 phase 4, AUDIT K3)
# ---------------------------------------------------------------------------
#
#   north   loading dock: a 1.2 m platform along the hall, trailers backed up
#           to it; the dock is the north lane between the two sides
#   middle  high-bay hall: five rows of 6 m pallet racks with gaps that never
#           line up (no line through the hall), a mezzanine along each end
#           wall, a ramp from the central dock door down to the hall floor
#   south   container yard: columns of containers along Y, two of them with a
#           ramp onto the roof
#   west/east  the bases: open truck gate yards, a security office each
# Mirror symmetric across X = 0: Alpha west, Bravo east.

def rack(b, x, y0, y1, frame, beam, goods, folder="Hall/Racks"):
    """A pallet rack along Y, 1.1 m deep, 6 m tall. Pallets of goods (a list of
    materials, taken in turn) fill it end to end with no gap between them, so
    it is a wall for sight and bullets with the look of shelving."""
    w = 55
    levels = ((10, 180), (210, 380), (410, 580))
    count = max(1, int(round((y1 - y0 - 20) / 125.0)))
    step = (y1 - y0 - 20) / float(count)
    for li, (z0, z1) in enumerate(levels):
        for k in range(count):
            p0 = y0 + 10 + k * step
            # Not every pallet is stacked as high - above head height only, so the
            # gap under the beam never lines up with an eye.
            top = z1 - (15 if li > 0 and (k + li) % 4 == 1 else 0)
            b.box(x - w + 6, x + w - 6, p0, p0 + step, z0, top, goods[(k * 2 + li) % len(goods)], folder, "RackGoods")
    for zb in (180, 380, 580):
        b.box(x - w, x + w, y0, y1, zb, zb + (30 if zb < 580 else 20), beam, folder, "RackBeam")
    n = max(1, int(round((y1 - y0) / 270.0)))
    for k in range(n + 1):
        yy = y0 + (y1 - y0) * k / n
        yy = min(max(yy, y0 + 6), y1 - 6)
        for side in (-1, 1):
            b.box(x + side * w - 6, x + side * w + 6, yy - 6, yy + 6, 0, 600, frame, folder, "RackUpright")


def build_warehouse():
    new_level("/Game/Maps/Lvl_Warehouse")
    b = Builder("Warehouse")
    X, Y = 4000, 3200
    asphalt = surface("asphalt_02")
    concrete_floor = surface("concrete_floor_worn_001")
    concrete_wall = surface("concrete_wall_003")
    iron = surface("corrugated_iron")
    plate = surface("metal_plate")
    painted = surface("painted_concrete")
    rust = surface("rusty_painted_metal")
    planks = surface("weathered_planks")
    yellow_paint = tinted("painted_concrete", "Paint_Yellow", (1.0, 0.8, 0.25))
    # Racks in painted metal (the painted concrete scan is green under any tint).
    rack_blue = tinted("rusty_painted_metal", "Rack_Blue", (0.22, 0.36, 0.72))
    rack_orange = tinted("rusty_painted_metal", "Rack_Orange", (0.95, 0.45, 0.12))
    kraft = tinted("weathered_planks", "Goods_Kraft", (0.78, 0.62, 0.44))
    wrapped = tinted("white_plaster_02", "Goods_Wrapped", (0.8, 0.84, 0.88))
    crates = tinted("weathered_planks", "Goods_Crate", (0.55, 0.42, 0.3))
    blue_wrap = tinted("white_plaster_02", "Goods_BlueWrap", (0.45, 0.6, 0.8))
    goods_a = [kraft, wrapped, crates, kraft, blue_wrap]
    goods_b = [wrapped, crates, kraft, blue_wrap, kraft]
    floor_grey = tinted("concrete_floor_worn_001", "ConcreteFloor_Grey", (0.6, 0.6, 0.6))
    hall_wall = tinted("corrugated_iron", "Hall_Grey", (0.62, 0.66, 0.7))
    containers = [
        tinted("rusty_painted_metal", "Container_Red", (0.75, 0.22, 0.16)),
        tinted("rusty_painted_metal", "Container_Blue", (0.22, 0.38, 0.7)),
        tinted("rusty_painted_metal", "Container_Green", (0.3, 0.52, 0.3)),
        tinted("rusty_painted_metal", "Container_Orange", (0.9, 0.5, 0.18)),
        tinted("rusty_painted_metal", "Container_Grey", (0.55, 0.57, 0.6)),
    ]
    trailer_white = tinted("rusty_painted_metal", "Trailer_White", (0.85, 0.86, 0.86))
    room = reverb_effect("RE_Room")

    # Ground and perimeter.
    b.box(-X, X, -Y, Y, -60, 0, asphalt, "Ground", "Asphalt")
    for (a, c) in (((-X, Y), (X, Y)), ((-X, -Y), (X, -Y)), ((-X, -Y), (-X, Y)), ((X, -Y), (X, Y))):
        b.wall(a, c, 0, 500, 60, concrete_wall, folder="Perimeter")

    # ---- High-bay hall: 36 x 28 m, 10 m high ----
    hx, hy, hh = 1800, 1400, 1000
    b.box(-hx, hx, -hy, hy, 0, 4, floor_grey, "Hall", "HallFloor")
    for s in (-1, 1):
        # Walk door at the south end, roll-up door under the mezzanine.
        b.wall((s * hx, -hy), (s * hx, hy), 0, hh, 30, hall_wall, [(-1300, -1000, 0, 280), (300, 900, 0, 330)], "Hall")
    b.wall((-hx, -hy), (hx, -hy), 0, hh, 30, hall_wall, [(-950, -650, 0, 280), (650, 950, 0, 280)], "Hall")
    b.wall((-hx, hy), (hx, hy), 0, hh, 30, hall_wall, [(-200, 200, 120, 450)], "Hall")   # dock door
    b.box(-hx - 40, hx + 40, -hy - 40, hy + 40, hh, hh + 25, plate, "Hall", "HallRoof")
    # Closed dock doors either side of the open one (look only).
    for x in (-1000, -600, 600, 1000):
        b.box(x - 160, x + 160, hy + 15, hy + 22, 120, 450, rack_blue, "Hall", "DockDoor")
    # Racks. Rows at +-1200 and 0 leave cross aisles at both ends, rows at +-600
    # one in the middle, so no straight line crosses the hall.
    for s in (-1, 1):
        rack(b, s * 1200, -1000, 1000, rack_blue, rack_orange, goods_a)
        rack(b, s * 600, -1385, -300, rack_blue, rack_orange, goods_b)
        rack(b, s * 600, 300, 1385, rack_blue, rack_orange, goods_a)
    rack(b, 0, -1000, 700, rack_blue, rack_orange, goods_b)
    # Ramp from the dock door down to the hall floor (12 degrees).
    b.ramp(-200, 200, 800, 1385, 4, 124, plate, "y", "Hall")
    b.box(-205, -200, 800, 1385, 4, 130, yellow_paint, "Hall", "RampEdge")
    b.box(200, 205, 800, 1385, 4, 130, yellow_paint, "Hall", "RampEdge")
    # Mezzanines along both end walls, 3.5 m up, stairs from the south.
    for s in (-1, 1):
        m0, m1 = sorted((s * 1785, s * 1485))
        b.box(m0, m1, -200, 1385, 350, 370, plate, "Hall/Mezzanine", "Mezzanine")
        rail_x = s * 1485
        b.wall((rail_x, -200), (rail_x, 1385), 370, 470, 8, rust, [], "Hall/Mezzanine", "Railing")
        for yy in (-150, 400, 950):
            b.box(rail_x - 12, rail_x + 12, yy - 12, yy + 12, 0, 350, rust, "Hall/Mezzanine", "Post")
        s0, s1 = sorted((s * 1785, s * 1535))
        b.ramp(s0, s1, -900, -200, 4, 370, plate, "y", "Hall/Mezzanine")
        b.prop("cardboard_box_01", s * 1650, 900, 370, 20)
        b.prop("wooden_crate_01", s * 1640, 300, 370, 0)
    # Pallets and forklifts' worth of clutter in the aisles (low cover).
    for s in (-1, 1):
        for (x, y, yaw) in ((900, -1200, 0), (300, -600, 90), (900, 1150, 15), (300, 1000, 0)):
            b.prop("wooden_crate_02", s * x, y, 4, yaw)
        b.prop("Barrel_01", s * 1400, 1250, 4)
    for (x, y) in ((-1200, 0), (1200, 0), (-600, 900), (600, -900)):
        b.light(x, y, 900, 5000, 2000, (0.9, 0.95, 1.0), "Hall")
    b.light(0, -1150, 800, 3000, 1400, (0.9, 0.95, 1.0), "Hall")
    b.reverb(-hx, hx, -hy, hy, 0, hh, reverb_effect("RE_Hall"), folder="Hall")

    # ---- Loading dock, north ----
    b.box(-1400, 1400, hy + 15, 2000, 0, 120, floor_grey, "Dock", "DockPlatform")
    b.box(-1400, 1400, 1980, 2000, 0, 122, yellow_paint, "Dock", "DockEdge")
    b.ramp(-1900, -1400, hy + 15, 2000, 0, 120, floor_grey, "x", "Dock")
    b.ramp(1400, 1900, hy + 15, 2000, 120, 0, floor_grey, "x", "Dock")
    # Steps down into the yard between the trailers.
    b.ramp(-200, 200, 2000, 2550, 120, 0, floor_grey, "y", "Dock")
    # Trailers backed up to the dock: the yard behind the dock splits in three.
    for s in (-1, 1):
        truck(b, s * 1000, 2000, Y - 30, False, trailer_white, containers[1 if s < 0 else 0], "Dock")
    for (x, y) in ((-600, 1700), (600, 1700)):
        b.prop("wooden_crate_02", x, y, 120, 10)
    for (x, y) in ((-1300, 1600), (1300, 1600)):
        b.prop("Barrel_01", x, y, 120)
    for x in (-900, 0, 900):
        b.light(x, 1800, 480, 2500, 1200, (1.0, 0.85, 0.6), "Dock")

    # ---- Container yard, south ----
    def container(x, y0, y1, levels, mat_index, label="Container"):
        for k in range(levels):
            b.box(x - 122, x + 122, y0 + k * 4, y1 - k * 4, k * 260, (k + 1) * 260, containers[(mat_index + k) % len(containers)], "Yard", label)

    for s in (-1, 1):
        container(s * 2100, -Y + 40, -Y + 650, 2, 0 if s < 0 else 3)
        container(s * 2100, -2250, -1640, 1, 2)
        container(s * 800, -2700, -2090, 1, 4 if s < 0 else 1)
        # Ramp onto the container roof (30 degrees).
        r0, r1 = s * 800 - 122, s * 800 + 122
        b.ramp(r0, r1, -Y + 40, -2700, 0, 260, rust, "y", "Yard")
    container(0, -Y + 40, -Y + 650, 2, 1)
    for s in (-1, 1):
        b.prop("concrete_road_barrier", s * 1450, -1700, 0, 90)
        b.prop("old_military_crate", s * 400, -2200, 0, 0)
        b.prop("Barrel_01", s * 2500, -2900)
        b.prop("Barrel_01", s * 2560, -2850)
    b.light(0, -2300, 600, 2500, 1600, (1.0, 0.85, 0.6), "Yard")

    # ---- Pallet stacks against the long lines (map audit) ----
    for s in (-1, 1):
        x0, x1 = sorted((s * 2250, s * 2550))
        b.box(x0, x1, hy, 1640, 0, 240, kraft, "Cover", "PalletStack")          # by the dock ends
        x0, x1 = sorted((s * 2700, s * 3100))
        b.box(x0, x1, -1640, -hy, 0, 240, wrapped, "Cover", "PalletStack")      # south of the hall
        x0, x1 = sorted((s * 1830, s * 2650))
        b.box(x0, x1, -700, -450, 0, 240, kraft, "Cover", "PalletStack")        # between hall and base
        x0, x1 = sorted((s * 2900, s * 2650))
        b.box(x0, x1, 150, 400, 0, 200, wrapped, "Cover", "PalletStack")

    # ---- Bases: truck gate yards, west (Alpha) and east (Bravo) ----
    for s, team in ((-1, "Alpha"), (1, "Bravo")):
        b.box(s * 3970, s * 3000, -1000, 1000, 0, 4, concrete_floor, "Base" + team, "BaseFloor")
        # Security office north of the base, a door to the south and a window east.
        o0, o1 = sorted((s * 3970, s * 3100))
        b.box(o0, o1, 1500, 2300, 0, 6, concrete_floor, "Base" + team, "OfficeFloor")
        door = (s * 3700 - 150, s * 3700 + 150)
        b.wall((o0, 1500), (o1, 1500), 0, 380, 25, painted, [(min(door), max(door), 0, 230)], "Base" + team, "OfficeWall")
        b.wall((o0, 2300), (o1, 2300), 0, 380, 25, painted, [], "Base" + team, "OfficeWall")
        inner = o1 if s < 0 else o0
        # Window behind the truck cab: level with the other office's, it gave a 79 m line.
        b.wall((inner, 1500), (inner, 2300), 0, 380, 25, painted, [(2120, 2280, 120, 230)], "Base" + team, "OfficeWall")
        b.box(o0 - 20, o1 + 20, 1480, 2320, 380, 400, concrete_wall, "Base" + team, "OfficeRoof")
        b.prop("wooden_crate_01", s * 3800, 2150, 6, 0)
        b.light(s * 3530, 1900, 350, 2000, 800, (1.0, 0.85, 0.65), "Base" + team)
        b.reverb(o0, o1, 1500, 2300, 0, 380, room, folder="Base" + team)
        # Truck at the gate, a tank farm to the south.
        truck(b, s * 2750, 2100, Y - 30, True, containers[2], containers[4], "Base" + team)
        b.cylinder(s * 3500, -2300, 0, 160, 450, rust, "Base" + team, "Tank")
        b.cylinder(s * 3500, -2750, 0, 160, 450, rust, "Base" + team, "Tank")
        b.prop("concrete_road_barrier", s * 2700, -1300, 0, 0)
        b.prop("concrete_road_barrier", s * 2700, 1300, 0, 0)
        b.prop("utility_box_02", s * 3950, -1200, 0, 90 if s < 0 else -90)
        b.light(s * 3500, 0, 600, 2500, 1600, (1.0, 0.9, 0.75), "Base" + team)
        yaw = 0 if s < 0 else 180
        for i in range(10):
            b.start(team, s * (3600 - (i // 5) * 300), -800 + (i % 5) * 400, yaw)

    # ---- Free-for-all spawns, mirrored ----
    for (x, y, z, yaw) in ((900, -800, 4, 90), (300, 400, 4, -90), (800, 1700, 120, 0), (2000, 2600, 0, -90),
                           (1450, -2400, 0, 90), (400, -3000, 0, 90), (3500, 1900, 6, -90), (3000, -2600, 0, 90)):
        for s in (-1, 1):
            b.start("", s * x, y, yaw if s > 0 else 180 - yaw, z)

    # ---- Ammo machines (C19) ----
    b.machine(-3920, 0, 0, 4)
    b.machine(3920, 0, 180, 4)
    b.machine(0, -1340, 90, 4)      # hall, south cross aisle
    b.machine(0, Y - 80, -90)       # dock yard, between the trailers

    b.environment(sun_pitch=-42, sun_yaw=30, sun_intensity=8.5, fog_density=0.01, sun_color=(1.0, 0.94, 0.84),
                  exposure=(0.45, 2.0, -0.2))
    b.navmesh(-X, X, -Y, Y, -200, 1100)
    LEVELS.save_current_level()
    log("Lvl_Warehouse: %d meshes, starts %s, machines %d" % (b.count, b.starts, b.machines))


def main():
    wanted = [m.strip().lower() for m in os.environ.get("CS_MAPS", "depot,oldtown,warehouse").split(",") if m.strip()]
    ensure_nanite()
    if "depot" in wanted:
        build_depot()
    if "oldtown" in wanted:
        build_oldtown()
    if "warehouse" in wanted:
        build_warehouse()
    log("done")
    flush_log()


try:
    main()
except Exception as e:
    import traceback
    log("FAILED: %s\n%s" % (e, traceback.format_exc()))
    flush_log()
