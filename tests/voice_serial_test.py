"""Disconnect/reconnect tests with a fake serial device, never the watch."""
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/voice"))
from dataset import SAMPLES, fnv1a, read_wav, write_wav
from serial_capture import CaptureDisconnected, SerialCaptureLink
import collect


class FakeSerial:
    is_open = False
    def __init__(self):
        self.opens = 0
        self.reads = 0
        self.fail_open = False
        pcm = bytes(SAMPLES * 2)
        self.packets = ([b"VOXPCM BEGIN 1 16000 32000\n"] +
            [f"VOXPCM DATA {offset} ".encode() + bytes(256).hex().encode() + b"\n"
             for offset in range(0, SAMPLES, 128)] + [f"VOXPCM END {fnv1a(pcm):08x}\n".encode()])
        self.lines = iter(self.packets)

    def open(self):
        self.opens += 1
        if self.fail_open:
            self.fail_open = False
            raise PermissionError("device not available")
        self.is_open = True

    def close(self):
        self.is_open = False

    def reset_input_buffer(self):
        if not self.is_open:
            raise OSError("disconnected before preparation")
        self.lines = iter(self.packets)

    def readline(self, size):
        self.reads += 1
        if self.reads == 3:
            raise OSError("ClearCommError failed")
        return next(self.lines)


class SerialTest(unittest.TestCase):
    def test_missing_port_can_be_reopened_without_reset(self):
        fake = FakeSerial()
        fake.fail_open = True
        with patch("serial_capture.serial.Serial", return_value=fake):
            with SerialCaptureLink("COM_TEST") as link:
                self.assertFalse(fake.dtr)
                self.assertFalse(fake.rts)
                self.assertFalse(fake.is_open)
                with self.assertRaises(CaptureDisconnected):
                    link.prepare()
                link.prepare()
                self.assertTrue(fake.is_open)
                self.assertEqual(fake.opens, 2)
        self.assertFalse(fake.is_open)

    def test_collect_retries_same_recording_after_disconnect(self):
        fake = FakeSerial()
        with tempfile.TemporaryDirectory() as temporary:
            command = ["collect.py", "--port", "COM_TEST", "--speaker", "test",
                       "--session", "occasion", "--split", "train", "--words", "Comprar",
                       "--repetitions", "1", "--background-repetitions", "1", "--output", temporary]
            with patch.object(sys, "argv", command), patch("serial_capture.serial.Serial", return_value=fake), \
                    patch("builtins.input", side_effect=["", "", "", "q"]), redirect_stdout(io.StringIO()) as output:
                collect.main()
            directory = Path(temporary) / "test/occasion"
            rows = [json.loads(line) for line in (directory / "manifest.jsonl").read_text(encoding="utf-8").splitlines()]
            self.assertEqual(len(rows), 1)
            self.assertEqual(rows[0]["file"], "00000.wav")
            self.assertEqual(read_wav(directory / "00000.wav"), bytes(SAMPLES * 2))
            self.assertFalse((directory / "00001.wav").exists())
            self.assertEqual(fake.opens, 2)
            self.assertIn("amostras aceitas estão preservadas", output.getvalue())
            self.assertFalse(fake.is_open)
            # Switching an existing session preserves its first accepted row
            # and recovers the next manual pending sample without line input.
            pending = bytes([1, 0]) * SAMPLES
            write_wav(directory / "pending.wav", pending)
            class QuitKeys:
                def poll(self):
                    return "q"
            with patch.object(sys, "argv", command + ["--hands-free"]), \
                    patch("serial_capture.serial.Serial", return_value=fake), \
                    patch("hands_free_capture.ConsoleKeys", return_value=QuitKeys()), \
                    patch("builtins.input", side_effect=AssertionError("No Enter expected")), \
                    redirect_stdout(io.StringIO()):
                collect.main()
            resumed = [json.loads(line) for line in (directory / "manifest.jsonl").read_text(encoding="utf-8").splitlines()]
            self.assertEqual(resumed[0], rows[0])
            self.assertEqual(len(resumed), 2)
            self.assertEqual(read_wav(directory / "00001.wav"), pending)


if __name__ == "__main__":
    unittest.main()
