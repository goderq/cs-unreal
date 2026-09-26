"""
v2.0 phase 7: the most expensive timers in an UnrealInsights timer-statistics
CSV (Scripts/analyze_trace.ps1). Per frame = total over the region divided by
the number of frames (FEngineLoop::Tick calls on the game thread, or the
frame count passed by analyze_trace.ps1 for the other threads).

    python Scripts/summarize_timers.py <stats.csv> [top]
"""

import csv
import sys


def number(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return 0.0


FRAMES = 0


def main(path, top):
    with open(path, newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        print("  (no rows)")
        return
    keys = rows[0].keys()
    name_key = next(k for k in keys if k.lower() in ("name", "timername", "timer"))
    count_key = next((k for k in keys if k.lower() in ("count", "instancecount")), None)
    incl_key = next((k for k in keys if k.lower() in ("incl", "inclusive", "totalinclusivetime")), None)
    excl_key = next((k for k in keys if k.lower() in ("excl", "exclusive", "totalexclusivetime")), None)

    frames = FRAMES
    if not frames:
        for row in rows:
            if row[name_key] == "FEngineLoop::Tick":   # once a frame ("Frame" appears twice)
                frames = int(number(row.get(count_key, 0)))
    frames = frames or 1
    unit = 1000.0  # the exporter writes seconds

    rows.sort(key=lambda r: number(r.get(excl_key or incl_key)), reverse=True)
    print("  frames in the region: %d" % frames)
    print("  %-58s %9s %12s %12s" % ("timer", "calls/fr", "excl ms/fr", "incl ms/fr"))
    for row in rows[:top]:
        calls = number(row.get(count_key)) / frames if count_key else 0.0
        excl = number(row.get(excl_key)) * unit / frames if excl_key else 0.0
        incl = number(row.get(incl_key)) * unit / frames if incl_key else 0.0
        print("  %-58s %9.1f %12.3f %12.3f" % (row[name_key][:58], calls, excl, incl))


if __name__ == "__main__":
    # Optional third argument: the frame count (the render and GPU exports
    # have no FEngineLoop::Tick; analyze_trace.ps1 passes the game thread's).
    FRAMES = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 25)
