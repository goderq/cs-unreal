"""
Downloads the Poly Haven (CC0) textures and models the v1.1 maps use into
SourceArt/Maps/PolyHaven/<asset>/. Textures: 2K JPG diffuse, OpenGL normal
and ARM (AO/roughness/metal). Models: 1K FBX with their textures.

    python Scripts/download_polyhaven.py        (any Python 3, stdlib only)

Source: https://polyhaven.com - every asset is CC0 (public domain).
"""
import json, os, urllib.request

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "SourceArt", "Maps", "PolyHaven")
TEXTURES = ["concrete_floor_worn_001", "asphalt_02", "corrugated_iron", "metal_plate", "rusty_painted_metal",
            "painted_concrete", "red_brick_03", "concrete_wall_003", "cobblestone_floor_08", "plastered_wall_02",
            "white_plaster_02", "castle_brick_02_red", "clay_roof_tiles_02", "weathered_planks", "stone_tiles_02",
            "yellow_plaster"]
MODELS = ["Barrel_01", "wooden_crate_01", "wooden_crate_02", "concrete_road_barrier", "cardboard_box_01",
          "utility_box_02", "old_military_crate", "wine_barrel_01", "planter_box_01", "painted_wooden_bench"]
UA = {"User-Agent": "CS-Fusion asset fetch (CC0)"}


def get(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=120).read()


def save(url, path):
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(get(url))


for tid in TEXTURES:
    files = json.loads(get("https://api.polyhaven.com/files/" + tid))
    for m in ["Diffuse", "nor_gl", "arm"]:
        e = files[m]["2k"]["jpg"]
        save(e["url"], os.path.join(ROOT, tid, "%s_%s_2k.jpg" % (tid, m)))
    print("texture", tid)

for mid in MODELS:
    files = json.loads(get("https://api.polyhaven.com/files/" + mid))
    e = files["fbx"]["1k"]["fbx"]
    save(e["url"], os.path.join(ROOT, mid, os.path.basename(e["url"])))
    for rel, inc in e.get("include", {}).items():
        save(inc["url"], os.path.join(ROOT, mid, rel))
    print("model", mid)
print("done")
