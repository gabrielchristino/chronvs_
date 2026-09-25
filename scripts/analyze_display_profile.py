"""Summarize saved display_perf windows; never opens a serial port or uploads."""
import argparse
import json
from pathlib import Path
import re
import sys


ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
REQUIRED = (
    "window_ms", "frames", "refresh_avg_ms", "refresh_max_ms", "over20",
    "flush_avg_us", "px_avg", "internal_free", "dma_largest",
)
TOUCH = (
    "touch_reads", "touch_read_max_us", "touch_gap_max_us",
    "interaction_frames", "frame_gap_max_us", "input_refresh_max_us",
)
WATCH_TIMES = tuple("watch_" + name + "_us" for name in (
    "setup", "background", "geometry", "case", "mother", "minutes", "hours",
    "weekday", "temperature", "seconds", "marker"))
WATCH = ("watch_frames", "watch_slices") + WATCH_TIMES


def parse(text):
    """Group by reported optimization, rejecting incomplete numeric windows."""
    groups = {}
    optimization = "unknown"
    rejected = 0
    for line in ANSI.sub("", text).splitlines():
        if "display_perf:" not in line:
            continue
        payload = line.split("display_perf:", 1)[1].strip()
        banner = re.search(r"LVGL optimization=(\S+)", payload)
        if banner:
            optimization = banner[1]
            continue
        if "window_ms" not in payload:
            continue
        tokens = payload.split()
        fields = {}
        valid = True
        for token in tokens:
            match = re.fullmatch(r"([a-z_0-9]+)=(\d+)", token)
            if not match or match[1] in fields:
                valid = False
                break
            fields[match[1]] = int(match[2])
        has_touch = any(key in fields for key in TOUCH)
        has_watch = any(key in fields for key in WATCH)
        if (not valid or not all(key in fields for key in REQUIRED)
                or not fields.get("window_ms") or not fields.get("frames")
                or fields.get("over20", 0) > fields.get("frames", 0)
                or fields.get("refresh_avg_ms", 0) > fields.get("refresh_max_ms", 0)
                or (has_touch and not all(key in fields for key in TOUCH))
                or fields.get("interaction_frames", 0) > fields.get("frames", 0)
                or (has_watch and not all(key in fields for key in WATCH))
                or fields.get("watch_frames", 0) > fields.get("frames", 0)
                or fields.get("watch_frames", 0) > fields.get("watch_slices", 0)):
            rejected += 1
            continue
        groups.setdefault(optimization, []).append(fields)
    return groups, rejected


def summarize(windows):
    frames = sum(row["frames"] for row in windows)
    duration = sum(row["window_ms"] for row in windows)
    result = {
        "windows": len(windows), "observed_window_ms": duration, "frames": frames,
        "refreshes_per_observed_second": round(frames * 1000 / duration, 3),
        "over20": sum(row["over20"] for row in windows),
        "refresh_max_ms": max(row["refresh_max_ms"] for row in windows),
        "internal_free_min_bytes": min(row["internal_free"] for row in windows),
        "dma_largest_min_bytes": min(row["dma_largest"] for row in windows),
    }
    # Firmware averages are truncated integers; reconstruct approximate sums.
    for key in ("refresh_avg_ms", "flush_avg_us", "px_avg"):
        result[key + "_approx"] = round(
            sum(row[key] * row["frames"] for row in windows) / frames, 3)
    touch = [row for row in windows if "touch_reads" in row]
    result["touch_windows"] = len(touch)
    for key in TOUCH:
        values = [row[key] for row in touch]
        result[key] = (sum(values) if key in ("touch_reads", "interaction_frames")
                       else max(values)) if values else None
    watch = [row for row in windows if "watch_frames" in row]
    result["watch_windows"] = len(watch)
    watch_frames = sum(row["watch_frames"] for row in watch)
    result["watch_frames"] = watch_frames if watch else None
    result["watch_slices"] = sum(row["watch_slices"] for row in watch) if watch else None
    result["watch_section_avg_us"] = {
        key: round(sum(row[key] for row in watch) / watch_frames, 3)
        if watch_frames else None for key in WATCH_TIMES
    }
    return result


def read_log(path):
    data = path.read_bytes()
    encoding = "utf-16" if data.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig"
    return data.decode(encoding, errors="replace")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", type=Path, nargs="+", help="one file per scenario")
    args = parser.parse_args(argv)
    reports = []
    invalid = False
    for path in args.logs:
        try:
            groups, rejected = parse(read_log(path))
        except OSError as error:
            print(f"{path}: {error.strerror}", file=sys.stderr)
            invalid = True
            continue
        if not groups:
            print(f"{path}: no complete display_perf windows", file=sys.stderr)
            invalid = True
        if rejected:
            print(f"{path}: rejected {rejected} incomplete/invalid windows", file=sys.stderr)
            invalid = True
        for optimization, windows in groups.items():
            reports.append({"file": str(path), "lvgl_optimization": optimization,
                            **summarize(windows)})
    print(json.dumps(reports, indent=2, ensure_ascii=True))
    return 1 if invalid else 0


if __name__ == "__main__":
    sys.exit(main())
