"""
CS-Fusion content bootstrap (Unreal Engine 5.8).

Creates the Stage 1 content that cannot be expressed as C++ source:

  Content/Input/IA_*                 Enhanced Input actions
  Content/Input/IMC_Default          Input Mapping Context, keyboard + mouse
  Content/Input/DA_CSInputConfig     UCSInputConfig, fully wired
  Content/Characters/BP_CSCharacter  Blueprint subclass of ACSCharacter
  Content/Maps/Lvl_Warehouse         Test map: floor, cover, 8 player starts

Run headless:

    UnrealEditor-Cmd.exe "<Project>.uproject" -run=pythonscript
        -script="<Project>/Scripts/bootstrap_content.py"

Or from the editor: Output Log > switch "Cmd" to "Python" and run

    exec(open(r"<Project>/Scripts/bootstrap_content.py").read())

Idempotent: existing assets are reused rather than duplicated.

API notes for UE 5.8
--------------------
* UInputAction and UInputMappingContext both derive from UDataAsset and have
  no dedicated factory, so DataAssetFactory creates them.
* UInputMappingContext::Mappings is deprecated since 5.7. Mappings now live in
  the DefaultKeyMappings struct (FInputMappingContextMappingData).
* Structs marshal to Python by value, so MapKey()'s returned mapping cannot be
  edited in place. The whole mapping array is built here and assigned at once.
* Modifiers are Instanced UPROPERTYs, so they are created with new_object()
  parented to the context rather than default-constructed into the transient
  package.
* EditorLevelLibrary is superseded by LevelEditorSubsystem and
  EditorActorSubsystem.
"""

import unreal

ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EDITOR_ASSET = unreal.EditorAssetLibrary

INPUT_DIR = "/Game/Input"
CHAR_DIR = "/Game/Characters"
MAPS_DIR = "/Game/Maps"
MAP_PATH = MAPS_DIR + "/Lvl_Warehouse"


def log(message):
    unreal.log("[CS-Bootstrap] " + str(message))


def ensure_dir(path):
    if not EDITOR_ASSET.does_directory_exist(path):
        EDITOR_ASSET.make_directory(path)


def data_asset_factory(asset_class):
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class)
    return factory


def create_asset(name, package_path, asset_class, factory):
    full_path = "{0}/{1}".format(package_path, name)
    if EDITOR_ASSET.does_asset_exist(full_path):
        log("reuse  " + full_path)
        return EDITOR_ASSET.load_asset(full_path)
    log("create " + full_path)
    return ASSET_TOOLS.create_asset(name, package_path, asset_class, factory)


# ---------------------------------------------------------------------------
# Input actions
# ---------------------------------------------------------------------------

BOOL = unreal.InputActionValueType.BOOLEAN
AXIS2D = unreal.InputActionValueType.AXIS2D

# (asset name, value type, UCSInputConfig python property)
#
# The property name is spelled out rather than derived from the asset name:
# Unreal snake-cases the UPROPERTY identifier, so IA_ToggleInventory becomes
# ia_toggle_inventory, which asset_name.lower() would not produce.
ACTION_SPEC = [
    ("IA_Move", AXIS2D, "ia_move"),
    ("IA_Look", AXIS2D, "ia_look"),
    ("IA_Jump", BOOL, "ia_jump"),
    ("IA_Sprint", BOOL, "ia_sprint"),
    ("IA_Crouch", BOOL, "ia_crouch"),
    ("IA_Fire", BOOL, "ia_fire"),
    ("IA_Aim", BOOL, "ia_aim"),
    ("IA_Reload", BOOL, "ia_reload"),
    ("IA_Interact", BOOL, "ia_interact"),
    ("IA_ToggleInventory", BOOL, "ia_toggle_inventory"),
    ("IA_PauseMenu", BOOL, "ia_pause_menu"),
    ("IA_Scoreboard", BOOL, "ia_scoreboard"),
    ("IA_EquipSlot", unreal.InputActionValueType.AXIS1D, "ia_equip_slot"),
    ("IA_Drop", BOOL, "ia_drop"),
]


def make_input_actions():
    ensure_dir(INPUT_DIR)
    factory = data_asset_factory(unreal.InputAction)

    actions = {}
    for name, value_type, _prop in ACTION_SPEC:
        asset = create_asset(name, INPUT_DIR, unreal.InputAction, factory)
        asset.set_editor_property("value_type", value_type)
        EDITOR_ASSET.save_loaded_asset(asset)
        actions[name] = asset
    return actions


# ---------------------------------------------------------------------------
# Mapping context
# ---------------------------------------------------------------------------

def make_mapping_context(actions):
    factory = data_asset_factory(unreal.InputMappingContext)
    imc = create_asset("IMC_Default", INPUT_DIR, unreal.InputMappingContext, factory)

    def negate():
        return unreal.new_object(unreal.InputModifierNegate, imc)

    def swizzle_yxz():
        mod = unreal.new_object(unreal.InputModifierSwizzleAxis, imc)
        mod.set_editor_property("order", unreal.InputAxisSwizzle.YXZ)
        return mod

    def fkey(key_name):
        # FKey's Python binding has no positional constructor; the key is set
        # through its KeyName property.
        key = unreal.Key()
        key.set_editor_property("key_name", key_name)
        return key

    def mapping(action, key_name, modifiers=None):
        entry = unreal.EnhancedActionKeyMapping()
        entry.set_editor_property("action", action)
        entry.set_editor_property("key", fkey(key_name))
        if modifiers:
            entry.set_editor_property("modifiers", modifiers)
        return entry

    move = actions["IA_Move"]
    mappings = [
        # Axis2D: X = strafe (right positive), Y = forward.
        # ACSCharacter::Input_Move reads Y as forward and X as right.
        mapping(move, "W", [swizzle_yxz()]),
        mapping(move, "S", [swizzle_yxz(), negate()]),
        mapping(move, "D"),
        mapping(move, "A", [negate()]),

        mapping(actions["IA_Look"], "Mouse2D"),
        mapping(actions["IA_Jump"], "SpaceBar"),
        mapping(actions["IA_Sprint"], "LeftShift"),
        mapping(actions["IA_Crouch"], "LeftControl"),
        mapping(actions["IA_Fire"], "LeftMouseButton"),
        mapping(actions["IA_Aim"], "RightMouseButton"),
        mapping(actions["IA_Reload"], "R"),
        mapping(actions["IA_Interact"], "E"),
        mapping(actions["IA_ToggleInventory"], "Tab"),
        mapping(actions["IA_PauseMenu"], "Escape"),
        mapping(actions["IA_Scoreboard"], "BackSpace"),
    ]

    data = unreal.InputMappingContextMappingData()
    data.set_editor_property("mappings", mappings)
    imc.set_editor_property("default_key_mappings", data)

    EDITOR_ASSET.save_loaded_asset(imc)
    log("IMC_Default: {0} mappings".format(len(mappings)))
    return imc


# ---------------------------------------------------------------------------
# Input config data asset
# ---------------------------------------------------------------------------

def make_input_config(actions, imc):
    factory = data_asset_factory(unreal.CSInputConfig)
    config = create_asset("DA_CSInputConfig", INPUT_DIR, unreal.CSInputConfig, factory)

    config.set_editor_property("default_mapping_context", imc)
    config.set_editor_property("mapping_priority", 0)
    for name, _value_type, prop in ACTION_SPEC:
        # Explicit mapping: see ACTION_SPEC.
        config.set_editor_property(prop, actions[name])

    EDITOR_ASSET.save_loaded_asset(config)
    return config


# ---------------------------------------------------------------------------
# Character blueprint
# ---------------------------------------------------------------------------

def make_character_blueprint(input_config):
    ensure_dir(CHAR_DIR)

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.CSCharacter)
    bp = create_asset("BP_CSCharacter", CHAR_DIR, unreal.Blueprint, factory)

    cdo = unreal.get_default_object(bp.generated_class())
    cdo.set_editor_property("input_config", input_config)

    EDITOR_ASSET.save_loaded_asset(bp)
    return bp


def make_gamemode_blueprint(character_bp):
    """
    ACSGameMode defaults DefaultPawnClass to the native ACSCharacter, which has
    no InputConfig assigned and therefore cannot be driven. The shipped game
    mode is this Blueprint, which points at BP_CSCharacter instead.
    """
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.CSGameMode)
    bp = create_asset("BP_CSGameMode", CHAR_DIR, unreal.Blueprint, factory)

    cdo = unreal.get_default_object(bp.generated_class())
    cdo.set_editor_property("default_pawn_class", character_bp.generated_class())

    EDITOR_ASSET.save_loaded_asset(bp)
    return bp


# ---------------------------------------------------------------------------
# Test map
# ---------------------------------------------------------------------------

def make_test_map(gamemode_bp):
    ensure_dir(MAPS_DIR)

    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    if EDITOR_ASSET.does_asset_exist(MAP_PATH):
        log("reuse  {0} (delete it to regenerate)".format(MAP_PATH))
        return MAP_PATH

    level_subsystem.new_level(MAP_PATH)

    cube = EDITOR_ASSET.load_asset("/Engine/BasicShapes/Cube.Cube")

    def box(x, y, z, sx, sy, sz, label):
        actor = actor_subsystem.spawn_actor_from_class(
            unreal.StaticMeshActor, unreal.Vector(x, y, z), unreal.Rotator(0, 0, 0))
        actor.set_actor_label(label)
        actor.set_actor_scale3d(unreal.Vector(sx, sy, sz))
        actor.static_mesh_component.set_static_mesh(cube)
        return actor

    # Floor 60 m x 60 m, then perimeter walls.
    box(0, 0, -50, 60, 60, 1, "Floor")
    box(0, 3000, 200, 60, 1, 6, "Wall_N")
    box(0, -3000, 200, 60, 1, 6, "Wall_S")
    box(3000, 0, 200, 1, 60, 6, "Wall_E")
    box(-3000, 0, 200, 1, 60, 6, "Wall_W")

    # Central structure plus symmetric cover, so both bases get equal sightlines.
    box(0, 0, 100, 8, 8, 3, "Mid_Block")
    for x, y in [(1200, 600), (-1200, 600), (1200, -600), (-1200, -600),
                 (0, 1400), (0, -1400), (1800, 0), (-1800, 0)]:
        box(x, y, 45, 3, 3, 1.9, "Cover_{0}_{1}".format(x, y))

    # Player starts. ACSGameMode sorts them by name and indexes with the Photon
    # player id, so the numbering here decides who spawns where.
    for i in range(4):
        start = actor_subsystem.spawn_actor_from_class(
            unreal.PlayerStart, unreal.Vector(-2500, -450 + i * 300, 100),
            unreal.Rotator(0, 0, 0))
        start.set_actor_label("PlayerStart_A{0}".format(i))
    for i in range(4):
        start = actor_subsystem.spawn_actor_from_class(
            unreal.PlayerStart, unreal.Vector(2500, -450 + i * 300, 100),
            unreal.Rotator(0, 180, 0))
        start.set_actor_label("PlayerStart_B{0}".format(i))

    # Lighting, otherwise the level ships pitch black.
    actor_subsystem.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0, 0, 1500), unreal.Rotator(-45, -35, 0))
    actor_subsystem.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(0, 0, 1200), unreal.Rotator(0, 0, 0))
    actor_subsystem.spawn_actor_from_class(
        unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))

    # World Settings: pin the game mode and the pawn for this map explicitly so
    # a packaged build does not depend on project defaults alone.
    world = unreal.EditorLevelLibrary.get_editor_world()
    if world:
        settings = world.get_world_settings()
        settings.set_editor_property("default_game_mode", gamemode_bp.generated_class())

    level_subsystem.save_current_level()
    log("created " + MAP_PATH)
    return MAP_PATH


WEAPONS_DIR = "/Game/Weapons"


def make_starter_pistol():
    """
    The starter pistol. Referenced from DefaultGame.ini
    ([/Script/CSFusion.CSCombatSettings] StarterWeapon=...), not from any
    Blueprint, so the authority always validates against the same asset.
    Numbers are left at the UCSWeaponDefinition C++ defaults except identity.
    """
    ensure_dir(WEAPONS_DIR)
    factory = data_asset_factory(unreal.CSWeaponDefinition)
    pistol = create_asset("DA_Weapon_StarterPistol", WEAPONS_DIR, unreal.CSWeaponDefinition, factory)

    pistol.set_editor_property("weapon_id", "starter_pistol")
    pistol.set_editor_property("display_name", unreal.Text("Starter Pistol"))
    pistol.set_editor_property("description", unreal.Text(
        "Always carried. Never dropped, never lost, restored on respawn."))
    pistol.set_editor_property("is_starter_weapon", True)

    EDITOR_ASSET.save_loaded_asset(pistol)
    return pistol


# ---------------------------------------------------------------------------
# Stage 3: weapons, items, loot placement
# ---------------------------------------------------------------------------

ITEMS_DIR = "/Game/Items"

# name: (display, stats). Anything not listed keeps the UCSWeaponDefinition
# C++ default. reserve_ammo = 0 because inventory weapons reload from ammo
# items in the inventory, not from a built-in reserve.
WEAPONS = {
    "AK47": ("AK-47", dict(
        base_damage=32.0, headshot_multiplier=4.0, range=15000.0,
        falloff_start_distance=2500.0, falloff_end_distance=9000.0, min_damage_multiplier=0.5,
        rounds_per_minute=600.0, automatic=True, magazine_size=30, reserve_ammo=0, reload_seconds=2.4,
        hip_spread_degrees=2.2, aim_spread_degrees=0.35, spread_per_shot=0.6, max_bloom_spread_degrees=5.0,
        recoil_pitch=1.2, recoil_yaw=0.35)),
    "M4": ("M4", dict(
        base_damage=28.0, headshot_multiplier=4.0, range=15000.0,
        falloff_start_distance=2500.0, falloff_end_distance=9000.0, min_damage_multiplier=0.5,
        rounds_per_minute=720.0, automatic=True, magazine_size=30, reserve_ammo=0, reload_seconds=2.1,
        hip_spread_degrees=1.8, aim_spread_degrees=0.25, spread_per_shot=0.45, max_bloom_spread_degrees=4.0,
        recoil_pitch=0.9, recoil_yaw=0.25)),
    "SMG": ("SMG", dict(
        base_damage=20.0, headshot_multiplier=3.0, range=8000.0,
        falloff_start_distance=800.0, falloff_end_distance=3500.0, min_damage_multiplier=0.4,
        rounds_per_minute=900.0, automatic=True, magazine_size=25, reserve_ammo=0, reload_seconds=1.8,
        hip_spread_degrees=2.5, aim_spread_degrees=0.9, spread_per_shot=0.35, max_bloom_spread_degrees=4.5,
        recoil_pitch=0.5, recoil_yaw=0.3)),
    "Shotgun": ("Shotgun", dict(
        base_damage=14.0, headshot_multiplier=2.0, range=3000.0, pellets_per_shot=8,
        falloff_start_distance=400.0, falloff_end_distance=1500.0, min_damage_multiplier=0.2,
        rounds_per_minute=70.0, automatic=False, magazine_size=6, reserve_ammo=0, reload_seconds=3.0,
        hip_spread_degrees=5.0, aim_spread_degrees=3.5, spread_per_shot=0.0, max_bloom_spread_degrees=5.0,
        recoil_pitch=3.0, recoil_yaw=0.5)),
    "Sniper": ("Sniper Rifle", dict(
        base_damage=95.0, headshot_multiplier=2.5, range=30000.0,
        falloff_start_distance=20000.0, falloff_end_distance=30000.0, min_damage_multiplier=0.9,
        rounds_per_minute=45.0, automatic=False, magazine_size=5, reserve_ammo=0, reload_seconds=3.2,
        hip_spread_degrees=7.0, aim_spread_degrees=0.0, spread_per_shot=0.0, max_bloom_spread_degrees=7.0,
        recoil_pitch=4.0, recoil_yaw=0.2)),
}

CUBE = "/Engine/BasicShapes/Cube.Cube"
SPHERE = "/Engine/BasicShapes/Sphere.Sphere"
CYL = "/Engine/BasicShapes/Cylinder.Cylinder"

# ItemId: (asset suffix, display, type, stackable, max_stack, pickup_count,
#          weapon key, ammo item id, armor, heal, mesh, scale, color)
T = unreal.CSItemType
ITEMS = [
    ("ak47", "AK47", "AK-47", T.WEAPON, False, 1, 1, "AK47", "ammo_rifle", 0, 0, CUBE, (0.9, 0.12, 0.22), (1.0, 0.45, 0.1)),
    ("m4", "M4", "M4", T.WEAPON, False, 1, 1, "M4", "ammo_rifle", 0, 0, CUBE, (0.85, 0.12, 0.22), (0.15, 0.4, 0.15)),
    ("smg", "SMG", "SMG", T.WEAPON, False, 1, 1, "SMG", "ammo_smg", 0, 0, CUBE, (0.55, 0.12, 0.2), (0.5, 0.5, 0.55)),
    ("shotgun", "Shotgun", "Shotgun", T.WEAPON, False, 1, 1, "Shotgun", "ammo_shells", 0, 0, CUBE, (0.95, 0.14, 0.2), (0.45, 0.25, 0.1)),
    ("sniper", "Sniper", "Sniper Rifle", T.WEAPON, False, 1, 1, "Sniper", "ammo_sniper", 0, 0, CUBE, (1.2, 0.1, 0.2), (0.08, 0.08, 0.1)),
    ("ammo_rifle", "AmmoRifle", "Rifle Ammo", T.AMMO, True, 120, 30, None, "", 0, 0, CUBE, (0.25, 0.18, 0.15), (0.95, 0.8, 0.2)),
    ("ammo_smg", "AmmoSMG", "SMG Ammo", T.AMMO, True, 150, 50, None, "", 0, 0, CUBE, (0.25, 0.18, 0.15), (0.8, 0.8, 0.3)),
    ("ammo_shells", "AmmoShells", "Shotgun Shells", T.AMMO, True, 32, 8, None, "", 0, 0, CUBE, (0.25, 0.18, 0.15), (0.9, 0.3, 0.2)),
    ("ammo_sniper", "AmmoSniper", "Sniper Ammo", T.AMMO, True, 20, 5, None, "", 0, 0, CUBE, (0.25, 0.18, 0.15), (0.6, 0.6, 0.9)),
    ("grenade", "Grenade", "Grenade", T.GRENADE, True, 3, 1, None, "", 0, 0, SPHERE, (0.18, 0.18, 0.22), (0.25, 0.3, 0.15)),
    ("armor", "Armor", "Armor Vest", T.ARMOR, True, 2, 1, None, "", 50.0, 0, CUBE, (0.5, 0.45, 0.14), (0.2, 0.45, 1.0)),
    ("medkit", "Medkit", "Medkit", T.MEDKIT, True, 3, 1, None, "", 0, 50.0, CUBE, (0.35, 0.3, 0.15), (1.0, 0.15, 0.15)),
]

# ItemId, x, y. Floor top is z = 0. Player starts are at x = -2500 / +2500.
LOOT_LAYOUT = [
    ("ak47", -2150, -150), ("m4", -2150, 150),
    ("ammo_rifle", -2150, -450), ("ammo_rifle", -2150, 450),
    ("armor", -2150, 750), ("medkit", -2150, -750),
    ("smg", 2150, -150), ("shotgun", 2150, 150),
    ("ammo_smg", 2150, -450), ("ammo_shells", 2150, 450),
    ("armor", 2150, 750), ("medkit", 2150, -750),
    ("sniper", 0, 650), ("ammo_sniper", 0, -650),
    ("grenade", 650, 0), ("grenade", -650, 0),
]


def make_weapons():
    ensure_dir(WEAPONS_DIR)
    factory = data_asset_factory(unreal.CSWeaponDefinition)
    out = {}
    for key, (display, stats) in WEAPONS.items():
        asset = create_asset("DA_Weapon_" + key, WEAPONS_DIR, unreal.CSWeaponDefinition, factory)
        asset.set_editor_property("weapon_id", key.lower())
        asset.set_editor_property("display_name", unreal.Text(display))
        asset.set_editor_property("is_starter_weapon", False)
        for prop, value in stats.items():
            asset.set_editor_property(prop, value)
        EDITOR_ASSET.save_loaded_asset(asset)
        out[key] = asset
    return out


def make_items(weapons):
    ensure_dir(ITEMS_DIR)
    factory = data_asset_factory(unreal.CSItemDefinition)
    paths = []
    for (item_id, suffix, display, item_type, stackable, max_stack, count,
         weapon_key, ammo_id, armor, heal, mesh, scale, color) in ITEMS:
        asset = create_asset("DA_Item_" + suffix, ITEMS_DIR, unreal.CSItemDefinition, factory)
        asset.set_editor_property("item_id", item_id)
        asset.set_editor_property("display_name", unreal.Text(display))
        asset.set_editor_property("item_type", item_type)
        asset.set_editor_property("stackable", stackable)
        asset.set_editor_property("max_stack", max_stack)
        asset.set_editor_property("default_pickup_count", count)
        if weapon_key:
            asset.set_editor_property("weapon", weapons[weapon_key])
            asset.set_editor_property("ammo_item_id", ammo_id)
        asset.set_editor_property("armor_amount", float(armor))
        asset.set_editor_property("heal_amount", float(heal))
        asset.set_editor_property("world_mesh", EDITOR_ASSET.load_asset(mesh))
        asset.set_editor_property("world_mesh_scale", unreal.Vector(*scale))
        asset.set_editor_property("placeholder_color", unreal.LinearColor(color[0], color[1], color[2], 1.0))
        EDITOR_ASSET.save_loaded_asset(asset)
        paths.append("{0}/DA_Item_{1}.DA_Item_{1}".format(ITEMS_DIR, suffix))
    return paths


def place_loot():
    """Adds ACSPickupSpawnPoint markers to the test map if it has none yet."""
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    level_subsystem.load_level(MAP_PATH)

    existing = [a for a in actor_subsystem.get_all_level_actors()
                if isinstance(a, unreal.CSPickupSpawnPoint)]
    if existing:
        log("map already has {0} loot markers".format(len(existing)))
        return

    for item_id, x, y in LOOT_LAYOUT:
        marker = actor_subsystem.spawn_actor_from_class(
            unreal.CSPickupSpawnPoint, unreal.Vector(x, y, 30), unreal.Rotator(0, 0, 0))
        marker.set_actor_label("Loot_{0}_{1}_{2}".format(item_id, x, y))
        marker.set_editor_property("item_id", item_id)

    level_subsystem.save_current_level()
    log("placed {0} loot markers".format(len(LOOT_LAYOUT)))


def make_lighting_dynamic():
    """Lights movable: Lumen lights the map at runtime, so there is nothing to
    bake, and a packaged build no longer shows LIGHTING NEEDS TO BE REBUILT."""
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    level_subsystem.load_level(MAP_PATH)

    changed = 0
    for actor in actor_subsystem.get_all_level_actors():
        if isinstance(actor, (unreal.DirectionalLight, unreal.SkyLight)):
            root = actor.get_editor_property("root_component")
            if root and root.get_editor_property("mobility") != unreal.ComponentMobility.MOVABLE:
                root.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
                changed += 1
    if changed:
        level_subsystem.save_current_level()
    log("lighting: {0} light(s) switched to movable".format(changed))


MENU_MAP_PATH = MAPS_DIR + "/Lvl_MainMenu"


def make_menu_map():
    """Empty map that only hosts the main menu (ACSMenuGameMode, no pawn)."""
    ensure_dir(MAPS_DIR)
    if EDITOR_ASSET.does_asset_exist(MENU_MAP_PATH):
        log("reuse  {0}".format(MENU_MAP_PATH))
        return

    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    level_subsystem.new_level(MENU_MAP_PATH)

    world = unreal.EditorLevelLibrary.get_editor_world()
    if world:
        settings = world.get_world_settings()
        settings.set_editor_property("default_game_mode", unreal.CSMenuGameMode.static_class())

    level_subsystem.save_current_level()
    log("created " + MENU_MAP_PATH)


def main():
    log("Stage 1 content bootstrap starting")
    actions = make_input_actions()
    imc = make_mapping_context(actions)
    config = make_input_config(actions, imc)
    character_bp = make_character_blueprint(config)
    gamemode_bp = make_gamemode_blueprint(character_bp)
    make_test_map(gamemode_bp)
    make_starter_pistol()

    weapons = make_weapons()
    item_paths = make_items(weapons)
    place_loot()
    make_lighting_dynamic()
    make_menu_map()
    log("ITEM REGISTRY ORDER (must match DefaultGame.ini):")
    for p in item_paths:
        log("  " + p)
    unreal.EditorAssetLibrary.save_directory("/Game", only_if_is_dirty=True, recursive=True)
    log("done")


main()
