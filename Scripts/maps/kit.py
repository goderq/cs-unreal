"""
v2.1 map rework: the modular kit (docs/MAPS_REWORK.md 2.1).

Two kinds of pieces:
  * instanced meshes saved once under /Game/Environment/V21/Kit (pallet,
    containers, sleepers, bollards, dock bumpers, windows, doors, railing,
    trailer, lamp pole, trees, sign panel) - placed with lib.Cluster, recoloured
    per use through material overrides;
  * generators that add geometry to a map's MeshBuilder (stairs, ladders,
    steel frames, walls with openings, cornices, roofs) - merged into one mesh
    per building, so a building costs a handful of draw calls.

Real sizes throughout: steps 17.5 x 28 cm, railings 105 cm, doors 90-100 x
210 cm, containers 606 / 1219 x 244 x 259 cm, trailer 1360 x 255 x 400 cm.
"""

import math

import unreal

import lib
from lib import MeshBuilder, surface, save_mesh, add, sub, scale, norm, cross

KIT = lib.V21 + "/Kit"
NOGRIME = lambda x, y: -1.0e6  # noqa: E731 - kit pieces carry no ground grime


def M(name):
    return surface(name)


# ---------------------------------------------------------------------------
# Derived material instances (tints, glass, signs, foliage)
# ---------------------------------------------------------------------------

def tinted(base, name, tint, rough=None, dirt=None, desat=None, normal=None):
    """A copy of surface variant `base` with another tint (same textures)."""
    path = lib.V21 + "/Surfaces/MI_" + name
    mel = unreal.MaterialEditingLibrary
    src = surface(base)
    mi = lib.EAL.load_asset(path) if lib.EAL.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "MI_" + name, lib.V21 + "/Surfaces", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, src.get_editor_property("parent"))
    for p in ("Diffuse", "Normal", "ARM", "MacroNoise"):
        t = mel.get_material_instance_texture_parameter_value(src, p)
        if t:
            mel.set_material_instance_texture_parameter_value(mi, p, t)
    for p in ("TileSize", "RoughnessScale", "NormalStrength", "DirtAmount", "MacroVariation"):
        mel.set_material_instance_scalar_parameter_value(mi, p, mel.get_material_instance_scalar_parameter_value(src, p))
    mel.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(tint[0], tint[1], tint[2], 1))
    if rough is not None:
        mel.set_material_instance_scalar_parameter_value(mi, "RoughnessScale", rough)
    if dirt is not None:
        mel.set_material_instance_scalar_parameter_value(mi, "DirtAmount", dirt)
    mel.set_material_instance_scalar_parameter_value(mi, "Desaturation", 0.0 if desat is None else desat)
    if normal is not None:
        mel.set_material_instance_scalar_parameter_value(mi, "NormalStrength", normal)
    mi.set_editor_property("phys_material", src.get_editor_property("phys_material"))
    lib.EAL.save_loaded_asset(mi, only_if_is_dirty=False)
    lib._cache[path] = mi
    return mi


PAINT_BASE_LUMINANCE = 0.25   # white_plaster_02, linear (measured)


def paint(name, albedo, rough=0.55, dirt=0.35):
    """Painted surface of a given linear albedo: the smooth white plaster, desaturated,
    tinted, its normal map nearly flat - steelwork, doors, railings, rubber."""
    t = tuple(c / PAINT_BASE_LUMINANCE for c in albedo)
    return tinted("Plaster_White", name, t, rough=rough / 0.9, dirt=dirt, desat=1.0, normal=0.25)


def material_instance(parent_path, name, scalars=None, vectors=None, textures=None):
    path = lib.V21 + "/Surfaces/MI_" + name
    mel = unreal.MaterialEditingLibrary
    mi = lib.EAL.load_asset(path) if lib.EAL.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "MI_" + name, lib.V21 + "/Surfaces", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, lib.load(parent_path))
    for k, v in (scalars or {}).items():
        mel.set_material_instance_scalar_parameter_value(mi, k, float(v))
    for k, v in (vectors or {}).items():
        mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(v[0], v[1], v[2], 1))
    for k, v in (textures or {}).items():
        mel.set_material_instance_texture_parameter_value(mi, k, v)
    lib.EAL.save_loaded_asset(mi, only_if_is_dirty=False)
    lib._cache[path] = mi
    return mi


def glass(name, tint=(0.05, 0.07, 0.08), glow=0.0, glow_color=(1.0, 0.72, 0.4)):
    return material_instance(lib.V21 + "/Materials/M_CS_Glass", "Glass_" + name, {"Glow": glow}, {"Tint": tint, "GlowColor": glow_color})


def sign_material(cell, glow=0.0):
    return material_instance(lib.V21 + "/Materials/M_CS_Atlas", "Sign_%02d" % cell, {"Cell": cell, "Glow": glow})


def leaves():
    return surface("LeafCluster")


# Sign cells (T_Signs, Scripts/maps/make_textures.py)
SIGN = {"caffe": 0, "farmacia": 1, "panificio": 2, "tabacchi": 3, "alimentari": 4, "bar": 5, "via": 6, "piazza": 7,
        "depot": 8, "bays": 9, "nosmoking": 10, "voltage": 11, "nordlog": 12, "dock": 13, "office": 14, "personnel": 15}


# ---------------------------------------------------------------------------
# Geometry helpers
# ---------------------------------------------------------------------------

def cyl(b, p0, p1, r, mat, seg=12, caps=True, r1=None):
    """Cylinder (or cone with r1) between two 3D points."""
    axis = norm(sub(p1, p0))
    up = (0.0, 0.0, 1.0) if abs(axis[2]) < 0.95 else (1.0, 0.0, 0.0)
    u = norm(cross(axis, up))
    v = norm(cross(u, axis))
    r1 = r if r1 is None else r1
    ring0 = [add(p0, add(scale(u, r * math.cos(math.tau * k / seg)), scale(v, r * math.sin(math.tau * k / seg)))) for k in range(seg)]
    ring1 = [add(p1, add(scale(u, r1 * math.cos(math.tau * k / seg)), scale(v, r1 * math.sin(math.tau * k / seg)))) for k in range(seg)]
    for k in range(seg):
        a, bb = ring0[k], ring0[(k + 1) % seg]
        c, d = ring1[(k + 1) % seg], ring1[k]
        mid = norm(add(add(scale(u, math.cos(math.tau * (k + 0.5) / seg)), scale(v, math.sin(math.tau * (k + 0.5) / seg))), (0, 0, 0)))
        b.quad(a, bb, c, d, mat, normal=mid)
    if caps:
        # (u, v) turn about -axis: ring0 as is faces -axis, ring1 reversed faces +axis.
        b.poly(ring0, mat, normal=scale(axis, -1))
        b.poly(list(reversed(ring1)), mat, normal=axis)


def beam(b, p0, p1, w, h, mat):
    """Rectangular beam between two points (diagonal braces, rails, rafters)."""
    d = sub(p1, p0)
    length = math.sqrt(d[0] ** 2 + d[1] ** 2 + d[2] ** 2)
    yaw = math.degrees(math.atan2(d[1], d[0]))
    pitch = math.degrees(math.atan2(d[2], math.hypot(d[0], d[1])))
    b.obox(lib.scale(add(p0, p1), 0.5), (length, w, h), yaw, mat, pitch=pitch)


def stair_flight(b, x, y, z0, z1, width, yaw, step_mat, side_mat=None, solid=True, rise=17.5, tread=28.0):
    """A straight flight rising from (x, y, z0) along yaw (degrees, 0 = +X) to z1.
    solid: concrete steps down to z0; else steel treads between two stringers.
    Returns the (x, y) at the top edge."""
    n = max(1, int(round((z1 - z0) / rise)))
    rise = (z1 - z0) / n
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))

    def at(along, across):
        return (x + along * c - across * s, y + along * s + across * c)

    for i in range(n):
        a0, a1 = i * tread, (i + 1) * tread
        zt = z0 + (i + 1) * rise
        zb = z0 if solid else zt - 5
        center = at((a0 + a1) / 2, 0)
        b.obox((center[0], center[1], (zb + zt) / 2), (tread + (2 if solid else 0), width, zt - zb), yaw, step_mat)
    if not solid or side_mat:
        run = n * tread
        pitch = math.degrees(math.atan2(z1 - z0, run))
        length = math.hypot(run, z1 - z0)
        for side in (-1, 1):
            mid = at(run / 2, side * (width / 2 + 3))
            b.obox((mid[0], mid[1], (z0 + z1) / 2 - 12), (length + 30, 6, 28), yaw, side_mat or step_mat, pitch=pitch)
    return at(n * tread, 0)


def railing_run(cluster, p0, p1, mesh=None, height_offset=0.0, material=None, collision=True):
    """Railing along a straight line (may slope): 200 cm instanced segments."""
    mesh = mesh or railing_segment()
    d = sub(p1, p0)
    horiz = math.hypot(d[0], d[1])
    length = math.sqrt(horiz ** 2 + d[2] ** 2)
    if length < 20:
        return
    n = max(1, int(math.ceil(length / 200.0)))
    yaw = math.degrees(math.atan2(d[1], d[0]))
    pitch = math.degrees(math.atan2(d[2], horiz))
    for i in range(n):
        p = lib.lerp3(p0, p1, i / n)
        cluster.add(mesh, (p[0], p[1], p[2] + height_offset), yaw, (length / n / 200.0, 1.0, 1.0), pitch=pitch, cull=6000,
                    collision=collision, materials=[material] if material else None)


def ladder(b, x, y, z0, z1, yaw, mat, width=45.0):
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    for side in (-1, 1):
        px, py = x - s * side * width / 2, y + c * side * width / 2
        b.box(px - 3, px + 3, py - 3, py + 3, z0, z1, mat)
    z = z0 + 30
    while z < z1 - 10:
        cyl(b, (x - s * width / 2, y + c * width / 2, z), (x + s * width / 2, y - c * width / 2, z), 1.6, mat, seg=6, caps=False)
        z += 30


def wall_openings(b, a, c, z0, z1, t, mat, openings, reveal_mat=None, outside=-1):
    """Straight axis-aligned wall from a to c, thickness t, with openings (s, e, bottom, top)
    along it. outside: side of the wall that faces out (-1 / +1 across the wall line)."""
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

    # The wall is cut into vertical strips at every opening edge; in each strip the openings
    # covering it are merged in z and the wall fills only the gaps between them, clamped to
    # this band. (Openings stacked on several floors at one span, and a wall built as plinth +
    # cladding that passes every opening to both calls, both come out right.)
    edges = sorted({lo, hi} | {min(max(v, lo), hi) for s, e, _, _ in openings for v in (s, e)})
    for a0, a1 in zip(edges, edges[1:]):
        if a1 - a0 < 1:
            continue
        mid = (a0 + a1) / 2
        spans = sorted((max(zb, z0), min(zt, z1)) for s, e, zb, zt in openings if s <= mid <= e and min(zt, z1) > max(zb, z0))
        z = z0
        for zb, zt in spans:
            piece(a0, a1, z, zb)
            z = max(z, zt)
        piece(a0, a1, z, z1)


# ---------------------------------------------------------------------------
# Instanced kit meshes
# ---------------------------------------------------------------------------

_made = {}


def _once(name, build):
    if name not in _made:
        _made[name] = build()
    return _made[name]


def pallet():
    def build():
        b = MeshBuilder(ground=NOGRIME)
        w = M("Wood_Planks")
        for i in range(5):
            y = -33 + i * 16.5
            b.box(-60, 60, y - 7, y + 7, 12.2, 14.4, w)
        for y in (-34, 0, 34):
            for x in (-54, 0, 54):
                b.box(x - 7, x + 7, y - 5, y + 5, 2.2, 12.2, w)
            b.box(-60, 60, y - 6, y + 6, 0, 2.2, w)
        return save_mesh(b, KIT + "/SM_Kit_Pallet", collision="simple")
    return _once("pallet", build)


def crate(w=120.0, d=100.0, h=100.0):
    """Wooden shipping crate on the floor (origin bottom centre): plank body, dark frame on the
    edges, box collision exactly its size - stacks by its height and blocks what it looks like."""
    name = "SM_Kit_Crate_%dx%dx%d" % (w, d, h)

    def build():
        b = MeshBuilder(ground=NOGRIME)
        body, frame = M("Wood_Planks"), M("Wood_Dark")
        f, o = 9.0, 1.5
        b.box(-w / 2, w / 2, -d / 2, d / 2, 0, h, body)
        for sx in (-1, 1):
            for sy in (-1, 1):
                x, y = sx * (w / 2 - f / 2), sy * (d / 2 - f / 2)
                b.box(x - f / 2 - o, x + f / 2 + o, y - f / 2 - o, y + f / 2 + o, 0, h, frame)
        for z0 in (0.0, h - f):
            b.box(-w / 2 - o, w / 2 + o, -d / 2 - o, -d / 2 + f, z0, z0 + f, frame)
            b.box(-w / 2 - o, w / 2 + o, d / 2 - f, d / 2 + o, z0, z0 + f, frame)
            b.box(-w / 2 - o, -w / 2 + f, -d / 2 - o, d / 2 + o, z0, z0 + f, frame)
            b.box(w / 2 - f, w / 2 + o, -d / 2 - o, d / 2 + o, z0, z0 + f, frame)
        return save_mesh(b, KIT + "/" + name, collision="simple")
    return _once(name, build)


def container(length=606.0):
    """ISO container: corrugated sides and front, doors with locking bars at +X.
    Slot 0 body (tint per use), slot 1 frame, slot 2 bars/hinges."""
    name = "SM_Kit_Container%d" % (20 if length < 1000 else 40)

    def build():
        b = MeshBuilder(ground=NOGRIME)
        body, frame, bars = M("Metal_Container_Grey"), M("Metal_Container_Grey"), paint("Paint_Iron", (0.02, 0.02, 0.022), rough=0.6)
        L, W, H = length, 244.0, 259.0
        x0, x1 = -L / 2, L / 2
        # Corrugated long sides: trapezoid ribs along X.
        pitch_ = 27.0
        for side in (-1, 1):
            y = side * W / 2
            x = x0 + 12
            while x < x1 - 12:
                xa, xb, xc, xd = x, x + 6, x + 13.5, x + 19.5
                xe = min(x + pitch_, x1 - 12)
                out = side * 3.0
                segs = [(xa, 0.0), (xb, out), (xc, out), (xd, 0.0), (xe, 0.0)]
                for (sa, oa), (sb, ob) in zip(segs, segs[1:]):
                    if sb <= sa:
                        continue
                    p = [(sa, y + oa, 12), (sb, y + ob, 12), (sb, y + ob, H - 12), (sa, y + oa, H - 12)]
                    b.quad(*(p if side < 0 else [p[1], p[0], p[3], p[2]]), body)
                x += pitch_
        # Front end (-X): vertical corrugation.
        y = -W / 2 + 12
        while y < W / 2 - 12:
            ya, yb, yc, yd, ye = y, y + 6, y + 13.5, y + 19.5, min(y + pitch_, W / 2 - 12)
            segs = [(ya, 0.0), (yb, -3.0), (yc, -3.0), (yd, 0.0), (ye, 0.0)]
            for (sa, oa), (sb, ob) in zip(segs, segs[1:]):
                if sb > sa:
                    b.quad((x0 + oa, sb, 12), (x0 + ob, sa, 12), (x0 + ob, sa, H - 12), (x0 + oa, sb, H - 12), body)
            y += pitch_
        # Doors (+X): two leaves, vertical locking bars, hinges.
        b.box(x1 - 4, x1, -W / 2 + 12, W / 2 - 12, 12, H - 12, body, faces="e")
        for yy in (-W / 2 + 40, -W / 2 + 90, W / 2 - 90, W / 2 - 40):
            cyl(b, (x1 + 4, yy, 20), (x1 + 4, yy, H - 20), 2.2, bars, seg=6, caps=False)
            b.box(x1, x1 + 6, yy - 6, yy + 6, 110, 124, bars)
        b.box(x1, x1 + 2, -1, 1, 12, H - 12, frame)
        # Frame: corner posts, top and bottom rails, roof.
        for sx in (x0, x1 - 16):
            for sy in (-W / 2, W / 2 - 16):
                b.box(sx, sx + 16, sy, sy + 16, 0, H, frame)
        for sy in (-W / 2, W / 2 - 16):
            b.box(x0, x1, sy, sy + 16, 0, 12, frame)
            b.box(x0, x1, sy, sy + 16, H - 12, H, frame)
        b.box(x0, x0 + 16, -W / 2, W / 2, 0, 12, frame)
        b.box(x0, x0 + 16, -W / 2, W / 2, H - 12, H, frame)
        b.box(x1 - 16, x1, -W / 2, W / 2, 0, 12, frame)
        b.box(x1 - 16, x1, -W / 2, W / 2, H - 12, H, frame)
        b.box(x0 + 16, x1 - 16, -W / 2 + 16, W / 2 - 16, H - 14, H - 12, body, faces="t")
        return save_mesh(b, KIT + "/" + name, collision="simple")
    return _once(name, build)


def sleeper():
    def build():
        b = MeshBuilder(ground=NOGRIME)
        b.box(-130, 130, -12, 12, 0, 16, M("Concrete_Wall"))
        return save_mesh(b, KIT + "/SM_Kit_Sleeper", collision="none")
    return _once("sleeper", build)


def bollard():
    def build():
        b = MeshBuilder(ground=NOGRIME)
        yellow = paint("Paint_SafetyYellow", (0.55, 0.38, 0.03), dirt=0.5)
        cyl(b, (0, 0, 0), (0, 0, 100), 10, yellow, seg=14)
        cyl(b, (0, 0, 70), (0, 0, 80), 10.5, M("Metal_Rusty"), seg=14, caps=False)
        return save_mesh(b, KIT + "/SM_Kit_Bollard", collision="simple")
    return _once("bollard", build)


def dock_bumper():
    def build():
        b = MeshBuilder(ground=NOGRIME)
        rubber = paint("Paint_Rubber", (0.025, 0.025, 0.025), rough=0.95, dirt=0.2)
        b.box(-30, 30, -14, 0, 0, 45, rubber)
        for z in (8, 20, 32):
            b.box(-30, 30, -16, -14, z, z + 6, rubber)
        return save_mesh(b, KIT + "/SM_Kit_DockBumper", collision="simple")
    return _once("bumper", build)


def window(w=120.0, h=150.0, style="plaster"):
    """Window unit, origin at the bottom centre of the opening on the outer wall face,
    facing -Y. Frame, cross mullion, recessed glass, protruding sill (plaster style),
    or a slim steel frame with a horizontal bar (industrial style).
    Slots: 0 frame, 1 glass, 2 sill/trim."""
    name = "SM_Kit_Window_%s_%dx%d" % (style, w, h)

    def build():
        b = MeshBuilder(ground=NOGRIME)
        frame = M("Plaster_White") if style == "plaster" else paint("Paint_FrameGrey", (0.1, 0.11, 0.12))
        gl = glass("Dark")
        sill = M("Stone_Blocks") if style == "plaster" else paint("Paint_FrameGrey", (0.1, 0.11, 0.12))
        f = 7.0 if style == "plaster" else 5.0
        depth = 14.0
        # Frame ring, set back 8 cm in the opening.
        y0, y1 = 8.0, 8.0 + depth
        b.box(-w / 2, -w / 2 + f, y0, y1, 0, h, frame)
        b.box(w / 2 - f, w / 2, y0, y1, 0, h, frame)
        b.box(-w / 2, w / 2, y0, y1, 0, f, frame)
        b.box(-w / 2, w / 2, y0, y1, h - f, h, frame)
        if style == "plaster":
            b.box(-2.5, 2.5, y0 + 2, y1 - 2, f, h - f, frame)
            b.box(-w / 2 + f, w / 2 - f, y0 + 2, y1 - 2, h * 0.62, h * 0.62 + 5, frame)
            b.box(-w / 2 - 6, w / 2 + 6, -6, 10, -6, 0, sill)          # sill
            b.box(-w / 2 - 4, w / 2 + 4, -3, 8, h, h + 10, sill)        # lintel band
        else:
            b.box(-w / 2 + f, w / 2 - f, y0 + 2, y1 - 2, h * 0.5 - 2, h * 0.5 + 2, frame)
        b.quad((-w / 2 + f, y0 + 6, f), (w / 2 - f, y0 + 6, f), (w / 2 - f, y0 + 6, h - f), (-w / 2 + f, y0 + 6, h - f), gl,
               uvs=[(0, 1), (1, 1), (1, 0), (0, 0)])
        return save_mesh(b, KIT + "/" + name, collision="none")
    return _once(name, build)


def shutters(w=120.0, h=150.0):
    """A pair of louvred wooden shutters opened flat against the wall, beside a window."""
    name = "SM_Kit_Shutters_%dx%d" % (w, h)

    def build():
        b = MeshBuilder(ground=NOGRIME)
        wood = M("Wood_Planks")
        half = w / 2
        for side in (-1, 1):
            xa, xb = (side * (half + 2), side * (half + 2 + half * 0.95))
            xa, xb = min(xa, xb), max(xa, xb)
            b.box(xa, xb, -5, -1, 0, h, wood)
            z = 8
            while z < h - 8:
                b.box(xa + 4, xb - 4, -7, -5, z, z + 4, wood)
                z += 9
        return save_mesh(b, KIT + "/" + name, collision="none")
    return _once(name, build)


def door(w=100.0, h=215.0, style="wood", open=True):
    """Door unit like the window: frame, leaf with panels, handle. Slots: 0 frame, 1 leaf, 2 metal.
    open: the leaf stands swung 95 degrees into the room (+Y) on its hinge - the doors of the
    kit sit in walkable openings, and a closed-looking leaf you walk through reads as a bug."""
    name = "SM_Kit_Door_%s_%dx%d%s" % (style, w, h, "_open" if open else "")

    def build():
        b = MeshBuilder(ground=NOGRIME)
        frame = M("Plaster_White") if style == "wood" else paint("Paint_FrameGrey", (0.1, 0.11, 0.12))
        leaf = M("Wood_Dark") if style == "wood" else paint("Paint_DoorGrey", (0.16, 0.19, 0.21))
        metal = M("Metal_Rusty")
        f = 8.0
        b.box(-w / 2, -w / 2 + f, 6, 18, 0, h, frame)
        b.box(w / 2 - f, w / 2, 6, 18, 0, h, frame)
        b.box(-w / 2, w / 2, 6, 18, h - f, h, frame)
        if not open:
            b.box(-w / 2 + f, w / 2 - f, 12, 16, 0, h - f, leaf)
            for z0, z1 in ((20, 95), (110, h - 25)):
                for xa, xb in ((-w / 2 + 16, -3), (3, w / 2 - 16)):
                    b.box(xa, xb, 10, 12, z0, z1, leaf)
            b.box(w / 2 - 24, w / 2 - 12, 6, 10, 100, 104, metal)
            return save_mesh(b, KIT + "/" + name, collision="none")
        # Open leaf: hinged at the left jamb, swung into the room.
        lw = w - 2 * f
        ang = math.radians(95.0)
        d = (math.cos(ang), math.sin(ang))
        hx, hy = -w / 2 + f, 16.0

        def at(t, off=0.0):
            return (hx + d[0] * t - d[1] * off, hy + d[1] * t + d[0] * off)
        yaw = math.degrees(ang)
        c = at(lw / 2)
        b.obox((c[0], c[1], (h - f) / 2 + 1), (lw, 4, h - f - 2), yaw, leaf)
        for z0, z1 in ((20, 95), (110, h - 25)):
            for t0, t1 in ((8, lw / 2 - 3), (lw / 2 + 3, lw - 8)):
                for side in (-1, 1):
                    p = at((t0 + t1) / 2, side * 2.5)
                    b.obox((p[0], p[1], (z0 + z1) / 2), (t1 - t0, 1.5, z1 - z0), yaw, leaf)
        for side in (-1, 1):
            p = at(lw - 10, side * 5)
            b.obox((p[0], p[1], 102), (12, 3, 4), yaw, metal)
        return save_mesh(b, KIT + "/" + name, collision="none")
    return _once(name, build)


def railing_segment(style="steel"):
    """200 cm railing along +X from the origin: posts, top rail at 105 cm, mid rail, toe board."""
    name = "SM_Kit_Railing_%s" % style

    def build():
        b = MeshBuilder(ground=NOGRIME)
        if style == "steel":
            mat = paint("Paint_SafetyYellow", (0.55, 0.38, 0.03), dirt=0.5)
            for x in (0.0, 200.0):
                b.box(x - 2.5, x + 2.5, -2.5, 2.5, 0, 105, mat)
            cyl(b, (0, 0, 105), (200, 0, 105), 2.6, mat, seg=8, caps=False)
            cyl(b, (0, 0, 55), (200, 0, 55), 2.0, mat, seg=8, caps=False)
            b.box(0, 200, -1, 1, 0, 12, mat)
        else:  # wrought iron balcony railing
            mat = paint("Paint_Iron", (0.02, 0.02, 0.022), rough=0.6)
            b.box(0, 200, -2, 2, 95, 100, mat)
            b.box(0, 200, -1.5, 1.5, 8, 12, mat)
            x = 0.0
            while x <= 200.0:
                b.box(x - 1, x + 1, -1, 1, 8, 95, mat)
                x += 12.0
        return save_mesh(b, KIT + "/" + name, collision="simple")
    return _once(name, build)


def trailer():
    """Box semi-trailer (no tractor): body, chassis, 3 axles, landing legs, lights. Slot 0 body (tint)."""
    def build():
        b = MeshBuilder(ground=NOGRIME)
        body = M("Metal_Container_Grey")
        chassis = M("Metal_Rusty")
        rubber = paint("Paint_Rubber", (0.025, 0.025, 0.025), rough=0.95, dirt=0.2)
        L, W = 1360.0, 255.0
        b.box(-L / 2, L / 2, -W / 2, W / 2, 125, 400, body)
        for x in range(int(-L / 2) + 60, int(L / 2), 120):
            b.box(x, x + 5, -W / 2 - 2, W / 2 + 2, 130, 395, body, faces="nsw")
        b.box(-L / 2, L / 2, -45, 45, 95, 125, chassis)
        b.box(-L / 2 + 20, L / 2 - 20, -W / 2, W / 2, 110, 125, chassis)
        for ax in (L / 2 - 180, L / 2 - 310, L / 2 - 440):
            for side in (-1, 1):
                for off in (0.0, 32.0):
                    y = side * (W / 2 - 22 - off)
                    cyl(b, (ax, y - 14, 52), (ax, y + 14, 52), 50, rubber, seg=16)
                    cyl(b, (ax, y - 15, 52), (ax, y + 15, 52), 26, chassis, seg=10)
            b.box(ax - 6, ax + 6, -W / 2 + 30, W / 2 - 30, 45, 60, chassis)
        for side in (-1, 1):
            b.box(-L / 2 + 330, -L / 2 + 342, side * 70 - 6, side * 70 + 6, 0, 110, chassis)
            b.box(-L / 2 + 320, -L / 2 + 352, side * 70 - 14, side * 70 + 14, 0, 6, chassis)
        red = paint("Paint_LightRed", (0.6, 0.01, 0.01), rough=0.3, dirt=0.0)
        for side in (-1, 1):
            b.box(L / 2, L / 2 + 3, side * 100 - 12, side * 100 + 12, 105, 118, red)
        b.box(L / 2 - 30, L / 2, -W / 2, W / 2, 60, 75, chassis)  # underride bar
        return save_mesh(b, KIT + "/SM_Kit_Trailer", collision="simple")
    return _once("trailer", build)


def lamp_pole(height=800.0):
    """Tall steel pole with an arm; the lamp head (security_light) is placed at the arm end."""
    name = "SM_Kit_LampPole_%d" % height

    def build():
        b = MeshBuilder(ground=NOGRIME)
        steel = paint("Paint_Galvanised", (0.14, 0.145, 0.15), rough=0.45)
        cyl(b, (0, 0, 0), (0, 0, height), 11, steel, seg=12, r1=6)
        b.box(-18, 18, -18, 18, 0, 8, steel)
        beam(b, (0, 0, height - 20), (120, 0, height - 5), 6, 8, steel)
        return save_mesh(b, KIT + "/" + name, collision="simple")
    return _once(name, build)


def sign_panel():
    """1 m x 25 cm panel facing -Y, origin bottom centre; M_CS_Atlas picks the sign."""
    def build():
        b = MeshBuilder(ground=NOGRIME)
        back = paint("Paint_FrameGrey", (0.1, 0.11, 0.12))
        b.quad((-50, 0, 0), (50, 0, 0), (50, 0, 25), (-50, 0, 25), sign_material(0), uvs=[(0, 1), (1, 1), (1, 0), (0, 0)])
        b.box(-50.5, 50.5, 0.2, 3, -0.5, 25.5, back, faces="nsewtb")
        return save_mesh(b, KIT + "/SM_Kit_Sign", collision="none")
    return _once("sign", build)


def tree(kind=0):
    """Own tree: tapered trunk and branches (bark), leaf cards with the shrub_02 leaf texture.
    Collision only around the trunk. kind 0: broad plane tree, 1: slim, 2: small ornamental."""
    name = "SM_Kit_Tree_%d" % kind

    def build():
        r = lib.rng(100 + kind)
        b = MeshBuilder(ground=NOGRIME)
        bark, leaf = M("Bark"), leaves()
        height, spread, trunk_r, n_branches, cards = [(620, 320, 22, 7, 46), (760, 190, 17, 6, 36), (400, 200, 12, 5, 26)][kind]
        # Trunk: a few segments leaning slightly.
        pts = [(0.0, 0.0, 0.0)]
        lean = (r.uniform(-15, 15), r.uniform(-15, 15))
        for i in range(1, 5):
            t = i / 4.0
            pts.append((lean[0] * t * t, lean[1] * t * t, height * 0.55 * t))
        for a, c in zip(pts, pts[1:]):
            cyl(b, a, c, trunk_r * (1.0 - 0.45 * a[2] / height), bark, seg=10, caps=False, r1=trunk_r * (1.0 - 0.45 * c[2] / height))
        top = pts[-1]
        tips = []
        for i in range(n_branches):
            ang = math.tau * i / n_branches + r.uniform(-0.3, 0.3)
            up = r.uniform(0.45, 0.85)
            length = r.uniform(0.6, 1.0) * spread
            end = (top[0] + math.cos(ang) * length, top[1] + math.sin(ang) * length, top[2] + up * length * 0.9 + r.uniform(0, height * 0.25))
            start = (top[0], top[1], top[2] - r.uniform(0, height * 0.12))
            mid = lib.lerp3(start, end, 0.5)
            mid = (mid[0], mid[1], mid[2] + 25)
            cyl(b, start, mid, trunk_r * 0.45, bark, seg=7, caps=False, r1=trunk_r * 0.3)
            cyl(b, mid, end, trunk_r * 0.3, bark, seg=6, caps=False, r1=trunk_r * 0.12)
            tips.append(end)
            # Two side branches off each limb: the leaves fill the crown, not a few balls.
            for k in range(2):
                s0 = lib.lerp3(mid, end, r.uniform(0.1, 0.6))
                a2 = ang + r.choice((-1, 1)) * r.uniform(0.5, 1.1)
                l2 = length * r.uniform(0.35, 0.55)
                e2 = (s0[0] + math.cos(a2) * l2, s0[1] + math.sin(a2) * l2, s0[2] + r.uniform(-0.1, 0.5) * l2)
                cyl(b, s0, e2, trunk_r * 0.16, bark, seg=5, caps=False, r1=trunk_r * 0.07)
                tips.append(e2)
        crown = (top[0], top[1], top[2] + height * 0.28)
        tips.append(crown)
        # Leaf cards: many 60-130 cm quads around every tip, each facing away from the crown's
        # centre (with jitter) - a full, irregular crown lit like a volume, not flat balls.
        cc = (top[0], top[1], top[2] + height * 0.22)
        for i in range(cards * 3):
            base = r.choice(tips)
            c = (base[0] + r.gauss(0, spread * 0.2), base[1] + r.gauss(0, spread * 0.2), base[2] + r.gauss(0, height * 0.07))
            n = norm((c[0] - cc[0] + r.gauss(0, 40), c[1] - cc[1] + r.gauss(0, 40), (c[2] - cc[2]) * 1.4 + r.gauss(0, 40) + 30))
            u = norm(cross(n, (0.0, 0.0, 1.0))) if abs(n[2]) < 0.95 else (1.0, 0.0, 0.0)
            v = cross(u, n)
            spin = r.uniform(0, math.tau)
            u, v = (scale(u, math.cos(spin))[0] + scale(v, math.sin(spin))[0], scale(u, math.cos(spin))[1] + scale(v, math.sin(spin))[1],
                    scale(u, math.cos(spin))[2] + scale(v, math.sin(spin))[2]), \
                   (scale(v, math.cos(spin))[0] - scale(u, math.sin(spin))[0], scale(v, math.cos(spin))[1] - scale(u, math.sin(spin))[1],
                    scale(v, math.cos(spin))[2] - scale(u, math.sin(spin))[2])
            half = r.uniform(60, 130) / 2
            p0 = add(c, add(scale(u, -half), scale(v, -half)))
            p1 = add(c, add(scale(u, half), scale(v, -half)))
            p2 = add(c, add(scale(u, half), scale(v, half)))
            p3 = add(c, add(scale(u, -half), scale(v, half)))
            b.quad(p0, p1, p2, p3, leaf, uvs=[(0, 1), (1, 1), (1, 0), (0, 0)], normal=n)
        col = MeshBuilder(ground=NOGRIME)
        col.box(-trunk_r, trunk_r, -trunk_r, trunk_r, 0, height * 0.5, bark)
        sm = save_mesh(b, KIT + "/" + name, collision="none")
        dm, _ = col.to_dynamic_mesh()
        opts = unreal.GeometryScriptCollisionFromMeshOptions()
        opts.set_editor_property("method", unreal.GeometryScriptCollisionGenerationMethod.ALIGNED_BOXES)
        unreal.GeometryScript_Collision.set_static_mesh_collision_from_mesh(dm, sm, opts)
        body = sm.get_editor_property("body_setup")
        body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_DEFAULT)
        lib.EAL.save_loaded_asset(sm, only_if_is_dirty=False)
        return sm
    return _once(name, build)


# ---------------------------------------------------------------------------
# Scatter
# ---------------------------------------------------------------------------

def grass_tuft():
    """Two crossed 60 x 45 cm cards with the grass texture (T_GrassCard), origin at the root."""
    def build():
        b = MeshBuilder(ground=NOGRIME)
        mat = surface("GrassCard")
        for yaw in (0.0, 90.0):
            c, s_ = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
            p0, p1 = (-30 * c, -30 * s_, 0), (30 * c, 30 * s_, 0)
            b.quad(p0, p1, (p1[0], p1[1], 45), (p0[0], p0[1], 45), mat, uvs=[(0, 1), (1, 1), (1, 0), (0, 0)], normal=(0.0, 0.0, 1.0))
        return save_mesh(b, KIT + "/SM_Kit_GrassTuft", collision="none")
    return _once("grass", build)


def grass_patch(cluster, r, cx, cy, radius, density, ground, keep=lambda x, y: True, cull=3500, tall=False):
    """Grass tufts (crossed cards) in a disc, with a few weeds and dandelions; density per m2."""
    tuft = grass_tuft()
    n = int(math.pi * radius * radius / 10000.0 * density)
    for _ in range(n):
        a, d = r.uniform(0, math.tau), radius * math.sqrt(r.random())
        x, y = cx + math.cos(a) * d, cy + math.sin(a) * d
        if not keep(x, y):
            continue
        s_ = r.uniform(0.7, 1.3 if tall else 1.0)
        cluster.add(tuft, (x, y, ground(x, y) - 2), r.uniform(0, 360), (s_, s_, s_ * r.uniform(0.7, 1.2)), cull=cull, shadow=False)
        roll = r.random()
        if roll < 0.03:
            cluster.add(lib.prop("dandelion_01", r.choice(["dandelion_01_c_LOD0", "dandelion_01_d_LOD0", "dandelion_01_e_LOD0"])),
                        (x, y, ground(x, y) - 1), r.uniform(0, 360), r.uniform(0.9, 1.3), cull=cull, shadow=False)
        elif roll < 0.05:
            cluster.add(lib.prop("weed_plant_02", r.choice(["weed_plant_02_a_LOD0", "weed_plant_02_b_LOD0", "weed_plant_02_c_LOD0"])),
                        (x, y, ground(x, y) - 1), r.uniform(0, 360), r.uniform(1.5, 2.5), cull=cull, shadow=False)


def debris(cluster, r, points, ground, cull=3000):
    """Small stones and litter at given (x, y) spots (kerbs, wall feet, corners)."""
    stones = [("namaqualand_stones_01", "namaqualand_stones_01_%s_LOD0" % k) for k in "abcde"] + [("rock_07", None), ("rock_09", None)]
    for x, y in points:
        model, mesh = r.choice(stones)
        cluster.add(lib.prop(model, mesh), (x, y, ground(x, y) - 1), r.uniform(0, 360), r.uniform(0.8, 2.2), cull=cull, shadow=False)


def container_colors():
    """Container paint: the green Poly Haven texture desaturated and tinted."""
    base = "Metal_Container_Grey"
    palette = [("Red", (1.9, 0.42, 0.3)), ("Blue", (0.35, 0.62, 1.5)), ("Orange", (2.2, 1.0, 0.25)), ("Grey", (1.05, 1.05, 1.08)),
               ("White", (1.9, 1.9, 1.85)), ("Maroon", (1.1, 0.3, 0.25)), ("Teal", (0.35, 1.1, 1.0))]
    out = [tinted(base, "Container_" + n, c, desat=1.0) for n, c in palette]
    out.append(surface("Metal_Container_Red"))  # the texture's own green
    return out


def pallet_rack():
    """One 270 cm bay of a pallet rack along +X from the origin, 110 deep (-Y), 3 beam levels, 450 high.
    Slot 0 uprights (blue), slot 1 beams (orange)."""
    def build():
        b = MeshBuilder(ground=NOGRIME)
        upright = paint("Paint_RackBlue", (0.03, 0.08, 0.3))
        beam_m = paint("Paint_RackOrange", (0.6, 0.18, 0.02))
        for x in (0.0, 270.0):
            for y in (0.0, -110.0):
                b.box(x - 4, x + 4, y - 4, y + 4, 0, 450, upright)
            for z in range(30, 440, 60):
                beam(b, (x, 0, z), (x, -110, z + 40), 3, 3, upright)
        for z in (140, 280, 420):
            for y in (0.0, -110.0):
                b.box(4, 266, y - 3, y + 3, z - 12, z, beam_m)
        return save_mesh(b, KIT + "/SM_Kit_PalletRack", collision="simple")
    return _once("rack", build)
