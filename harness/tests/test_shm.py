# Copyright (C) 2026 EncidXVII
# SPDX-License-Identifier: GPL-3.0-or-later

"""Shared-memory layout and the test gaze source end to end: Python writes, the C++ gaze module reads (fov_probe).

Run from the repo root after a native build: python -m unittest discover -s harness/tests
"""
import ctypes
import json
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import shm  # noqa: E402

PROBE = os.path.join(os.path.dirname(__file__), "..", "..", "build", "Release", "fov_probe.exe")


def probe(env=None):
    out = subprocess.run([PROBE, "--gaze"], capture_output=True, text=True, check=True, env=env).stdout
    return json.loads(out)


class Layout(unittest.TestCase):
    # The same numbers native/include/fov_shm.h and app/crates/shm assert.
    def test_offsets(self):
        self.assertEqual(ctypes.sizeof(shm.Settings), 96)
        self.assertEqual(shm.Settings.inner_deg.offset, 24)
        self.assertEqual(shm.Settings.rate.offset, 32)
        self.assertEqual(shm.Settings.saccade_hold_ms.offset, 64)
        self.assertEqual(ctypes.sizeof(shm.Gaze), 48)
        self.assertEqual(shm.Gaze.qpc.offset, 16)
        self.assertEqual(shm.Gaze.eye.offset, 24)
        self.assertEqual(ctypes.sizeof(shm.Stats), 256)
        self.assertEqual(shm.Stats.gaze_valid_pct.offset, 92)
        self.assertEqual(shm.Stats.frames.offset, 104)
        self.assertEqual(shm.Stats.focus.offset, 128)
        self.assertEqual(shm.Stats.last_error.offset, 160)
        self.assertEqual(ctypes.sizeof(shm.Slot), 368)
        self.assertEqual(shm.Slot.settings.offset, 8)
        self.assertEqual(shm.Slot.stats.offset, 112)
        self.assertEqual(ctypes.sizeof(shm.Shm), 3112)
        self.assertEqual(shm.Shm.settings_seq.offset, 16)
        self.assertEqual(shm.Shm.settings.offset, 24)
        self.assertEqual(shm.Shm.gaze.offset, 120)
        self.assertEqual(shm.Shm.slots.offset, 168)


@unittest.skipUnless(os.path.exists(PROBE), "native build missing")
class TestSource(unittest.TestCase):
    def setUp(self):
        self.writer = shm.Writer()

    def tearDown(self):
        self.writer.close()

    def test_round_trip(self):
        self.writer.write((0.25, 0.4), (0.75, 0.6))
        got = probe()
        self.assertEqual(got["status"], "valid")
        self.assertEqual(got["source"], shm.SOURCE_TEST)
        self.assertAlmostEqual(got["left"][0], 0.25, places=5)
        self.assertAlmostEqual(got["right"][1], 0.6, places=5)
        self.assertEqual(self.writer.shm.magic, shm.MAGIC)  # the core filled the header on its first map

    def test_blink_and_invalid(self):
        self.writer.write((0.5, 0.5), (0.5, 0.5), blink=True)
        self.assertEqual(probe()["status"], "blink")
        self.writer.write((0.5, 0.5), (0.5, 0.5), valid=False)
        self.assertEqual(probe()["status"], "stale")

    def test_stale(self):
        freq = ctypes.c_int64()
        ctypes.windll.kernel32.QueryPerformanceFrequency(ctypes.byref(freq))
        self.writer.write((0.5, 0.5), (0.5, 0.5), stamp=shm.qpc() - freq.value // 5)  # 200 ms old
        self.assertEqual(probe()["status"], "stale")


@unittest.skipUnless(os.path.exists(PROBE), "native build missing")
class FileBacked(unittest.TestCase):
    r"""The Linux / Proton arrangement: the app maps a file (/dev/shm/FoveaXR.v1), the core inside Wine maps the
    same file (Z:\dev\shm\...). Here both sides map one temp file: Python as the app, fov_probe as the core."""

    def test_round_trip_through_a_file(self):
        path = os.path.join(tempfile.mkdtemp(), "FoveaXR.v1")
        writer = shm.Writer(path)
        try:
            writer.write((0.1, 0.2), (0.3, 0.4))
            got = probe(dict(os.environ, FOV_SHM_FILE=path))
            self.assertEqual(got["status"], "valid")
            self.assertAlmostEqual(got["left"][1], 0.2, places=5)
            self.assertEqual(os.path.getsize(path), ctypes.sizeof(shm.Shm))
            self.assertEqual(writer.shm.magic, shm.MAGIC)  # the core filled the header in the file
            # Not the named section: that one never saw this gaze.
            self.assertNotEqual(probe()["left"][1], 0.2)
        finally:
            writer.close()


if __name__ == "__main__":
    unittest.main()
