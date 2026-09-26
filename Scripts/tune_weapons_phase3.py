"""
Phase 3 (docs/AUDIT.md B8): per-weapon numbers for the authority's shot model
(Weapons/CSShotModel.h) - running and jumping penalties, the length of the
recoil pattern and how fast a series recovers - plus how fast the sights come
up in first person (looks only).

    UnrealEditor-Cmd.exe CSFusion.uproject -run=pythonscript -script=Scripts/tune_weapons_phase3.py

Idempotent: the values are set in place on the weapon data assets.
"""

import os
import unreal

EAL = unreal.EditorAssetLibrary
WEAPONS_DIR = "/Game/Weapons"

# move spread (deg at a run), jump spread (deg), pattern shots, recovery (s),
# time to raise the sights (s, the look only)
TUNING = {
    "AK47":          (4.5,  9.0,  9, 0.50, 0.18),
    "M4":            (4.0,  8.0,  8, 0.45, 0.16),
    "SMG":           (2.0,  5.0, 12, 0.40, 0.13),
    "Shotgun":       (1.5,  4.0,  1, 0.60, 0.20),
    "Sniper":        (12.0, 20.0, 1, 1.00, 0.28),
    "StarterPistol": (1.6,  5.0,  5, 0.35, 0.11),
}

lines = []
for key, (move, jump, shots, recovery, aim) in TUNING.items():
    path = "%s/DA_Weapon_%s" % (WEAPONS_DIR, key)
    weapon = EAL.load_asset(path)
    if not weapon:
        lines.append("MISSING " + path)
        continue
    weapon.set_editor_property("move_spread_degrees", move)
    weapon.set_editor_property("jump_spread_degrees", jump)
    weapon.set_editor_property("recoil_pattern_shots", shots)
    weapon.set_editor_property("recoil_recovery_seconds", recovery)
    weapon.set_editor_property("aim_seconds", aim)
    EAL.save_loaded_asset(weapon, only_if_is_dirty=False)
    lines.append("%s: move %.1f, jump %.1f, pattern %d, recovery %.2f, aim %.2f" % (key, move, jump, shots, recovery, aim))

log = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "Logs", "tune_weapons_phase3.txt")
with open(log, "w") as f:
    f.write("\n".join(lines))
