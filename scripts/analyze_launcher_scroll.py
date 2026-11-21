"""Summarize saved hardware scroll diagnostics, without serial or upload access."""
import argparse
import json
from pathlib import Path
import re

PATHS = ("slow_up", "fast_up", "hold_up", "reverse_up", "slow_down", "fast_down", "hold_down", "reverse_down")
COST_MODES = ("full", "no_icons", "no_labels", "no_badges", "no_fade", "no_clip", "background", "square_background", "full_check")


def summarize(text):
    text = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", text)
    results = []
    costs = []
    cost_ready = None
    cost_done = None
    done = idle = None
    for line in text.splitlines():
        if "scroll_test:" not in line:
            continue
        fields = dict(re.findall(r"(\w+)=([^\s]+)", line))
        fields = {key: int(value) if value.isdigit() else value for key, value in fields.items()}
        message = line.split("scroll_test:", 1)[1].strip()
        if message.startswith("COST_RESULT "):
            costs.append(fields)
        elif message.startswith("COST_READY "):
            cost_ready = fields
        elif message.startswith("COST_DONE "):
            cost_done = fields
        elif message.startswith("RESULT "):
            results.append(fields)
        elif message.startswith("DONE "):
            done = fields
        elif message.startswith("IDLE "):
            idle = fields
    groups = {}
    for result in results:
        groups.setdefault(result["name"], []).append(result)
    grouped = {}
    for name, cases in groups.items():
        frames = sum(case["frames"] for case in cases)
        grouped[name] = {
            "cases": len(cases),
            "render_ms": round(sum(c["render_avg_us"] * c["frames"] for c in cases) / max(1, frames) / 1000, 2),
            "flush_ms": round(sum(c["flush_avg_us"] * c["frames"] for c in cases) / max(1, frames) / 1000, 2),
            "frame_max_ms": max(c["frame_max_us"] for c in cases) / 1000,
            "touch_gap_max_ms": max(c["touch_gap_max_ms"] for c in cases),
            "pixels_per_frame": round(sum(c["px_avg"] * c["frames"] for c in cases) / max(1, frames)),
        }
    errors = re.findall(r".*(?:Guru Meditation|assert failed|panic|ABORT|SPI.*[Ee]rror|drain.*[Ff]ail).*", text)
    modes = (("full_reference",) + COST_MODES) if cost_ready and cost_ready.get("modes") == 10 else COST_MODES
    complete = bool(done and done.get("completed") == done.get("total") == len(results) == 24
                    and done.get("failed") == 0 and done.get("physical_touch_restored") == 1
                    and done.get("wraps_up", 0) and done.get("wraps_down", 0)
                    and [r["n"] for r in results] == list(range(1, 25))
                    and [(r["name"], r["repeat"]) for r in results]
                    == [(name, repeat) for repeat in range(1, 4) for name in PATHS]
                    and all(r["pass"] == 1 and r["coast"] == 0 and r["rows"] == 8 for r in results)
                    and len(groups) == 8 and all(len(cases) == 3 for cases in groups.values())
                    and idle and idle.get("pass") == 1 and not errors
                    and (not cost_ready or cost_done and cost_done.get("pass") == 1
                         and cost_done.get("groups") == cost_done.get("expected") == len(costs) == len(modes)*3
                         and [(r["name"], r["repeat"]) for r in costs]
                         == [(name, repeat) for repeat in range(1, 4) for name in modes]
                         and all(r["pass"] == 1 and r["frames"] == 32 and r["px_avg"] == 412*412 for r in costs)))
    cost_groups = {}
    for name in modes:
        selected = [r for r in costs if r["name"] == name]
        if selected:
            cost_groups[name] = {key: round(sum(r[field] for r in selected) / len(selected) / 1000, 3)
                                 for key, field in (("render_ms", "render_avg_us"), ("flush_ms", "flush_avg_us"), ("prepare_ms", "prepare_avg_us"))}
            cost_groups[name]["frame_max_ms"] = max(r["frame_max_us"] for r in selected) / 1000
            if "full" in cost_groups:
                cost_groups[name]["render_saved_ms"] = round(cost_groups["full"]["render_ms"] - cost_groups[name]["render_ms"], 3)
    minima = {key: min(r[key] for r in results) for key in
              ("internal_min", "dma_min", "lvgl_min", "lvgl_largest_min")} if results else {}
    return {"complete": complete, "done": done, "idle": idle, "observed_memory_minima": minima,
            "groups": grouped, "errors": errors, "results": results,
            "cost_done": cost_done, "cost_groups": cost_groups, "cost_results": costs}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    summary = summarize(args.log.read_text(encoding="utf-8", errors="replace"))
    rendered = json.dumps(summary, indent=2)
    if args.output:
        args.output.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    raise SystemExit(0 if summary["complete"] else 1)
