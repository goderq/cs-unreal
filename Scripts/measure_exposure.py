"""
v2.0 phase 4 (AUDIT C20): exposure numbers for map screenshots.

    python Scripts/measure_exposure.py Saved/CSTest/before Saved/CSTest

For every tour_*.png: mean brightness (0-255, Rec.709 luma of the sRGB
values), the share of blown-out pixels (all channels >= 250) and of crushed
ones (luma <= 6), then the same per map. Rough targets for a readable
shooter: mean 90-140, blown-out under 2 %, crushed under 5 %. The last shot
of a tour is the overview from above and is kept out of the map average.
Needs Pillow (pip install Pillow).
"""

import glob
import os
import re
import sys

from PIL import Image


def measure(path):
    img = Image.open(path).convert("RGB")
    img.thumbnail((480, 270))
    px = list(img.get_flattened_data() if hasattr(img, "get_flattened_data") else img.getdata())
    luma = [0.2126 * r + 0.7152 * g + 0.0722 * b for r, g, b in px]
    blown = sum(1 for r, g, b in px if r >= 250 and g >= 250 and b >= 250)
    crushed = sum(1 for y in luma if y <= 6)
    return sum(luma) / len(luma), 100.0 * blown / len(px), 100.0 * crushed / len(px)


def main(folders):
    for folder in folders:
        print("== %s" % folder)
        maps = {}
        for path in sorted(glob.glob(os.path.join(folder, "tour_*.png"))):
            m = re.match(r"tour_(.+)_(\d+)\.png", os.path.basename(path))
            if m:
                maps.setdefault(m.group(1), []).append(path)
        for name, shots in maps.items():
            rows = [measure(p) for p in shots]
            for p, (mean, blown, crushed) in zip(shots, rows):
                print("  %-28s mean %5.1f  blown %5.1f%%  crushed %5.1f%%" % (os.path.basename(p), mean, blown, crushed))
            ground = rows[:-1] or rows
            print("  %-28s mean %5.1f  blown %5.1f%%  crushed %5.1f%%  (ground shots)" % (
                name + " average",
                sum(r[0] for r in ground) / len(ground),
                sum(r[1] for r in ground) / len(ground),
                sum(r[2] for r in ground) / len(ground)))


if __name__ == "__main__":
    main(sys.argv[1:] or ["Saved/CSTest"])
