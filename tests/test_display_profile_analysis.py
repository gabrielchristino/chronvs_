"""Synthetic diagnostic windows; these are not device measurements."""
import contextlib
import io
from pathlib import Path
import runpy
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/analyze_display_profile.py"
MODULE = runpy.run_path(str(SCRIPT))


def window(**changes):
    values = dict(window_ms=2000, frames=2, refresh_avg_ms=10, refresh_max_ms=20,
                  over20=0, flush_avg_us=5000, px_avg=100, internal_free=40000,
                  dma_largest=16000)
    values.update(changes)
    return "I (1234) display_perf: " + " ".join(f"{k}={v}" for k, v in values.items())


class ProfileAnalysisTest(unittest.TestCase):
    def test_case_breakdown_coverage_and_consistency(self):
        counters = {key: 100 for key in MODULE["WATCH_TIMES"]}
        old = window(watch_frames=2, watch_slices=42, **counters)
        new = old + " watch_rings_us=80 watch_dates_us=20"
        groups, bad = MODULE["parse"](old + "\n" + new)
        result = MODULE["summarize"](groups["unknown"])
        self.assertEqual(bad, 0)
        self.assertEqual(result["watch_case_detail_windows"], 1)
        self.assertEqual(result["watch_case_detail_frames"], 2)
        self.assertEqual(result["watch_case_detail_avg_us"],
                         {"watch_rings_us": 40, "watch_dates_us": 10})
        self.assertEqual(result["watch_section_avg_us"]["watch_case_us"], 50)
        for row in (old + " watch_rings_us=80", new.replace("watch_dates_us=20", "watch_dates_us=21"),
                    window() + " watch_rings_us=80 watch_dates_us=20"):
            self.assertEqual(MODULE["parse"](row), ({}, 1))
        groups, _ = MODULE["parse"](old)
        self.assertIsNone(MODULE["summarize"](groups["unknown"])["watch_case_detail_avg_us"]["watch_rings_us"])

    def test_watch_totals_use_watch_frames_not_all_frames_or_slices(self):
        counters = {key: 100 for key in MODULE["WATCH_TIMES"]}
        first = window(frames=10, watch_frames=2, watch_slices=40, **counters)
        second = window(frames=8, watch_frames=1, watch_slices=21,
                        **{key: 500 for key in counters})
        groups, bad = MODULE["parse"]("\n".join((first, window(), second)))
        result = MODULE["summarize"](groups["unknown"])
        self.assertEqual(bad, 0)
        self.assertEqual(result["watch_windows"], 2)
        self.assertEqual(result["watch_frames"], 3)
        self.assertEqual(result["watch_slices"], 61)
        self.assertEqual(result["watch_section_avg_us"]["watch_case_us"], 200)
        groups, bad = MODULE["parse"](window(watch_frames=0, watch_slices=0,
                                             **{key: 0 for key in counters}))
        self.assertIsNone(MODULE["summarize"](groups["unknown"])["watch_section_avg_us"]["watch_case_us"])
        for row in (window(watch_frames=1), window(watch_frames=3, watch_slices=50, **counters),
                    window(watch_frames=2, watch_slices=1, **counters)):
            self.assertEqual(MODULE["parse"](row), ({}, 1))

    def test_weighted_averages_duration_and_memory_minima(self):
        text = window() + "\n" + window(window_ms=3000, frames=8, refresh_avg_ms=30,
            refresh_max_ms=40, over20=5, flush_avg_us=10000, internal_free=30000,
            dma_largest=8000)
        groups, bad = MODULE["parse"](text)
        result = MODULE["summarize"](groups["unknown"])
        self.assertEqual(bad, 0)
        self.assertEqual(result["refresh_avg_ms_approx"], 26)
        self.assertEqual(result["flush_avg_us_approx"], 9000)
        self.assertEqual(result["refreshes_per_observed_second"], 2)
        self.assertEqual(result["over20"], 5)
        self.assertEqual(result["dma_largest_min_bytes"], 8000)
        self.assertEqual(result["internal_free_min_bytes"], 30000)
        self.assertIsNone(result["touch_gap_max_us"])

    def test_touch_extrema_and_legacy_coverage(self):
        touch = dict(touch_reads=60, touch_read_max_us=500, touch_gap_max_us=30000,
                     interaction_frames=1, frame_gap_max_us=40000, input_refresh_max_us=90000)
        groups, bad = MODULE["parse"]("\n".join([
            window(), window(**touch), window(**{**touch, "touch_gap_max_us": 60000})]))
        result = MODULE["summarize"](groups["unknown"])
        self.assertEqual(bad, 0)
        self.assertEqual(result["touch_windows"], 2)
        self.assertEqual(result["touch_reads"], 120)
        self.assertEqual(result["touch_gap_max_us"], 60000)

    def test_color_prefix_noise_and_distinct_builds(self):
        text = "unrelated log\nI (0) display_perf: enabled; LVGL optimization=-Og\n"
        text += "12:30:01 \x1b[32m" + window() + "\x1b[0m\n"
        text += "I (0) display_perf: enabled; LVGL optimization=-O2\n" + window()
        groups, bad = MODULE["parse"](text)
        self.assertEqual(set(groups), {"-Og", "-O2"})
        self.assertEqual(bad, 0)

    def test_rejects_corrupt_partial_and_inconsistent_rows(self):
        rows = [window(frames=0), window(window_ms=0), window(frames=-1),
                window(over20=3), window(refresh_avg_ms=50), window(touch_reads=1),
                window() + " frames=2", window().rsplit(" ", 1)[0], window() + " broken",
                "I (2) display_perf: window_ms=2000 frames=2"]
        groups, bad = MODULE["parse"]("\n".join(rows))
        self.assertEqual(groups, {})
        self.assertEqual(bad, len(rows))

    def test_cli_unicode_bom_errors_and_no_input_data(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "capture.log"
            for encoding in ("utf-8-sig", "utf-16"):
                log.write_text(window(), encoding=encoding)
                with contextlib.redirect_stdout(io.StringIO()) as output:
                    self.assertEqual(MODULE["main"]([str(log)]), 0)
                self.assertIn('"frames": 2', output.getvalue())
            for content in ("ordinary boot output", window() + "\n" + window(frames=0)):
                log.write_text(content, encoding="utf-8")
                with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(MODULE["main"]([str(log)]), 1)


if __name__ == "__main__":
    unittest.main()
