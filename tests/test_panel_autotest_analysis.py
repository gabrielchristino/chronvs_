"""Reject incomplete or misidentified gesture and HTTPS evidence."""
from pathlib import Path
import runpy
import tempfile
import unittest

analyze = runpy.run_path(str(Path(__file__).resolve().parents[1] /
                            "scripts/analyze_panel_autotest.py"))["summarize"]


def run_lines():
    gestures = ("quick_open", "quick_close", "quick_cancel_open", "launcher_open",
                "launcher_cancel_close", "launcher_close", "launcher_cancel_open")
    lines = []
    for repeat in range(1, 4):
        for speed in ("slow", "fast"):
            for name in gestures:
                lines.append(f"I (0) panel_test: RESULT n={len(lines)+1} name={name} "
                             f"speed={speed} repeat={repeat} pass=1 preferences_ok=1 frames=2 "
                             "render_avg_us=100 flush_avg_us=20 frame_max_us=150 "
                             "touch_gap_max_ms=10 press_ms=300")
    return lines


DONE = "I (1) panel_test: DONE completed=42 total=42 failed=0 physical_touch_restored=1"


class PanelAnalysisTests(unittest.TestCase):
    def report(self, lines):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "run.log"
            path.write_text("\n".join(lines), encoding="utf-8")
            return analyze(path)

    def test_complete_run(self):
        report = self.report(run_lines() + [DONE])
        self.assertTrue(report["complete"])
        self.assertEqual(len(report["groups"]), 14)

    def test_missing_done_or_result(self):
        self.assertFalse(self.report(run_lines())["complete"])
        self.assertFalse(self.report(run_lines()[:-1] + [DONE])["complete"])

    def test_shorter_declared_total_does_not_pass(self):
        self.assertFalse(self.report(run_lines()[:14] +
                                    [DONE.replace("42", "14")])["complete"])

    def test_wrong_case_or_preferences(self):
        for before, after in (("name=quick_open", "name=launcher_open"),
                              ("preferences_ok=1", "preferences_ok=0")):
            lines = run_lines()
            lines[0] = lines[0].replace(before, after)
            self.assertFalse(self.report(lines + [DONE])["complete"])

    def test_weather_needs_explicit_success_and_fetch(self):
        prefix = run_lines() + ["I (0) panel_test: WEATHER BEGIN normal_app=1 real_https=1"]
        self.assertFalse(self.report(prefix + [DONE])["complete"])
        success = "I (0) panel_test: WEATHER pass=1 state=2 fetching_seen=1 timeout=0 internal_free_min=90000 dma_largest_min=20000"
        self.assertTrue(self.report(prefix + [success, DONE])["complete"])
        for before, after in (("pass=1", "pass=0"), ("state=2", "state=3"),
                              ("fetching_seen=1", "fetching_seen=0"), ("timeout=0", "timeout=1")):
            self.assertFalse(self.report(prefix + [success.replace(before, after), DONE])["complete"])


if __name__ == "__main__":
    unittest.main()
