"""
Stage 7 content bootstrap: navigation for bots.

Adds a NavMeshBoundsVolume covering the whole Warehouse arena. The navmesh
itself is generated at runtime (RecastNavMesh RuntimeGeneration=Dynamic in
DefaultEngine.ini), so nothing needs baking and edits to the map stay valid.

    UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script=Scripts/bootstrap_stage7.py

Idempotent: does nothing if the volume already exists.
"""

import os
import unreal

MAP_PATH = "/Game/Maps/Lvl_Warehouse"
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
LOG_PATH = os.path.join(PROJECT_DIR, "Saved", "Logs", "bootstrap_stage7.txt")
lines = []


def log(msg):
    lines.append("[CS-Stage7] " + str(msg))
    unreal.log(lines[-1])


def main():
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    level.load_level(MAP_PATH)

    existing = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.NavMeshBoundsVolume)]
    if existing:
        log("map already has {0} NavMeshBoundsVolume(s)".format(len(existing)))
        return

    # The arena is 60 m x 60 m (walls at +-30 m); the default volume brush is
    # 2 m on a side, so scale 31 x 31 x 4 covers it with margin, 8 m tall.
    volume = actors.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(0, 0, 200), unreal.Rotator(0, 0, 0))
    volume.set_actor_label("NavBounds_Arena")
    volume.set_actor_scale3d(unreal.Vector(31.0, 31.0, 4.0))
    level.save_current_level()
    log("added NavMeshBoundsVolume covering the arena")


try:
    main()
finally:
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
    with open(LOG_PATH, "w") as f:
        f.write("\n".join(lines))
