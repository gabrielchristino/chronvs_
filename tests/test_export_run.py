import importlib.util
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location("export_run", Path(__file__).resolve().parents[1] / "scripts/export_run.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ExportTests(unittest.TestCase):
    def test_native_gpx_files_from_recorder(self):
        folders = list((Path(__file__).resolve().parents[1] / ".pio/host-tests").glob("runs-*"))
        self.assertTrue(folders)
        folder = max(folders, key=lambda p: p.stat().st_mtime_ns)
        files = list(folder.glob("*.gpx"))
        self.assertEqual(len(files), 2)
        ns = {"g": "http://www.topografix.com/GPX/1/1"}
        for file in files:
            root = ET.parse(file).getroot()
            points = root.findall(".//g:trkpt", ns)
            self.assertTrue(points)
            for point in points:
                self.assertIsNotNone(point.find("g:time", ns))
                self.assertTrue(-90 <= float(point.attrib["lat"]) <= 90)
            self.assertEqual(root.attrib["version"], "1.1")

    def test_recovery_segments_and_utc(self):
        with tempfile.TemporaryDirectory() as folder:
            src, dst = Path(folder)/"run.csv", Path(folder)/"run.gpx"
            src.write_text("utc,elapsed_s,active_s,segment,latitude,longitude,speed_kmh,altitude_m,distance_m\n"
                           "1794054920,0,0,1,-23.55,-46.63,10,750,0\n"
                           "1794054940,20,10,2,-23.54,-46.63,,,2\n"
                           "1794054941,21,11,2,-23.5")
            self.assertEqual(module.export(src, dst), 2)
            root = ET.parse(dst).getroot()
            ns = {"g": "http://www.topografix.com/GPX/1/1"}
            self.assertEqual(len(root.findall("g:trk/g:trkseg", ns)), 2)
            self.assertEqual(root.find(".//g:time", ns).text, "2026-11-07T12:35:20Z")
            self.assertEqual(len(root.findall(".//g:ele", ns)), 1)
            with self.assertRaises(FileExistsError):
                module.export(src, dst)


if __name__ == "__main__":
    unittest.main()
