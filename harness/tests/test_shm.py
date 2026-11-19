"""Shared-memory layout and the test gaze source end to end: Python writes, the C++ gaze module reads (fov_probe).

Run from the repo root after a native build: python -m unittest discover -s harness/tests
"""
import ctypes
import json
import os
import subprocess
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import gaze  # noqa: E402

PROBE = os.path.join(os.path.dirname(__file__), "..", "..", "build", "Release", "fov_probe.exe")


def probe():
    return json.loads(subprocess.run([PROBE, "--gaze"], capture_output=True, text=True, check=True).stdout)


class Layout(unittest.TestCase):
    # The same numbers native/include/fov_shm.h and app/crates/shm assert.
    def test_offsets(self):
        self.assertEqual(ctypes.sizeof(gaze.Settings), 96)
        self.assertEqual(gaze.Settings.inner_deg.offset, 24)
        self.assertEqual(gaze.Settings.rate.offset, 32)
        self.assertEqual(gaze.Settings.saccade_hold_ms.offset, 64)
        self.assertEqual(ctypes.sizeof(gaze.Gaze), 48)
        self.assertEqual(gaze.Gaze.qpc.offset, 16)
        self.assertEqual(gaze.Gaze.eye.offset, 24)
        self.assertEqual(ctypes.sizeof(gaze.Stats), 256)
        self.assertEqual(gaze.Stats.gaze_valid_pct.offset, 92)
        self.assertEqual(gaze.Stats.frames.offset, 104)
        self.assertEqual(gaze.Stats.last_error.offset, 128)
        self.assertEqual(ctypes.sizeof(gaze.Slot), 368)
        self.assertEqual(gaze.Slot.settings.offset, 8)
        self.assertEqual(gaze.Slot.stats.offset, 112)
        self.assertEqual(ctypes.sizeof(gaze.Shm), 3112)
        self.assertEqual(gaze.Shm.settings_seq.offset, 16)
        self.assertEqual(gaze.Shm.settings.offset, 24)
        self.assertEqual(gaze.Shm.gaze.offset, 120)
        self.assertEqual(gaze.Shm.slots.offset, 168)


@unittest.skipUnless(os.path.exists(PROBE), "native build missing")
class TestSource(unittest.TestCase):
    def setUp(self):
        self.writer = gaze.GazeWriter()

    def tearDown(self):
        self.writer.close()

    def test_round_trip(self):
        self.writer.write((0.25, 0.4), (0.75, 0.6))
        got = probe()
        self.assertEqual(got["status"], "valid")
        self.assertEqual(got["source"], gaze.SOURCE_TEST)
        self.assertAlmostEqual(got["left"][0], 0.25, places=5)
        self.assertAlmostEqual(got["right"][1], 0.6, places=5)
        self.assertEqual(self.writer.shm.magic, gaze.MAGIC)  # the core filled the header on its first map

    def test_blink_and_invalid(self):
        self.writer.write((0.5, 0.5), (0.5, 0.5), blink=True)
        self.assertEqual(probe()["status"], "blink")
        self.writer.write((0.5, 0.5), (0.5, 0.5), valid=False)
        self.assertEqual(probe()["status"], "stale")

    def test_stale(self):
        freq = ctypes.c_int64()
        ctypes.windll.kernel32.QueryPerformanceFrequency(ctypes.byref(freq))
        self.writer.write((0.5, 0.5), (0.5, 0.5), stamp=gaze.qpc() - freq.value // 5)  # 200 ms old
        self.assertEqual(probe()["status"], "stale")


if __name__ == "__main__":
    unittest.main()
