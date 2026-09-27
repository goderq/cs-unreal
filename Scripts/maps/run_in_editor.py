"""
Runs a map pipeline script inside the full editor and quits: some editor APIs (mesh LOD
reduction through StaticMeshEditorSubsystem) are not available in the Python commandlet.

    $env:CS_V21_SCRIPT = "Scripts/maps/import_assets.py"
    UnrealEditor.exe CSFusion.uproject -ExecutePythonScript="Scripts/maps/run_in_editor.py" -unattended -nosplash
"""
import os
import traceback
import unreal

root = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
script = os.path.join(root, os.environ.get("CS_V21_SCRIPT", "Scripts/maps/import_assets.py"))
try:
    code = open(script, encoding="utf-8").read()
    exec(compile(code, script, "exec"), {"__name__": "__main__", "__file__": script})
except Exception:
    unreal.log_error("run_in_editor: %s failed\n%s" % (script, traceback.format_exc()))
unreal.SystemLibrary.quit_editor()
