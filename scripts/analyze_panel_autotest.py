"""Summarize a complete on-device tray gesture run; no serial or upload access."""
import argparse
import json
from pathlib import Path
import re

GESTURES = ("quick_open", "quick_close", "quick_cancel_open", "launcher_open",
            "launcher_cancel_close", "launcher_close", "launcher_cancel_open")
EXPECTED = [(name, speed, repeat) for repeat in range(1, 4)
            for speed in ("slow", "fast") for name in GESTURES]


def summarize(path):
    text = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", path.read_text(encoding="utf-8", errors="replace"))
    results = []
    done = None
    weather_started = False
    weather = None
    for line in text.splitlines():
        if "panel_test:" not in line:
            continue
        message = line.split("panel_test:", 1)[1].strip()
        fields = dict(re.findall(r"(\w+)=([^\s]+)", message))
        if message.startswith("RESULT "):
            results.append({key: int(value) if re.fullmatch(r"-?\d+", value) else value
                            for key, value in fields.items()})
        if message.startswith("DONE "):
            done = {key: int(value) for key, value in fields.items()}
        if message.startswith("WEATHER BEGIN"):
            weather_started = True
        if message.startswith("WEATHER pass="):
            weather = {key: int(value) for key, value in fields.items()}
    complete = bool(done and done.get("failed") == 0 and done.get("completed") == done.get("total") == len(results) == len(EXPECTED)
                    and done.get("physical_touch_restored") == 1
                    and [row["n"] for row in results] == list(range(1, len(results) + 1))
                    and [(row.get("name"), row.get("speed"), row.get("repeat")) for row in results] == EXPECTED
                    and (not weather_started or weather and weather.get("pass") == 1
                         and weather.get("state") == 2 and weather.get("fetching_seen") == 1
                         and weather.get("timeout") == 0)
                    and all(row["pass"] == row["preferences_ok"] == 1 and row["frames"] > 0 for row in results))
    groups = {}
    for row in results:
        key = row["name"] + "/" + row["speed"]
        group = groups.setdefault(key, {"runs": 0, "passed": 0, "frames": 0,
            "render_sum": 0, "flush_sum": 0, "frame_max_us": 0, "touch_gap_max_ms": 0,
            "press_ms_min": row["press_ms"], "press_ms_max": 0})
        group["runs"] += 1
        group["passed"] += row["pass"]
        group["frames"] += row["frames"]
        group["render_sum"] += row["render_avg_us"] * row["frames"]
        group["flush_sum"] += row["flush_avg_us"] * row["frames"]
        group["frame_max_us"] = max(group["frame_max_us"], row["frame_max_us"])
        group["touch_gap_max_ms"] = max(group["touch_gap_max_ms"], row["touch_gap_max_ms"])
        group["press_ms_min"] = min(group["press_ms_min"], row["press_ms"])
        group["press_ms_max"] = max(group["press_ms_max"], row["press_ms"])
    for group in groups.values():
        for name in ("render", "flush"):
            group[name + "_avg_us_approx"] = round(group.pop(name + "_sum") / max(1, group["frames"]), 2)
    return {"file": str(path), "complete": complete, "done": done, "weather": weather,
            "groups": groups, "results": results}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    report = summarize(args.log)
    print(json.dumps(report, indent=2))
    raise SystemExit(0 if report["complete"] else 1)
