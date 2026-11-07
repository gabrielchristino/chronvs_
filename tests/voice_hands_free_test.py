"""Watch-driven collection state transitions, with no keyboard line input."""
from contextlib import redirect_stdout
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/voice"))
from dataset import SAMPLES, fnv1a, read_wav, write_wav
from hands_free_capture import collect_hands_free
from serial_capture import CaptureDisconnected


def packets(pcm):
    return ([b"VOXPCM BEGIN 1 16000 32000\n"] +
            [f"VOXPCM DATA {offset} ".encode() + pcm[offset*2:offset*2+256].hex().encode() + b"\n"
             for offset in range(0, SAMPLES, 128)] + [f"VOXPCM END {fnv1a(pcm):08x}\n".encode()])


class Clock:
    value = 0
    def __call__(self):
        self.value += .01
        return self.value

    def sleep(self, duration):
        self.value += duration


class Port:
    def __init__(self, lines):
        self.lines = iter(lines)
        self.opens = 0

    def prepare(self):
        self.opens += 1

    def readline(self, size):
        result = next(self.lines, b"")
        if isinstance(result, Exception):
            raise result
        return result


class Keys:
    def __init__(self, callback=lambda: None):
        self.callback = callback

    def poll(self):
        return self.callback()


class HandsFreeTest(unittest.TestCase):
    def run_flow(self, directory, port, prompts, start, keys, played):
        accepted = []
        clock = Clock()
        def save(index, pcm, stats):
            accepted.append((index, pcm))
            (directory / "pending.wav").rename(directory / f"{index:05d}.wav")
        def playback(path):
            played.append(read_wav(path))
        with redirect_stdout(io.StringIO()), patch("builtins.input", side_effect=AssertionError("No Enter expected")):
            collect_hands_free(port, io.BytesIO(), directory, prompts, start, save,
                               playback=playback, keys=keys, clock=clock, sleep=clock.sleep)
        return accepted

    def test_r_repeats_without_committing_or_skipping_and_next_tap_accepts(self):
        a, b, c = (struct.pack("<h", value) * SAMPLES for value in (11, 12, 13))
        played, repeated = [], []
        def key():
            if len(played) == 1 and not repeated:
                repeated.append(True)
                return "r"
            if len(played) == 3:
                return "q"
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            accepted = self.run_flow(directory, Port(packets(a) + packets(b) + packets(c)),
                                     ["one", "two", "three"], 0, Keys(key), played)
            self.assertEqual(accepted, [(0, b), (1, c)])
            self.assertEqual(played, [a, b, c])
            self.assertFalse((directory / "00002.wav").exists())
            self.assertFalse((directory / "pending.wav").exists())

    def test_resume_pending_and_q_preserves_previous_files(self):
        pcm = struct.pack("<h", 31) * SAMPLES
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            write_wav(directory / "00043.wav", pcm)
            write_wav(directory / "pending.wav", pcm)
            played = []
            accepted = self.run_flow(directory, Port([]), ["word"] * 80, 44, Keys(lambda: "q"), played)
            self.assertEqual(accepted, [(44, pcm)])
            self.assertEqual(read_wav(directory / "00043.wav"), pcm)
            self.assertEqual(read_wav(directory / "00044.wav"), pcm)
            self.assertEqual(played, [pcm])

    def test_disconnect_discards_only_partial_capture_and_reconnects(self):
        pcm = struct.pack("<h", 21) * SAMPLES
        played = []
        lines = packets(pcm)[:3] + [CaptureDisconnected("USB removed")] + packets(pcm)
        port = Port(lines)
        with tempfile.TemporaryDirectory() as temporary:
            accepted = self.run_flow(Path(temporary), port, ["one", "two"], 0,
                                     Keys(lambda: "q" if played else None), played)
            self.assertEqual(accepted, [(0, pcm)])
            self.assertEqual(port.opens, 2)

    def test_last_sample_auto_confirms_after_playback(self):
        pcm = bytes(SAMPLES * 2)
        played = []
        with tempfile.TemporaryDirectory() as temporary:
            accepted = self.run_flow(Path(temporary), Port(packets(pcm)), ["one"], 0, Keys(), played)
            self.assertEqual(accepted, [(0, pcm)])


if __name__ == "__main__":
    unittest.main()
