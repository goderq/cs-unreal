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


def main():
    log("Stage 1 content bootstrap starting")
    actions = make_input_actions()
    imc = make_mapping_context(actions)
    config = make_input_config(actions, imc)
    character_bp = make_character_blueprint(config)
    gamemode_bp = make_gamemode_blueprint(character_bp)
    make_test_map(gamemode_bp)
    make_starter_pistol()
    unreal.EditorAssetLibrary.save_directory("/Game", only_if_is_dirty=True, recursive=True)
    log("done")


main()
