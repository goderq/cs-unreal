"""
v2.1 map rework: the project's own textures, drawn with Pillow (no third
party art, docs/ASSETS.md):

  T_DecalAtlas   2048 RGBA, 4 x 4 cells of 512 - cracks, oil, tyre marks,
                 rust streaks, soot, wet patches, dirt, road lines, arrows,
                 parking corners, hazard stripes, grime, moss, stencils.
                 One texture for every decal on all three maps; M_CS_Decal
                 picks the cell with a parameter.
  T_Trim         1024 RGB trim sheet - painted strips, hazard stripes,
                 galvanised and brushed steel, rubber, wood, concrete.
  T_Signs        2048 RGBA, 2 x 8 cells of 1024 x 256 - shop signs, bay and
                 dock numbers, warnings, the depot and warehouse names.
  T_LeafCluster  1024 RGBA leaf cluster for tree crowns: several hundred small
                 leaves in greens and a few yellowed ones, darker towards the
                 middle (self-shadow), cut out by alpha.
  T_GrassCard    512 x 512 RGBA: a tuft of grass blades for crossed cards,
                 bottom darker, dry tips.
  T_MacroNoise   512 RGB tiling noise: R large blotches, G medium, B fine.
                 Sampled in world space by every surface material, so equal
                 modules differ in tint, roughness and dirt.

Text uses Roboto from the engine (Engine/Content/Slate/Fonts, Apache 2.0).

    python Scripts/maps/make_textures.py      (Python 3 with Pillow)

Output: SourceArt/Maps/Generated/*.png (in git - they are small and ours).
Deterministic: fixed random seeds, the same files every run.
"""

import math
import os
import random

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont, ImageOps

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "SourceArt", "Maps", "Generated")
FONTS = r"C:\Program Files\Epic Games\UE_5.8\Engine\Content\Slate\Fonts"
CELL = 512


def font(name, size):
    return ImageFont.truetype(os.path.join(FONTS, name), size)


# ---------------------------------------------------------------------------
# Noise
# ---------------------------------------------------------------------------

def cloud(size, scale, seed, octaves=4):
    """Fractal value noise in 0..255 ('L'), features about `scale` px wide.
    Seeded with Python's random, so every run draws the same texture."""
    rng = random.Random(seed)
    acc = None
    weight_sum = 0.0
    weight = 1.0
    s = float(scale)
    for _ in range(octaves):
        n = max(2, int(size / s) + 1)
        grid = Image.new("L", (n, n))
        grid.putdata([rng.randint(0, 255) for _ in range(n * n)])
        octave = grid.resize((size, size), Image.BICUBIC)
        acc = octave if acc is None else Image.blend(acc, octave, weight / (weight_sum + weight))
        weight_sum += weight
        weight *= 0.5
        s /= 2.0
    return ImageOps.autocontrast(acc)


def threshold(img, lo, hi):
    """Smooth step of an 'L' image between lo and hi (0..255)."""
    span = max(1, hi - lo)
    return img.point(lambda v: 0 if v <= lo else 255 if v >= hi else int(255 * (v - lo) / span))


def radial(size, inner, outer, cx=None, cy=None, sx=1.0, sy=1.0):
    """255 inside `inner` px, 0 outside `outer` px (ellipse via sx, sy)."""
    cx = size / 2 if cx is None else cx
    cy = size / 2 if cy is None else cy
    img = Image.new("L", (size, size))
    px = img.load()
    for y in range(size):
        for x in range(size):
            d = math.hypot((x - cx) / sx, (y - cy) / sy)
            px[x, y] = 255 if d <= inner else 0 if d >= outer else int(255 * (outer - d) / (outer - inner))
    return img


def multiply(a, b):
    return ImageChops.multiply(a, b)


def colored(alpha, rgb):
    img = Image.new("RGBA", alpha.size, rgb + (0,))
    img.putalpha(alpha)
    return img


# ---------------------------------------------------------------------------
# Decal cells (each returns a CELL x CELL RGBA)
# ---------------------------------------------------------------------------

def crack(seed, branches, length, width):
    random.seed(seed)
    mask = Image.new("L", (CELL, CELL), 0)
    d = ImageDraw.Draw(mask)

    def walk(x, y, ang, steps, w):
        for _ in range(steps):
            ang += random.uniform(-0.45, 0.45)
            nx, ny = x + math.cos(ang) * 9, y + math.sin(ang) * 9
            d.line([(x, y), (nx, ny)], fill=255, width=max(1, int(w)))
            if random.random() < 0.06 and w > 1.2:
                walk(nx, ny, ang + random.choice((-1, 1)) * random.uniform(0.6, 1.2), steps // 2, w * 0.6)
            x, y, w = nx, ny, max(1.0, w * 0.985)

    for b in range(branches):
        a = random.uniform(0, math.tau) if branches > 1 else random.uniform(-0.3, 0.3)
        sx = CELL / 2 if branches > 1 else 20
        walk(sx, CELL / 2, a, length, width)
    mask = mask.filter(ImageFilter.GaussianBlur(1.2))
    edge = mask.filter(ImageFilter.GaussianBlur(6)).point(lambda v: int(v * 0.35))
    alpha = ImageChops.lighter(mask, edge)
    return colored(alpha, (28, 25, 22))


def stain(seed, rgb, lo, hi, falloff, strength=1.0):
    edge = cloud(CELL, 70, seed)
    core = radial(CELL, CELL * falloff[0], CELL * falloff[1], sx=1.0, sy=0.8)
    # The noise only eats the edge: the middle stays solid.
    blob = ImageChops.add(multiply(threshold(edge, lo, hi), core), threshold(core, 200, 255), 1.0, 0)
    blob = blob.filter(ImageFilter.GaussianBlur(2)).point(lambda v: int(v * strength))
    detail = cloud(CELL, 14, seed + 100, 2).point(lambda v: 170 + v // 3)
    return colored(multiply(blob, detail), rgb)


def tyre_marks():
    random.seed(7)
    mask = Image.new("L", (CELL, CELL), 0)
    d = ImageDraw.Draw(mask)
    for off in (-70, 70):
        pts = [(x, CELL / 2 + off + 40 * math.sin(x / CELL * math.pi * 1.3)) for x in range(-20, CELL + 21, 8)]
        d.line(pts, fill=200, width=46)
    tread = cloud(CELL, 18, 8, 2)
    alpha = multiply(mask.filter(ImageFilter.GaussianBlur(4)), threshold(tread, 60, 200))
    alpha = multiply(alpha, cloud(CELL, 160, 9).point(lambda v: 90 + v // 2))
    return colored(alpha, (22, 22, 22))


def rust_streaks():
    random.seed(11)
    mask = Image.new("L", (CELL, CELL), 0)
    d = ImageDraw.Draw(mask)
    for _ in range(18):
        x = random.uniform(30, CELL - 30)
        w = random.uniform(6, 26)
        length = random.uniform(CELL * 0.35, CELL * 0.95)
        steps = 30
        for i in range(steps):
            y0, y1 = length * i / steps, length * (i + 1) / steps
            a = int(255 * (1.0 - i / steps) ** 1.4)
            d.line([(x, y0), (x + random.uniform(-2, 2), y1)], fill=a, width=int(w * (1.0 - 0.5 * i / steps)))
    mask = mask.filter(ImageFilter.GaussianBlur(5))
    alpha = multiply(mask, cloud(CELL, 60, 12).point(lambda v: 120 + v // 2))
    return colored(alpha, (112, 56, 24))


def road_line():
    alpha = Image.new("L", (CELL, CELL), 0)
    ImageDraw.Draw(alpha).rectangle([0, CELL / 2 - 40, CELL, CELL / 2 + 40], fill=235)
    wear = threshold(cloud(CELL, 9, 21, 3), 55, 110)
    return colored(multiply(alpha, wear).filter(ImageFilter.GaussianBlur(1)), (236, 234, 222))


def arrow():
    alpha = Image.new("L", (CELL, CELL), 0)
    d = ImageDraw.Draw(alpha)
    d.rectangle([CELL * 0.42, CELL * 0.38, CELL * 0.58, CELL * 0.95], fill=235)
    d.polygon([(CELL * 0.22, CELL * 0.42), (CELL * 0.5, CELL * 0.05), (CELL * 0.78, CELL * 0.42)], fill=235)
    wear = threshold(cloud(CELL, 9, 22, 3), 55, 110)
    return colored(multiply(alpha, wear).filter(ImageFilter.GaussianBlur(1)), (236, 234, 222))


def parking_corner():
    alpha = Image.new("L", (CELL, CELL), 0)
    d = ImageDraw.Draw(alpha)
    d.rectangle([0, 0, 60, CELL], fill=235)
    d.rectangle([0, CELL - 60, CELL, CELL], fill=235)
    wear = threshold(cloud(CELL, 9, 23, 3), 55, 110)
    return colored(multiply(alpha, wear).filter(ImageFilter.GaussianBlur(1)), (236, 234, 222))


def hazard():
    img = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    base = Image.new("RGB", (CELL, CELL), (24, 22, 20))
    d = ImageDraw.Draw(base)
    step = 96
    for k in range(-CELL, CELL * 2, step):
        d.polygon([(k, 0), (k + step / 2, 0), (k + step / 2 - CELL, CELL), (k - CELL, CELL)], fill=(222, 170, 20))
    wear = threshold(cloud(CELL, 10, 24, 3), 50, 105)
    img = base.convert("RGBA")
    img.putalpha(multiply(Image.new("L", (CELL, CELL), 245), wear))
    return img


def grime():
    grad = Image.new("L", (CELL, CELL))
    grad.putdata([int(255 * (y / CELL) ** 2.2) for y in range(CELL) for _ in range(CELL)])
    alpha = multiply(grad, cloud(CELL, 90, 31).point(lambda v: 110 + v // 2))
    return colored(alpha, (40, 34, 26))


def moss():
    n = cloud(CELL, 70, 41)
    blob = multiply(threshold(n, 90, 170), radial(CELL, CELL * 0.18, CELL * 0.48, sy=0.6))
    blob = multiply(blob, cloud(CELL, 12, 42, 2).point(lambda v: 90 + v // 2))
    return colored(blob.filter(ImageFilter.GaussianBlur(1)), (62, 78, 30))


def stencil(text):
    alpha = Image.new("L", (CELL, CELL), 0)
    d = ImageDraw.Draw(alpha)
    f = font("Roboto-BoldCondensed.ttf", 230)
    box = d.textbbox((0, 0), text, font=f)
    d.text(((CELL - (box[2] - box[0])) / 2 - box[0], (CELL - (box[3] - box[1])) / 2 - box[1]), text, fill=235, font=f)
    wear = threshold(cloud(CELL, 8, 51, 3), 60, 115)
    return colored(multiply(alpha, wear), (230, 228, 220))


def decal_atlas():
    cells = [
        crack(1, 1, 60, 3.2), crack(2, 5, 26, 2.6), stain(3, (18, 16, 14), 100, 170, (0.12, 0.46)), stain(4, (26, 20, 14), 120, 190, (0.05, 0.44)),
        tyre_marks(), rust_streaks(), stain(5, (12, 11, 10), 70, 160, (0.08, 0.47), 0.9), stain(6, (30, 30, 30), 80, 150, (0.15, 0.48), 0.55),
        stain(7, (72, 56, 36), 110, 175, (0.05, 0.45)), road_line(), arrow(), parking_corner(),
        hazard(), grime(), moss(), stencil("B-07"),
    ]
    atlas = Image.new("RGBA", (CELL * 4, CELL * 4), (0, 0, 0, 0))
    for i, c in enumerate(cells):
        atlas.paste(c, ((i % 4) * CELL, (i // 4) * CELL))
    return atlas


# ---------------------------------------------------------------------------
# Trim sheet
# ---------------------------------------------------------------------------

def trim_sheet():
    size = 1024
    img = Image.new("RGB", (size, size))
    rows = [  # (height px, kind, colour)
        (128, "hazard", None), (64, "paint", (150, 32, 26)), (64, "paint", (212, 160, 24)), (64, "paint", (40, 72, 112)),
        (128, "steel", (150, 152, 150)), (64, "paint", (18, 18, 18)), (64, "paint", (214, 210, 198)), (64, "paint", (46, 76, 52)),
        (64, "paint", (206, 96, 28)), (128, "steel", (118, 120, 124)), (128, "wood", (96, 66, 40)), (64, "paint", (128, 124, 116)),
    ]
    y = 0
    for i, (h, kind, rgb) in enumerate(rows):
        band = Image.new("RGB", (size, h))
        if kind == "hazard":
            d = ImageDraw.Draw(band)
            band.paste((24, 22, 20), (0, 0, size, h))
            for k in range(-h, size + h, 64):
                d.polygon([(k, 0), (k + 32, 0), (k + 32 - h, h), (k - h, h)], fill=(222, 170, 20))
        elif kind == "steel":
            n = cloud(size, 6, 60 + i, 2).resize((size, h))
            streak = n.transform((size, h), Image.AFFINE, (1, 0, 0, 0, 0.02, 0))
            band = Image.merge("RGB", [streak.point(lambda v, c=c: int(c * (0.85 + 0.3 * v / 255))) for c in rgb])
        elif kind == "wood":
            n = cloud(size, 30, 70, 3).resize((size, h)).transform((size, h), Image.AFFINE, (1, 0, 0, 0, 0.05, 0))
            band = Image.merge("RGB", [n.point(lambda v, c=c: int(c * (0.7 + 0.6 * v / 255))) for c in rgb])
        else:
            n = cloud(size, 40, 80 + i, 3).resize((size, h))
            band = Image.merge("RGB", [n.point(lambda v, c=c: int(c * (0.88 + 0.2 * v / 255))) for c in rgb])
        img.paste(band, (0, y))
        y += h
    grime_layer = cloud(size, 80, 99).point(lambda v: 205 + v // 5)
    return Image.merge("RGB", [multiply(ch, grime_layer) for ch in img.split()])


# ---------------------------------------------------------------------------
# Signs
# ---------------------------------------------------------------------------

SIGNS = [  # text, sub, background, foreground, style
    ("CAFFÈ ROMA", "dal 1952", (38, 58, 44), (232, 214, 170), "serif"),
    ("FARMACIA", "", (240, 238, 230), (20, 128, 60), "cross"),
    ("PANIFICIO", "pane · dolci", (150, 52, 36), (246, 232, 200), "serif"),
    ("TABACCHI", "", (24, 30, 48), (236, 236, 236), "tabacchi"),
    ("ALIMENTARI", "frutta e verdura", (212, 178, 96), (60, 36, 20), "serif"),
    ("BAR SPORT", "", (180, 30, 30), (250, 246, 236), "bold"),
    ("VIA DEL POZZO", "", (236, 232, 222), (30, 30, 30), "plate"),
    ("PIAZZA SAN LUCA", "", (236, 232, 222), (30, 30, 30), "plate"),
    ("DEPOT 3", "RAIL · ROAD · STORAGE", (32, 34, 38), (230, 170, 30), "bold"),
    ("LOADING BAY", "1   2   3   4", (212, 160, 24), (24, 22, 20), "bold"),
    ("NO SMOKING", "", (240, 240, 236), (190, 24, 24), "warning"),
    ("DANGER  HIGH VOLTAGE", "", (230, 190, 20), (20, 20, 20), "warning"),
    ("NORDLOG", "LOGISTICS CENTER", (238, 240, 242), (24, 64, 120), "logo"),
    ("DOCK", "1   2   3   4   5   6", (24, 64, 120), (240, 240, 240), "bold"),
    ("OFFICE", "RECEPTION  ->", (60, 64, 70), (240, 240, 240), "bold"),
    ("AUTHORISED PERSONNEL ONLY", "", (30, 90, 170), (245, 245, 245), "bold"),
]


def sign(text, sub, bg, fg, style, w=1024, h=256):
    img = Image.new("RGB", (w, h), bg)
    d = ImageDraw.Draw(img)
    if style in ("plate", "warning"):
        d.rectangle([10, 10, w - 11, h - 11], outline=fg, width=10)
    main = font("Roboto-Bold.ttf" if style != "serif" else "Roboto-BlackItalic.ttf", 120 if len(text) < 12 else 84 if len(text) < 18 else 62)
    tx = 60 if style in ("cross", "tabacchi") else None
    box = d.textbbox((0, 0), text, font=main)
    tw, th = box[2] - box[0], box[3] - box[1]
    if style == "cross":
        c = 128
        d.rectangle([60, c - 20, 150, c + 20], fill=fg)
        d.rectangle([85, c - 45, 125, c + 45], fill=fg)
        tx = 200
    if style == "tabacchi":
        d.rectangle([40, 38, 220, 218], fill=(236, 236, 236))
        d.text((82, 30), "T", fill=(24, 30, 48), font=font("Roboto-Black.ttf", 170))
        tx = 260
    y = (h - th) / 2 - box[1] - (22 if sub else 0)
    x = tx if tx is not None else (w - tw) / 2 - box[0]
    d.text((x, y), text, fill=fg, font=main)
    if sub:
        sf = font("Roboto-Medium.ttf", 40)
        sb = d.textbbox((0, 0), sub, font=sf)
        sx = tx if tx is not None else (w - (sb[2] - sb[0])) / 2 - sb[0]
        d.text((sx, y + th + 30), sub, fill=fg, font=sf)
    if style == "logo":
        d.polygon([(40, 200), (110, 56), (180, 200)], fill=fg)
    wear = cloud(w, 60, sum(map(ord, text))).resize((w, h)).point(lambda v: 200 + v // 5)
    return Image.merge("RGB", [multiply(ch, wear) for ch in img.split()])


def sign_atlas():
    atlas = Image.new("RGB", (2048, 2048))
    for i, s in enumerate(SIGNS):
        atlas.paste(sign(*s), ((i % 2) * 1024, (i // 2) * 256))
    return atlas


def tiling_cloud(size, cells, seed, octaves):
    """Noise that tiles: a periodic random grid, upscaled with wrap-around."""
    rng = random.Random(seed)
    acc = None
    wsum, w, n = 0.0, 1.0, cells
    for _ in range(octaves):
        grid = Image.new("L", (n, n))
        grid.putdata([rng.randint(0, 255) for _ in range(n * n)])
        # 3 x 3 copies, scaled, centre cut: seamless edges.
        big = Image.new("L", (n * 3, n * 3))
        for gx in range(3):
            for gy in range(3):
                big.paste(grid, (gx * n, gy * n))
        big = big.resize((size * 3, size * 3), Image.BICUBIC).crop((size, size, size * 2, size * 2))
        acc = big if acc is None else Image.blend(acc, big, w / (wsum + w))
        wsum += w
        w *= 0.5
        n *= 2
    return ImageOps.autocontrast(acc)


def macro_noise():
    return Image.merge("RGB", [tiling_cloud(512, 4, 301, 3), tiling_cloud(512, 16, 302, 3), tiling_cloud(512, 64, 303, 2)])


def leaf_cluster(size=1024, seed=77):
    rng = random.Random(seed)
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    greens = [(62, 92, 34), (78, 110, 40), (54, 80, 30), (96, 122, 46), (70, 98, 36), (110, 128, 52), (84, 104, 38), (150, 138, 62)]
    clumps = [(size / 2 + rng.uniform(-260, 260), size / 2 + rng.uniform(-260, 260), rng.uniform(150, 250)) for _ in range(7)]
    for i in range(1600):
        ccx, ccy, cr = rng.choice(clumps)
        r = cr * math.sqrt(rng.random())
        a = rng.uniform(0, math.tau)
        cx, cy = ccx + math.cos(a) * r, ccy + math.sin(a) * r
        length, width = rng.uniform(42, 78), rng.uniform(18, 32)
        box = int(length * 2)
        leaf = Image.new("RGBA", (box, box), (0, 0, 0, 0))
        d = ImageDraw.Draw(leaf)
        c = rng.choice(greens)
        # Leaves drawn later lie on top: the first ones (inner, shaded) are darker.
        shade = 0.6 + 0.4 * (i / 1600.0) * rng.uniform(0.8, 1.1)
        for k in range(4):  # base to tip, lighter towards the tip
            t = k / 3.0
            col = tuple(min(255, int(v * shade * (0.9 + 0.25 * t))) for v in c)
            x0 = length - length / 2 + t * length * 0.2
            d.ellipse([x0, length - width / 2 * (1 - 0.15 * t), length + length / 2, length + width / 2 * (1 - 0.15 * t)], fill=col + (255,))
        d.line([(length - length / 2, length), (length + length / 2, length)], fill=tuple(int(v * shade * 0.65) for v in c) + (255,), width=2)
        leaf = leaf.rotate(rng.uniform(0, 360), resample=Image.BICUBIC)
        img.alpha_composite(leaf, (int(cx - length), int(cy - length)))
    return img


def grass_card(size=512, seed=88):
    rng = random.Random(seed)
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    greens = [(78, 102, 40), (96, 116, 48), (66, 88, 34), (120, 124, 60), (140, 128, 70)]
    for _ in range(170):
        x0 = rng.gauss(size / 2, size * 0.18)
        h = rng.uniform(0.35, 0.95) * size
        lean = rng.uniform(-0.35, 0.35) * h
        w = rng.uniform(4, 9)
        c = rng.choice(greens)
        steps = 12
        pts_l, pts_r = [], []
        for k in range(steps + 1):
            t = k / steps
            x = x0 + lean * t * t
            y = size - t * h
            half = w * (1 - t) + 0.5
            pts_l.append((x - half, y))
            pts_r.append((x + half, y))
        shade = rng.uniform(0.75, 1.1)
        col = tuple(int(v * shade) for v in c)
        d.polygon(pts_l + list(reversed(pts_r)), fill=col + (255,))
    # Darker at the root (self-shadow in the tuft), dry lighter tips.
    px = img.load()
    for y in range(size):
        f = 0.55 + 0.45 * (1 - y / size)
        for x in range(size):
            r, g, b, a = px[x, y]
            if a:
                px[x, y] = (min(255, int(r * f + (1 - y / size) * 18)), min(255, int(g * f + (1 - y / size) * 10)), int(b * f), a)
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    decal_atlas().save(os.path.join(OUT, "T_DecalAtlas.png"))
    trim_sheet().save(os.path.join(OUT, "T_Trim.png"))
    sign_atlas().save(os.path.join(OUT, "T_Signs.png"))
    macro_noise().save(os.path.join(OUT, "T_MacroNoise.png"))
    leaves = leaf_cluster()
    leaves.convert("RGB").save(os.path.join(OUT, "T_LeafCluster_D.png"))
    leaves.getchannel("A").save(os.path.join(OUT, "T_LeafCluster_A.png"))
    grass = grass_card()
    grass.convert("RGB").save(os.path.join(OUT, "T_GrassCard_D.png"))
    grass.getchannel("A").save(os.path.join(OUT, "T_GrassCard_A.png"))
    print("written to", OUT)


if __name__ == "__main__":
    main()
