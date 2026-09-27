"""
v2.1 map rework: downloads the Poly Haven (CC0) textures and models the new
Depot, Old Town and Warehouse use (docs/MAPS_REWORK.md, docs/ASSETS.md) into
SourceArt/_download/polyhaven_v21/<asset>/ - outside git, re-downloadable.

Textures: JPG diffuse, OpenGL normal, ARM (AO/roughness/metal); 2K for large
surfaces, 1K for the rest. Models: 1K FBX with their textures.

    python Scripts/maps/download_assets.py      (any Python 3, stdlib only)

Source: https://polyhaven.com - every asset is CC0 (public domain).
Download approved by the project owner on 27.09.2026 (447 MB).
"""
import json, os, urllib.request

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "SourceArt", "_download", "polyhaven_v21")
TEX2K = ["road_damaged","concrete_floor_damaged_01","gravel_ground_01","dry_ground_01","leafy_grass","factory_wall","container_side",
         "patterned_cobblestone","pavement_03","worn_cracked_plaster","red_plaster_weathered","roof_tiles_14",
         "concrete_floor_painted","asphalt_04","exterior_wall_cladding","blue_metal_plate"]
TEX1K = ["box_profile_metal_sheet","rusty_metal_grid","painted_metal_shutter","concrete_block_wall","blue_plaster_weathered",
         "yellow_plaster_02","damaged_plaster","mixed_brick_wall","large_sandstone_blocks_01","park_dirt","brick_pavement",
         "anti_slip_concrete","concrete_pavers_02","corrugated_iron_02","metal_grate_rusty","concrete_wall_006","grey_roof_tiles",
         "sparse_grass","grassy_cobblestone","grass_concrete_pavement","bark_platanus"]
MODELS = ["modular_industrial_pipes_01","modular_airduct_circular_01","modular_chainlink_fence",
 "portable_generator","power_box_01","exterior_aircon_unit","security_light","hanging_industrial_lamp","mounted_fluorescent_lights",
 "propane_tank","small_lpg_tank","industrial_pastic_container","plastic_crate_03","barrel_03","old_tyre","hand_truck","steel_frame_shelves_01",
 "worn_metal_rack","ladder_sectioned_01","concrete_road_barrier_02","overhead_crane","old_military_compressor",
 "modular_fire_escape","modular_metal_gutter","street_lamp_01","street_lamp_02","metal_trash_can","covered_car",
 "rollershutter_door","rollershutter_window_01","water_manhole_cover","utility_box_01","security_camera_01",
 "large_iron_gate","potted_plant_04","shrub_02","shrub_03","shrub_04","weed_plant_02","fern_02",
 "grass_bermuda_01","dandelion_01","tree_stump_01","rock_07","rock_09","stone_01","namaqualand_stones_01"]
UA = {"User-Agent": "CS-Fusion asset fetch (CC0)"}


def get(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=180).read()


def save(url, path):
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = get(url)
    with open(path + ".part", "wb") as f:
        f.write(data)
    os.replace(path + ".part", path)


def texture(tid, res):
    files = json.loads(get("https://api.polyhaven.com/files/" + tid))
    for m in ["Diffuse", "nor_gl", "arm"]:
        save(files[m][res]["jpg"]["url"], os.path.join(ROOT, tid, "%s_%s_%s.jpg" % (tid, m, res)))
    print("texture", tid, res, flush=True)


for tid in TEX2K:
    texture(tid, "2k")
for tid in TEX1K:
    texture(tid, "1k")
for mid in MODELS:
    files = json.loads(get("https://api.polyhaven.com/files/" + mid))
    e = files["fbx"]["1k"]["fbx"]
    save(e["url"], os.path.join(ROOT, mid, os.path.basename(e["url"])))
    for rel, inc in e.get("include", {}).items():
        save(inc["url"], os.path.join(ROOT, mid, rel))
    print("model", mid, flush=True)
print("done", flush=True)
