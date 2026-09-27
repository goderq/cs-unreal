"""
v2.1 map rework: shared building blocks for the map scripts (docs/MAPS_REWORK.md).

MeshBuilder collects quads and triangles per material in plain Python lists and
hands them to Geometry Script in one call per material, then saves a Static
Mesh. UVs are world-space metres taken along the face's main axis, so pieces
built next to each other continue the same texture without seams (the
materials repeat every TileSize cm). Vertex colour R is the grime mask of
M_CS_Surface: low near the ground (dirty wall feet), 1 higher up.

Level helpers place meshes, props, instance clusters (ACSPropCluster),
decals, lights, player starts, ammo machines and reverb volumes, and write
the view list for the -cstestviews screenshots.
"""

import math
import os
import random

import unreal

EAL = unreal.EditorAssetLibrary
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LEVELS = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
GS = unreal

V21 = "/Game/Environment/V21"
GEN = "/Game/Environment/Generated"
PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())

_log = []


def log(msg):
    _log.append(str(msg))
    unreal.log_warning("[MAPS] " + str(msg))


def flush_log(name):
    path = os.path.join(PROJECT, "Saved", "Logs", name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(_log))


def ensure_dir(path):
    if not EAL.does_directory_exist(path):
        EAL.make_directory(path)


_cache = {}


def load(path):
    if path not in _cache:
        _cache[path] = EAL.load_asset(path) if EAL.does_asset_exist(path) else unreal.load_object(None, path)
        if _cache[path] is None:
            log("MISSING asset " + path)
    return _cache[path]


def surface(name):
    return load(V21 + "/Surfaces/MI_" + name)


def prop(model, mesh=None):
    return load("%s/Props/%s/%s" % (V21, model, mesh or ("SM_" + model)))


# ---------------------------------------------------------------------------
# Vector helpers (tuples)
# ---------------------------------------------------------------------------

def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def norm(a):
    length = math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]) or 1.0
    return (a[0] / length, a[1] / length, a[2] / length)


def lerp3(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)


# ---------------------------------------------------------------------------
# Mesh building
# ---------------------------------------------------------------------------

class MeshBuilder:
    """Geometry in world coordinates (cm), grouped by material."""

    def __init__(self, ground=None, grime_height=160.0):
        self.parts = {}          # material -> [verts, normals, uvs, colors, tris]
        self.order = []
        self.ground = ground or (lambda x, y: 0.0)
        self.grime_height = grime_height
        self.triangles = 0

    def _part(self, mat):
        if mat not in self.parts:
            self.parts[mat] = [[], [], [], [], []]
            self.order.append(mat)
        return self.parts[mat]

    def _grime(self, p):
        h = p[2] - self.ground(p[0], p[1])
        return max(0.3, min(1.0, 0.3 + 0.7 * h / self.grime_height))

    @staticmethod
    def _uv(p, n, uv_scale):
        ax, ay, az = abs(n[0]), abs(n[1]), abs(n[2])
        if az >= ax and az >= ay:
            u, v = p[0], p[1]
        elif ax >= ay:
            u, v = p[1] * (1 if n[0] > 0 else -1), -p[2]
        else:
            u, v = -p[0] * (1 if n[1] > 0 else -1), -p[2]
        return (u / 100.0 * uv_scale, v / 100.0 * uv_scale)

    def poly(self, pts, mat, normal=None, uvs=None, colors=None, uv_scale=1.0, grime=True):
        """Convex polygon, vertices counter-clockwise seen from the front."""
        if len(pts) < 3:
            return
        n = normal or norm(cross(sub(pts[1], pts[0]), sub(pts[2], pts[0])))
        verts, normals, uvl, cols, tris = self._part(mat)
        base = len(verts)
        for i, p in enumerate(pts):
            verts.append(p)
            normals.append(n)
            uvl.append(uvs[i] if uvs else self._uv(p, n, uv_scale))
            if colors:
                cols.append(colors[i])
            else:
                g = self._grime(p) if grime else 1.0
                cols.append((g, 1.0, 1.0, 1.0))
        for i in range(1, len(pts) - 1):
            # Unreal is left-handed: the front face winds the other way.
            tris.append((base, base + i + 1, base + i))
            self.triangles += 1

    def quad(self, a, b, c, d, mat, **kw):
        self.poly([a, b, c, d], mat, **kw)

    def box(self, x0, x1, y0, y1, z0, z1, mat, faces="nsewtb", top_mat=None, **kw):
        """Axis-aligned box. faces: any of n(+Y) s(-Y) e(+X) w(-X) t(top) b(bottom)."""
        x0, x1 = min(x0, x1), max(x0, x1)
        y0, y1 = min(y0, y1), max(y0, y1)
        z0, z1 = min(z0, z1), max(z0, z1)
        if "t" in faces:
            self.quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), top_mat or mat, **kw)
        if "b" in faces:
            self.quad((x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (x1, y0, z0), mat, **kw)
        if "s" in faces:
            self.quad((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1), mat, **kw)
        if "n" in faces:
            self.quad((x1, y1, z0), (x0, y1, z0), (x0, y1, z1), (x1, y1, z1), mat, **kw)
        if "w" in faces:
            self.quad((x0, y1, z0), (x0, y0, z0), (x0, y0, z1), (x0, y1, z1), mat, **kw)
        if "e" in faces:
            self.quad((x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1), mat, **kw)

    def obox(self, center, size, yaw, mat, pitch=0.0, faces="nsewtb", **kw):
        """Oriented box: size (x, y, z) around center, rotated by yaw then pitch (degrees)."""
        cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))

        def tr(x, y, z):
            # pitch about local Y (nose up), then yaw about Z
            x, z = x * cp - z * sp, x * sp + z * cp
            return (center[0] + x * cy - y * sy, center[1] + x * sy + y * cy, center[2] + z)

        hx, hy, hz = size[0] / 2.0, size[1] / 2.0, size[2] / 2.0
        c = {k: tr(*v) for k, v in {
            "000": (-hx, -hy, -hz), "100": (hx, -hy, -hz), "110": (hx, hy, -hz), "010": (-hx, hy, -hz),
            "001": (-hx, -hy, hz), "101": (hx, -hy, hz), "111": (hx, hy, hz), "011": (-hx, hy, hz)}.items()}
        if "t" in faces:
            self.quad(c["001"], c["101"], c["111"], c["011"], mat, **kw)
        if "b" in faces:
            self.quad(c["000"], c["010"], c["110"], c["100"], mat, **kw)
        if "s" in faces:
            self.quad(c["000"], c["100"], c["101"], c["001"], mat, **kw)
        if "n" in faces:
            self.quad(c["110"], c["010"], c["011"], c["111"], mat, **kw)
        if "w" in faces:
            self.quad(c["010"], c["000"], c["001"], c["011"], mat, **kw)
        if "e" in faces:
            self.quad(c["100"], c["110"], c["111"], c["101"], mat, **kw)

    def prism(self, footprint, z0, z1, mat, top=True, bottom=False, top_mat=None, **kw):
        """Vertical prism over a convex footprint (x, y) listed counter-clockwise from above."""
        n = len(footprint)
        for i in range(n):
            (ax, ay), (bx, by) = footprint[i], footprint[(i + 1) % n]
            self.quad((ax, ay, z0), (bx, by, z0), (bx, by, z1), (ax, ay, z1), mat, **kw)
        if top:
            self.poly([(x, y, z1) for x, y in footprint], top_mat or mat, **kw)
        if bottom:
            self.poly([(x, y, z0) for x, y in reversed(footprint)], mat, **kw)

    def cylinder(self, cx, cy, z0, z1, radius, mat, segments=12, caps=True, **kw):
        ring = [(cx + radius * math.cos(math.tau * i / segments), cy + radius * math.sin(math.tau * i / segments)) for i in range(segments)]
        for i in range(segments):
            (ax, ay), (bx, by) = ring[i], ring[(i + 1) % segments]
            mid = norm(((ax + bx) / 2 - cx, (ay + by) / 2 - cy, 0.0))
            self.quad((ax, ay, z0), (bx, by, z0), (bx, by, z1), (ax, ay, z1), mat, normal=mid, **kw)
        if caps:
            self.poly([(x, y, z1) for x, y in ring], mat, **kw)

    def tube(self, path, radius, mat, segments=10, **kw):
        """Round tube along a polyline of 3D points (pipes, cables, rails)."""
        rings = []
        for i, p in enumerate(path):
            a = path[max(0, i - 1)]
            b = path[min(len(path) - 1, i + 1)]
            t = norm(sub(b, a))
            up = (0.0, 0.0, 1.0) if abs(t[2]) < 0.9 else (1.0, 0.0, 0.0)
            u = norm(cross(t, up))
            v = norm(cross(u, t))
            rings.append([add(p, add(scale(u, radius * math.cos(math.tau * k / segments)), scale(v, radius * math.sin(math.tau * k / segments))))
                          for k in range(segments)])
        for i in range(len(rings) - 1):
            for k in range(segments):
                a, b = rings[i][k], rings[i][(k + 1) % segments]
                c, d = rings[i + 1][(k + 1) % segments], rings[i + 1][k]
                self.quad(a, b, c, d, mat, **kw)

    def sweep(self, profile, path, mat, closed=False, **kw):
        """Extrudes a 2D profile (right, up in cm) along a horizontal polyline (cornices, kerbs, parapets).
        The profile's right points to the outside of a counter-clockwise path."""
        n = len(path)
        frames = []
        for i in range(n):
            if closed:
                a, b = path[(i - 1) % n], path[(i + 1) % n]
            else:
                a, b = path[max(0, i - 1)], path[min(n - 1, i + 1)]
            t = norm((b[0] - a[0], b[1] - a[1], 0.0))
            right = (t[1], -t[0], 0.0)
            # Mitre: scale the offset at corners so the profile keeps its width.
            if 0 < i < n - 1 or closed:
                t0 = norm((path[i][0] - a[0], path[i][1] - a[1], 0.0))
                t1 = norm((b[0] - path[i][0], b[1] - path[i][1], 0.0))
                cosang = max(0.3, t0[0] * t1[0] + t0[1] * t1[1])
                miter = 1.0 / math.sqrt((1.0 + cosang) / 2.0)
            else:
                miter = 1.0
            frames.append([(path[i][0] + right[0] * r * miter, path[i][1] + right[1] * r * miter, path[i][2] + up) for r, up in profile])
        count = n if closed else n - 1
        for i in range(count):
            f0, f1 = frames[i], frames[(i + 1) % n]
            for k in range(len(profile) - 1):
                self.quad(f0[k], f1[k], f1[k + 1], f0[k + 1], mat, **kw)

    # -- output -------------------------------------------------------------

    def to_dynamic_mesh(self):
        dm = unreal.DynamicMesh()
        materials = []
        for mat_id, mat in enumerate(self.order):
            verts, normals, uvs, cols, tris = self.parts[mat]
            buf = unreal.GeometryScriptSimpleMeshBuffers()
            buf.vertices = [unreal.Vector(*v) for v in verts]
            buf.normals = [unreal.Vector(*n) for n in normals]
            buf.uv0 = [unreal.Vector2D(*u) for u in uvs]
            buf.vertex_colors = [unreal.LinearColor(*c) for c in cols]
            buf.triangles = [unreal.IntVector(*t) for t in tris]
            unreal.GeometryScript_MeshEdits.append_buffers_to_mesh(dm, buf, mat_id)
            materials.append(mat)
        return dm, materials


def save_mesh(builder_or_dm, path, materials=None, collision="complex", nanite=False, origin=(0.0, 0.0, 0.0)):
    """Writes a Static Mesh. collision: complex (per-triangle, exact), simple (boxes/convex), none.
    origin: world point that becomes the mesh pivot (the actor is placed there)."""
    if isinstance(builder_or_dm, MeshBuilder):
        dm, materials = builder_or_dm.to_dynamic_mesh()
    else:
        dm = builder_or_dm
    if origin != (0.0, 0.0, 0.0):
        unreal.GeometryScript_MeshTransforms.translate_mesh(dm, unreal.Vector(-origin[0], -origin[1], -origin[2]))
    ensure_dir(path.rsplit("/", 1)[0])
    if EAL.does_asset_exist(path):
        sm = EAL.load_asset(path)
        opts = unreal.GeometryScriptCopyMeshToAssetOptions()
        opts.set_editor_property("replace_materials", True)
        opts.set_editor_property("new_materials", materials)
        opts.set_editor_property("enable_recompute_normals", False)
        opts.set_editor_property("enable_recompute_tangents", True)
        lod = unreal.GeometryScriptMeshWriteLOD()
        unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(dm, sm, opts, lod)
    else:
        opts = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        opts.set_editor_property("enable_recompute_normals", False)
        opts.set_editor_property("enable_recompute_tangents", True)
        opts.set_editor_property("enable_collision", collision != "none")
        opts.set_editor_property("enable_nanite", nanite)
        result = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm, path, opts)
        sm = result[0] if isinstance(result, (tuple, list)) else result
        for i, m in enumerate(materials):
            sm.set_material(i, m)
    body = sm.get_editor_property("body_setup")
    if body:
        if collision == "complex":
            body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        elif collision == "simple":
            body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_DEFAULT)
            copts = unreal.GeometryScriptCollisionFromMeshOptions()
            copts.set_editor_property("method", unreal.GeometryScriptCollisionGenerationMethod.MIN_VOLUME_SHAPES)
            unreal.GeometryScript_Collision.set_static_mesh_collision_from_mesh(dm, sm, copts)
    ns = sm.get_editor_property("nanite_settings")
    ns.set_editor_property("enabled", nanite)
    sm.set_editor_property("nanite_settings", ns)
    EAL.save_loaded_asset(sm, only_if_is_dirty=False)
    return sm


# ---------------------------------------------------------------------------
# Terrain
# ---------------------------------------------------------------------------

def terrain_mesh(x0, x1, y0, y1, step, height, layers, mat, skirt=150.0):
    """Height field over a rectangle: height(x, y) -> z, layers(x, y) -> (r, g, b) weights
    of the terrain layers 1..3 over the base. Vertex colours carry the layers, UVs are metres."""
    nx = int(round((x1 - x0) / step))
    ny = int(round((y1 - y0) / step))
    verts, uvs, cols, heights = [], [], [], []
    for j in range(ny + 1):
        for i in range(nx + 1):
            x, y = x0 + i * step, y0 + j * step
            z = height(x, y)
            heights.append(z)
            verts.append((x, y, z))
            uvs.append((x / 100.0, y / 100.0))
            r, g, b = layers(x, y)
            cols.append((r, g, b, 1.0))
    normals = []
    for j in range(ny + 1):
        for i in range(nx + 1):
            hl = heights[j * (nx + 1) + max(0, i - 1)]
            hr = heights[j * (nx + 1) + min(nx, i + 1)]
            hd = heights[max(0, j - 1) * (nx + 1) + i]
            hu = heights[min(ny, j + 1) * (nx + 1) + i]
            normals.append(norm(((hl - hr) / (2 * step), (hd - hu) / (2 * step), 1.0)))
    tris = []
    for j in range(ny):
        for i in range(nx):
            a = j * (nx + 1) + i
            b, c, d = a + 1, a + nx + 2, a + nx + 1
            tris.append((a, c, b))
            tris.append((a, d, c))
    # A skirt down the outside edges, so the border never shows the sky under it.
    dm = unreal.DynamicMesh()
    buf = unreal.GeometryScriptSimpleMeshBuffers()
    buf.vertices = [unreal.Vector(*v) for v in verts]
    buf.normals = [unreal.Vector(*n) for n in normals]
    buf.uv0 = [unreal.Vector2D(*u) for u in uvs]
    buf.vertex_colors = [unreal.LinearColor(*c) for c in cols]
    buf.triangles = [unreal.IntVector(*t) for t in tris]
    unreal.GeometryScript_MeshEdits.append_buffers_to_mesh(dm, buf, 0)
    return dm, [mat], len(tris)


# ---------------------------------------------------------------------------
# Level
# ---------------------------------------------------------------------------

GAME_MODE = "/Game/Characters/BP_CSGameMode"


def new_level(path):
    """Opens (or creates) the level and clears it; the asset cannot be deleted while loaded."""
    if EAL.does_asset_exist(path):
        LEVELS.load_level(path)
        for actor in ACTORS.get_all_level_actors():
            if not isinstance(actor, (unreal.WorldSettings, unreal.Brush)) or isinstance(actor, unreal.Volume):
                ACTORS.destroy_actor(actor)
    else:
        LEVELS.new_level(path)
    world = unreal.EditorLevelLibrary.get_editor_world()
    gm = load(GAME_MODE)
    if world and gm:
        world.get_world_settings().set_editor_property("default_game_mode", gm.generated_class())
    log("level " + path)


def save_level():
    LEVELS.save_current_level()


def place(mesh, loc=(0, 0, 0), rot=(0, 0, 0), scl=(1, 1, 1), materials=None, folder="Geometry", label=None, shadow=True, collision=True):
    actor = ACTORS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*loc), unreal.Rotator(roll=rot[2], pitch=rot[0], yaw=rot[1]))
    comp = actor.static_mesh_component
    comp.set_static_mesh(mesh)
    actor.set_actor_scale3d(unreal.Vector(*scl))
    for i, m in enumerate(materials or []):
        if m:
            comp.set_material(i, m)
    comp.set_editor_property("cast_shadow", shadow)
    if not collision:
        comp.set_collision_profile_name("NoCollision")
    actor.set_folder_path(folder)
    if label:
        actor.set_actor_label(label)
    return actor


class Cluster:
    """Collects instances per mesh and writes one ACSPropCluster actor."""

    def __init__(self, label, folder="Detail"):
        self.label = label
        self.folder = folder
        self.groups = {}

    def add(self, mesh, loc, yaw=0.0, scl=1.0, pitch=0.0, roll=0.0, cull=0.0, collision=False, shadow=True, materials=None):
        if mesh is None:
            return
        key = (mesh.get_path_name(), cull, collision, shadow, tuple(m.get_path_name() if m else "" for m in (materials or [])))
        g = self.groups.setdefault(key, {"mesh": mesh, "materials": materials or [], "cull": cull, "collision": collision, "shadow": shadow, "xf": []})
        s = scl if isinstance(scl, (tuple, list)) else (scl, scl, scl)
        g["xf"].append(unreal.Transform(unreal.Vector(*loc), unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw), unreal.Vector(*s)))

    def count(self):
        return sum(len(g["xf"]) for g in self.groups.values())

    def build(self):
        actor = ACTORS.spawn_actor_from_class(unreal.CSPropCluster, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
        groups = []
        for g in self.groups.values():
            cg = unreal.CSClusterGroup()
            cg.set_editor_property("mesh", g["mesh"])
            cg.set_editor_property("materials", g["materials"])
            cg.set_editor_property("instances", g["xf"])
            cg.set_editor_property("cull_distance", float(g["cull"]))
            cg.set_editor_property("collision", g["collision"])
            cg.set_editor_property("cast_shadow", g["shadow"])
            groups.append(cg)
        actor.set_editor_property("groups", groups)
        actor.rebuild()
        actor.set_folder_path(self.folder)
        actor.set_actor_label(self.label)
        return actor


_decal_mis = {}


def decal_material(cell, tint=(1, 1, 1), opacity=1.0, roughness=0.8):
    key = (cell, tint, opacity, roughness)
    if key not in _decal_mis:
        name = "MI_Decal_%02d_%d" % (cell, len(_decal_mis))
        folder = GEN + "/Decals"
        ensure_dir(folder)
        path = folder + "/" + name
        mi = EAL.load_asset(path) if EAL.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        unreal.MaterialEditingLibrary.set_material_instance_parent(mi, load(V21 + "/Materials/M_CS_Decal"))
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi, "Cell", float(cell))
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi, "Opacity", float(opacity))
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi, "Roughness", float(roughness))
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(tint[0], tint[1], tint[2], 1))
        EAL.save_loaded_asset(mi, only_if_is_dirty=False)
        _decal_mis[key] = mi
    return _decal_mis[key]


# Decal atlas cells (Scripts/maps/make_textures.py)
CRACK, CRACK_WEB, OIL, OIL_SMALL, TYRES, RUST, SOOT, WET, DIRT, LINE, ARROW, PARK_CORNER, HAZARD, GRIME, MOSS, STENCIL = range(16)


def decal(cell, loc, size, yaw=0.0, pitch=-90.0, tint=(1, 1, 1), opacity=1.0, roughness=0.8, folder="Decals"):
    """size: (depth, width, height) in cm. pitch -90 projects straight down."""
    actor = ACTORS.spawn_actor_from_class(unreal.DecalActor, unreal.Vector(*loc), unreal.Rotator(roll=0, pitch=pitch, yaw=yaw))
    comp = actor.get_component_by_class(unreal.DecalComponent)
    comp.set_decal_material(decal_material(cell, tint, opacity, roughness))
    # DecalSize is the half extent of the projection box: callers give the full size.
    comp.set_editor_property("decal_size", unreal.Vector(size[0] / 2.0, size[1] / 2.0, size[2] / 2.0))
    comp.set_editor_property("fade_screen_size", 0.004)
    actor.set_folder_path(folder)
    return actor


def point_light(loc, intensity=3000.0, radius=1200.0, color=(1.0, 0.8, 0.6), shadows=False, folder="Lights"):
    actor = ACTORS.spawn_actor_from_class(unreal.PointLight, unreal.Vector(*loc), unreal.Rotator(0, 0, 0))
    comp = actor.get_component_by_class(unreal.PointLightComponent)
    comp.set_intensity(intensity)
    comp.set_attenuation_radius(radius)
    comp.set_light_color(unreal.LinearColor(color[0], color[1], color[2], 1.0))
    comp.set_cast_shadows(shadows)
    actor.set_folder_path(folder)
    return actor


def spot_light(loc, rot, intensity=6000.0, radius=1800.0, outer=55.0, color=(1.0, 0.9, 0.75), shadows=False, folder="Lights"):
    actor = ACTORS.spawn_actor_from_class(unreal.SpotLight, unreal.Vector(*loc), unreal.Rotator(roll=0, pitch=rot[0], yaw=rot[1]))
    comp = actor.get_component_by_class(unreal.SpotLightComponent)
    comp.set_intensity(intensity)
    comp.set_attenuation_radius(radius)
    comp.set_outer_cone_angle(outer)
    comp.set_inner_cone_angle(outer * 0.6)
    comp.set_light_color(unreal.LinearColor(color[0], color[1], color[2], 1.0))
    comp.set_cast_shadows(shadows)
    actor.set_folder_path(folder)
    return actor


def write_views(name, views):
    """views: list of (label, (x, y, z), pitch, yaw). Read by -cstestviews=<name>."""
    path = os.path.join(PROJECT, "Saved", "CSTest", "views_%s.txt" % name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write("# label x y z pitch yaw - written by Scripts/maps\n")
        for label, (x, y, z), pitch, yaw in views:
            f.write("%s %.0f %.0f %.0f %.1f %.1f\n" % (label, x, y, z, pitch, yaw))
    log("views: %d -> %s" % (len(views), path))


def rng(seed):
    return random.Random(seed)


# ---------------------------------------------------------------------------
# Gameplay actors (same rules as the v2.0 maps: tags Alpha / Bravo, free starts)
# ---------------------------------------------------------------------------

_starts = {}


def start(team, x, y, yaw, z=0.0):
    """Player start standing on z (the capsule centre goes 100 cm above)."""
    actor = ACTORS.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(x, y, z + 100.0), unreal.Rotator(0, 0, yaw))
    if team:
        actor.set_editor_property("player_start_tag", unreal.Name(team))
    n = _starts.get(team, 0)
    _starts[team] = n + 1
    actor.set_actor_label("Start_%s_%02d" % (team or "FFA", n))
    actor.set_folder_path("Spawns/" + (team or "FFA"))
    return actor


_machines = [0]


def machine(x, y, yaw, z=0.0):
    """Ammo machine; used from 80 cm in front (+X of the actor after yaw)."""
    actor = ACTORS.spawn_actor_from_class(unreal.CSAmmoMachine, unreal.Vector(x, y, z), unreal.Rotator(0, 0, yaw))
    actor.set_actor_label("AmmoMachine_%d" % _machines[0])
    actor.set_folder_path("AmmoMachines")
    _machines[0] += 1
    return actor


REVERBS = {
    "RE_Hall": dict(density=1.0, diffusion=1.0, gain=0.32, gain_hf=0.7, decay_time=2.4, decay_hf_ratio=0.6,
                    reflections_gain=0.3, reflections_delay=0.02, late_gain=1.2, late_delay=0.03),
    "RE_Room": dict(density=0.9, diffusion=0.9, gain=0.3, gain_hf=0.8, decay_time=0.7, decay_hf_ratio=0.8,
                    reflections_gain=0.5, reflections_delay=0.008, late_gain=0.9, late_delay=0.012),
    "RE_Alley": dict(density=0.7, diffusion=0.6, gain=0.26, gain_hf=0.75, decay_time=1.1, decay_hf_ratio=0.7,
                     reflections_gain=0.45, reflections_delay=0.012, late_gain=0.8, late_delay=0.02),
}


def reverb(x0, x1, y0, y1, z0, z1, name, volume=0.6):
    path = "/Game/Audio/Reverb/" + name
    effect = EAL.load_asset(path) if EAL.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, "/Game/Audio/Reverb", unreal.ReverbEffect, unreal.ReverbEffectFactory())
    for k, v in REVERBS[name].items():
        effect.set_editor_property(k, v)
    EAL.save_loaded_asset(effect, only_if_is_dirty=False)
    vol = ACTORS.spawn_actor_from_class(unreal.AudioVolume, unreal.Vector((x0 + x1) / 2.0, (y0 + y1) / 2.0, (z0 + z1) / 2.0), unreal.Rotator(0, 0, 0))
    vol.set_actor_scale3d(unreal.Vector(abs(x1 - x0) / 200.0, abs(y1 - y0) / 200.0, abs(z1 - z0) / 200.0))
    settings = vol.get_editor_property("settings")
    settings.set_editor_property("apply_reverb", True)
    settings.set_editor_property("reverb_effect", effect)
    settings.set_editor_property("volume", volume)
    settings.set_editor_property("fade_time", 0.4)
    vol.set_editor_property("settings", settings)
    vol.set_actor_label("Reverb_" + name)
    vol.set_folder_path("Audio")
    return vol


def navmesh(x0, x1, y0, y1, z0, z1):
    vol = ACTORS.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), unreal.Rotator(0, 0, 0))
    vol.set_actor_scale3d(unreal.Vector((x1 - x0) / 200.0, (y1 - y0) / 200.0, (z1 - z0) / 200.0))
    vol.set_folder_path("Environment")


def blocking_box(x0, x1, y0, y1, z0, z1, label="Blocker"):
    """Invisible player blocker (map edges, roofs that are not playable)."""
    vol = ACTORS.spawn_actor_from_class(unreal.BlockingVolume, unreal.Vector((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), unreal.Rotator(0, 0, 0))
    vol.set_actor_scale3d(unreal.Vector(abs(x1 - x0) / 200.0, abs(y1 - y0) / 200.0, abs(z1 - z0) / 200.0))
    vol.set_actor_label(label)
    vol.set_folder_path("Blockers")
    return vol


def environment(sun_pitch, sun_yaw, sun_lux, sun_color=(1.0, 0.95, 0.85), sky_intensity=1.0, sky_tint=(1.0, 1.0, 1.0),
                fog_density=0.01, fog_falloff=0.2, fog_color=None, fog_start=0.0, clouds=True, cloud_coverage=None,
                exposure=(0.6, 2.0, 0.0), contrast=1.0, saturation=1.0, temperature=6500.0, bloom=0.4, vignette=0.3,
                ao_intensity=0.6):
    """Per-map light and atmosphere. exposure: (min, max, bias) in legacy luminance units and stops."""
    sun = ACTORS.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 3000), unreal.Rotator(roll=0, pitch=sun_pitch, yaw=sun_yaw))
    comp = sun.get_editor_property("directional_light_component")
    comp.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    comp.set_editor_property("intensity", sun_lux)
    comp.set_editor_property("atmosphere_sun_light", True)
    comp.set_editor_property("contact_shadow_length", 0.03)
    comp.set_light_color(unreal.LinearColor(*sun_color, 1.0))
    sun.set_folder_path("Environment")

    sky = ACTORS.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 2800), unreal.Rotator(0, 0, 0))
    sky_comp = sky.get_editor_property("light_component")
    sky_comp.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky_comp.set_editor_property("real_time_capture", True)
    sky_comp.set_editor_property("intensity", sky_intensity)
    sky_comp.set_light_color(unreal.LinearColor(*sky_tint, 1.0))
    sky.set_folder_path("Environment")

    ACTORS.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0)).set_folder_path("Environment")

    fog = ACTORS.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, -200), unreal.Rotator(0, 0, 0))
    fc = fog.get_editor_property("component")
    fc.set_editor_property("fog_density", fog_density)
    fc.set_editor_property("fog_height_falloff", fog_falloff)
    fc.set_editor_property("start_distance", fog_start)
    if fog_color:
        fc.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(*fog_color, 1.0))
    fog.set_folder_path("Environment")

    if clouds:
        c = ACTORS.spawn_actor_from_class(unreal.VolumetricCloud, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
        c.set_folder_path("Environment")

    post = ACTORS.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    post.set_editor_property("unbound", True)
    s = post.get_editor_property("settings")
    for k, v in (("auto_exposure_min_brightness", exposure[0]), ("auto_exposure_max_brightness", exposure[1]),
                 ("auto_exposure_bias", exposure[2]), ("bloom_intensity", bloom), ("vignette_intensity", vignette),
                 ("white_temp", temperature), ("ambient_occlusion_intensity", ao_intensity)):
        s.set_editor_property("override_" + k, True)
        s.set_editor_property(k, v)
    s.set_editor_property("override_color_contrast", True)
    s.set_editor_property("color_contrast", unreal.Vector4(contrast, contrast, contrast, 1.0))
    s.set_editor_property("override_color_saturation", True)
    s.set_editor_property("color_saturation", unreal.Vector4(saturation, saturation, saturation, 1.0))
    post.set_editor_property("settings", s)
    post.set_folder_path("Environment")
    return sun
