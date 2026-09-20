"""
CS-Fusion content bootstrap.

Creates the Stage 1 content that cannot be expressed as C++ source:
  Content/Input/IA_*                Enhanced Input actions
  Content/Input/IMC_Default         Input Mapping Context with keyboard/mouse
  Content/Input/DA_CSInputConfig    UCSInputConfig data asset, fully wired
  Content/Characters/BP_CSCharacter Blueprint subclass of ACSCharacter
  Content/Maps/Lvl_Warehouse        Test map: floor, cover, 8 player starts

How to run
----------
1. Open the project in Unreal Editor 5.8.
2. Enable the Python plugin if it is not already on:
   Edit > Plugins > search "Python Editor Script Plugin" > enable > restart.
3. Window > Output Log, switch the command dropdown from "Cmd" to "Python".
4. Paste:

       exec(open(r"<ProjectDir>/Scripts/bootstrap_content.py").read())

   or use:  py "<ProjectDir>/Scripts/bootstrap_content.py"

The script is idempotent: assets that already exist are reused, not duplicated.
"""

import unreal

ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EDITOR_ASSET = unreal.EditorAssetLibrary

INPUT_DIR = "/Game/Input"
CHAR_DIR = "/Game/Characters"
MAPS_DIR = "/Game/Maps"


def log(message):
    unreal.log("[CS-Bootstrap] {0}".format(message))


def ensure_dir(path):
    if not EDITOR_ASSET.does_directory_exist(path):
        EDITOR_ASSET.make_directory(path)


def create_asset(name, package_path, asset_class, factory):
    full_path = "{0}/{1}".format(package_path, name)
    if EDITOR_ASSET.does_asset_exist(full_path):
        log("reuse  {0}".format(full_path))
        return EDITOR_ASSET.load_asset(full_path)
    log("create {0}".format(full_path))
    return ASSET_TOOLS.create_asset(name, package_path, asset_class, factory)


# ---------------------------------------------------------------------------
# Input actions
# ---------------------------------------------------------------------------

def make_input_actions():
    ensure_dir(INPUT_DIR)
    factory = unreal.InputActionFactory()

    spec = {
        "IA_Move":             unreal.InputActionValueType.AXIS2_D,
        "IA_Look":             unreal.InputActionValueType.AXIS2_D,
        "IA_Jump":             unreal.InputActionValueType.BOOLEAN,
        "IA_Sprint":           unreal.InputActionValueType.BOOLEAN,
        "IA_Crouch":           unreal.InputActionValueType.BOOLEAN,
        "IA_Fire":             unreal.InputActionValueType.BOOLEAN,
        "IA_Aim":              unreal.InputActionValueType.BOOLEAN,
        "IA_Reload":           unreal.InputActionValueType.BOOLEAN,
        "IA_Interact":         unreal.InputActionValueType.BOOLEAN,
        "IA_ToggleInventory":  unreal.InputActionValueType.BOOLEAN,
        "IA_PauseMenu":        unreal.InputActionValueType.BOOLEAN,
        "IA_Scoreboard":       unreal.InputActionValueType.BOOLEAN,
    }

    actions = {}
    for name, value_type in spec.items():
        asset = create_asset(name, INPUT_DIR, unreal.InputAction, factory)
        asset.set_editor_property("value_type", value_type)
        EDITOR_ASSET.save_loaded_asset(asset)
        actions[name] = asset
    return actions


# ---------------------------------------------------------------------------
# Mapping context
# ---------------------------------------------------------------------------

def _mapping(context, action, key, negate=False, swizzle=None):
    entry = context.map_key(action, unreal.InputCoreTypes.__dict__.get(key, unreal.Key(key)))
    modifiers = []
    if swizzle is not None:
        swizzle_mod = unreal.InputModifierSwizzleAxis()
        swizzle_mod.set_editor_property("order", swizzle)
        modifiers.append(swizzle_mod)
    if negate:
        modifiers.append(unreal.InputModifierNegate())
    if modifiers:
        entry.set_editor_property("modifiers", modifiers)
    return entry


def make_mapping_context(actions):
    factory = unreal.InputMappingContextFactory()
    imc = create_asset("IMC_Default", INPUT_DIR, unreal.InputMappingContext, factory)

    # Start clean so re-running does not stack duplicate bindings.
    imc.set_editor_property("mappings", [])

    yxz = unreal.InputAxisSwizzle.YXZ

    # Movement: WASD -> Axis2D (X = strafe, Y = forward)
    _mapping(imc, actions["IA_Move"], "W", negate=False, swizzle=yxz)
    _mapping(imc, actions["IA_Move"], "S", negate=True,  swizzle=yxz)
    _mapping(imc, actions["IA_Move"], "D")
    _mapping(imc, actions["IA_Move"], "A", negate=True)

    # Look: mouse XY
    imc.map_key(actions["IA_Look"], unreal.Key("Mouse2D"))

    imc.map_key(actions["IA_Jump"],            unreal.Key("SpaceBar"))
    imc.map_key(actions["IA_Sprint"],          unreal.Key("LeftShift"))
    imc.map_key(actions["IA_Crouch"],          unreal.Key("LeftControl"))
    imc.map_key(actions["IA_Fire"],            unreal.Key("LeftMouseButton"))
    imc.map_key(actions["IA_Aim"],             unreal.Key("RightMouseButton"))
    imc.map_key(actions["IA_Reload"],          unreal.Key("R"))
    imc.map_key(actions["IA_Interact"],        unreal.Key("E"))
    imc.map_key(actions["IA_ToggleInventory"], unreal.Key("Tab"))
    imc.map_key(actions["IA_PauseMenu"],       unreal.Key("Escape"))
    imc.map_key(actions["IA_Scoreboard"],      unreal.Key("BackSpace"))

    EDITOR_ASSET.save_loaded_asset(imc)
    return imc


# ---------------------------------------------------------------------------
# Input config data asset
# ---------------------------------------------------------------------------

def make_input_config(actions, imc):
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.CSInputConfig)
    config = create_asset("DA_CSInputConfig", INPUT_DIR, unreal.CSInputConfig, factory)

    config.set_editor_property("default_mapping_context", imc)
    config.set_editor_property("mapping_priority", 0)
    for name, asset in actions.items():
        # UPROPERTY IA_Move -> python property ia_move
        config.set_editor_property(name.lower(), asset)

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


# ---------------------------------------------------------------------------
# Test map
# ---------------------------------------------------------------------------

def _spawn_box(location, scale, label):
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.StaticMeshActor, location, unreal.Rotator(0, 0, 0))
    actor.set_actor_label(label)
    actor.set_actor_scale3d(scale)
    mesh = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Cube.Cube")
    actor.static_mesh_component.set_static_mesh(mesh)
    actor.set_mobility(unreal.ComponentMobility.STATIC)
    return actor


def make_test_map():
    ensure_dir(MAPS_DIR)
    map_path = "{0}/Lvl_Warehouse".format(MAPS_DIR)

    if EDITOR_ASSET.does_asset_exist(map_path):
        log("reuse  {0} (delete it first to regenerate)".format(map_path))
        return map_path

    unreal.EditorLevelLibrary.new_level(map_path)

    # Floor: 60m x 60m
    _spawn_box(unreal.Vector(0, 0, -50), unreal.Vector(60, 60, 1), "Floor")

    # Perimeter walls
    _spawn_box(unreal.Vector(0,  3000, 200), unreal.Vector(60, 1, 6), "Wall_N")
    _spawn_box(unreal.Vector(0, -3000, 200), unreal.Vector(60, 1, 6), "Wall_S")
    _spawn_box(unreal.Vector( 3000, 0, 200), unreal.Vector(1, 60, 6), "Wall_E")
    _spawn_box(unreal.Vector(-3000, 0, 200), unreal.Vector(1, 60, 6), "Wall_W")

    # Central structure + cover, arranged so both bases have equal sightlines.
    _spawn_box(unreal.Vector(0, 0, 100), unreal.Vector(8, 8, 3), "Mid_Block")
    for x, y in [(1200, 600), (-1200, 600), (1200, -600), (-1200, -600),
                 (0, 1400), (0, -1400), (1800, 0), (-1800, 0)]:
        _spawn_box(unreal.Vector(x, y, 45), unreal.Vector(3, 3, 1.9),
                   "Cover_{0}_{1}".format(x, y))

    # Player starts: 4 per base. Names are sorted by ACSGameMode, so the
    # numbering here decides the spawn index each Photon player id maps to.
    for i in range(4):
        start = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.PlayerStart, unreal.Vector(-2500, -450 + i * 300, 100),
            unreal.Rotator(0, 0, 0))
        start.set_actor_label("PlayerStart_A{0}".format(i))
    for i in range(4):
        start = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.PlayerStart, unreal.Vector(2500, -450 + i * 300, 100),
            unreal.Rotator(0, 180, 0))
        start.set_actor_label("PlayerStart_B{0}".format(i))

    # Lighting so the level is not pitch black.
    unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0, 0, 1500), unreal.Rotator(-45, -35, 0))
    unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(0, 0, 1200), unreal.Rotator(0, 0, 0))
    unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))

    unreal.EditorLevelLibrary.save_current_level()
    log("created {0}".format(map_path))
    return map_path


def main():
    log("starting Stage 1 content bootstrap")
    actions = make_input_actions()
    imc = make_mapping_context(actions)
    config = make_input_config(actions, imc)
    make_character_blueprint(config)
    make_test_map()
    log("done. Set the map's GameMode override to CSGameMode and assign "
        "BP_CSCharacter as the Default Pawn Class in World Settings.")


main()
