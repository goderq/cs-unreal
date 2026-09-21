"""
Copies the Epic Games template content this project uses from YOUR local
Unreal Engine install into Content/.

Why a script and not files in git: the Mannequin characters and animations,
the template weapons and the prototype grid materials are Epic content under
the Unreal Engine EULA. Using them in a UE project and shipping them cooked
in a game is allowed; redistributing the source .uasset files in a public
repository is not. Every UE install carries them, so each developer copies
them locally with this script (see docs/ASSETS.md).

Usage (any Python 3, e.g. the one bundled with UE):
    python Scripts/copy_epic_content.py [path/to/UE_5.8]

Default engine path: C:/Program Files/Epic Games/UE_5.8
Existing files are never overwritten.
"""

import os
import shutil
import sys

PACKS = [
    # (template resource pack, destination under Content/)
    ("High/Characters/Content", "Characters"),
    ("Standard/Weapons/Content", "Weapons"),
    ("High/LevelPrototyping/Content", "LevelPrototyping"),
]


def main():
    engine = sys.argv[1] if len(sys.argv) > 1 else r"C:/Program Files/Epic Games/UE_5.8"
    resources = os.path.join(engine, "Templates", "TemplateResources")
    project = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    content = os.path.join(project, "Content")

    if not os.path.isdir(resources):
        sys.exit("TemplateResources not found under " + engine + " - pass your UE 5.8 folder as the argument.")

    copied = skipped = 0
    for source_rel, dest_rel in PACKS:
        source = os.path.join(resources, source_rel)
        dest = os.path.join(content, dest_rel)
        for root, _, files in os.walk(source):
            for name in files:
                src = os.path.join(root, name)
                dst = os.path.join(dest, os.path.relpath(src, source))
                if os.path.exists(dst):
                    skipped += 1
                    continue
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(src, dst)
                copied += 1
    print("Epic content: copied {0} file(s), {1} already present.".format(copied, skipped))


if __name__ == "__main__":
    main()
