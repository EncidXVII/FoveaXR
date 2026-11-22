# Copyright (C) 2026 EncidXVII
# SPDX-License-Identifier: GPL-3.0-or-later

"""The shared memory every game process reads: test gaze source (SPEC 7, source 5) and test settings.

Mirrors native/include/fov_shm.h; tests/test_shm.py checks the offsets against the numbers the C++ and Rust sides
check too.
"""
import ctypes
import mmap
import os

NAME = "Local\\FoveaXR.v1"
MAGIC = 0x31564F46
VERSION = 1
SLOTS = 8
SOURCE_TEST = 5
KIND_UV = 0
VALID, BLINK = 1, 2
MODE_OFF, MODE_FIXED, MODE_EYE = 0, 1, 2
# Presets as the app resolves them (fov_settings_default): inner, mid degrees; rates inner, mid, outer.
PRESETS = {"low": (30, 45, (0, 0, 3)), "medium": (22, 35, (0, 3, 6)), "high": (15, 22, (0, 3, 6))}


class Settings(ctypes.Structure):
    _fields_ = [
        ("enabled", ctypes.c_uint32), ("mode", ctypes.c_uint32), ("preset", ctypes.c_uint32),
        ("debug_tint", ctypes.c_uint32), ("ab_off", ctypes.c_uint32), ("saccade_expand", ctypes.c_uint32),
        ("inner_deg", ctypes.c_float), ("mid_deg", ctypes.c_float), ("rate", ctypes.c_uint32 * 3),
        ("smooth_min_cutoff", ctypes.c_float), ("smooth_beta", ctypes.c_float), ("smooth_d_cutoff", ctypes.c_float),
        ("saccade_deg_s", ctypes.c_float), ("saccade_factor", ctypes.c_float), ("saccade_hold_ms", ctypes.c_float),
        ("reserved", ctypes.c_uint32 * 7),
    ]


class Gaze(ctypes.Structure):
    _fields_ = [
        ("source", ctypes.c_uint32), ("kind", ctypes.c_uint32), ("flags", ctypes.c_uint32), ("pad", ctypes.c_uint32),
        ("qpc", ctypes.c_int64), ("eye", (ctypes.c_float * 3) * 2),
    ]


class Stats(ctypes.Structure):
    _fields_ = [
        ("exe", ctypes.c_char * 64), ("app_id", ctypes.c_uint32), ("api", ctypes.c_uint32), ("gfx", ctypes.c_uint32),
        ("layout", ctypes.c_uint32), ("active", ctypes.c_uint32), ("reason", ctypes.c_uint32),
        ("gaze_source", ctypes.c_uint32), ("gaze_valid_pct", ctypes.c_float), ("eye_ms_on", ctypes.c_float),
        ("eye_ms_off", ctypes.c_float), ("frames", ctypes.c_uint64), ("rings_bound", ctypes.c_uint64),
        ("uncertain_eye", ctypes.c_uint64), ("focus", (ctypes.c_float * 2) * 2), ("ui_skipped", ctypes.c_uint64),
        ("eye_passes", ctypes.c_uint64), ("last_error", ctypes.c_char * 96),
    ]


class Slot(ctypes.Structure):
    _fields_ = [
        ("pid", ctypes.c_uint32), ("settings_seq", ctypes.c_uint32), ("settings", Settings),
        ("stats_seq", ctypes.c_uint32), ("pad", ctypes.c_uint32), ("stats", Stats),
    ]


class Shm(ctypes.Structure):
    _fields_ = [
        ("magic", ctypes.c_uint32), ("version", ctypes.c_uint32), ("size", ctypes.c_uint32), ("pad", ctypes.c_uint32),
        ("settings_seq", ctypes.c_uint32), ("gaze_seq", ctypes.c_uint32), ("settings", Settings), ("gaze", Gaze),
        ("slots", Slot * SLOTS),
    ]


def qpc():
    t = ctypes.c_int64()
    ctypes.windll.kernel32.QueryPerformanceCounter(ctypes.byref(t))
    return t.value


class Writer:
    """Holds the mapping open (it lives while any process holds it) and writes with the seqlock protocol. Gaze only,
    unless settings are given: then this run's settings replace the app's (tests only)."""

    def __init__(self, path=None):
        """path: a file to map instead of the named section, as the Linux app maps /dev/shm/FoveaXR.v1 (the core
        maps the same file through Wine's Z: drive, or the file FOV_SHM_FILE names)."""
        size = ctypes.sizeof(Shm)
        if path:
            with open(path, "a+b") as f:
                if os.path.getsize(path) < size:
                    f.truncate(size)
                self._map = mmap.mmap(f.fileno(), size)
        else:
            self._map = mmap.mmap(-1, size, tagname=NAME)
        self.shm = Shm.from_buffer(self._map)

    def settings(self, mode=MODE_EYE, preset="medium", tint=False):
        inner, mid, rates = PRESETS[preset]
        s = Settings(enabled=1, mode=mode, preset=list(PRESETS).index(preset), debug_tint=int(tint), saccade_expand=1,
                     inner_deg=inner, mid_deg=mid, smooth_min_cutoff=1.0, smooth_beta=0.05, smooth_d_cutoff=1.0,
                     saccade_deg_s=180, saccade_factor=1.5, saccade_hold_ms=50)
        s.rate[:] = rates
        self.shm.settings_seq += 1
        self.shm.settings = s
        self.shm.settings_seq += 1
        self.shm.version, self.shm.size, self.shm.magic = VERSION, ctypes.sizeof(Shm), MAGIC

    def stats(self, exe):
        """The stats a game process published, by exe name (lowercase), or None."""
        for slot in self.shm.slots:
            if slot.pid and slot.stats.exe.decode(errors="replace") == exe:
                return Stats.from_buffer_copy(slot.stats)  # a copy: the mapping can close while it lives
        return None

    def write(self, left, right, valid=True, blink=False, stamp=None):
        """left, right: (u, v) in eye-image UV, 0..1 from the top-left."""
        g = Gaze(source=SOURCE_TEST, kind=KIND_UV, flags=(VALID if valid else 0) | (BLINK if blink else 0),
                 qpc=qpc() if stamp is None else stamp)
        g.eye[0][0], g.eye[0][1] = left
        g.eye[1][0], g.eye[1][1] = right
        # x86 stores are not reordered with each other, so plain writes keep the odd/even protocol intact.
        self.shm.gaze_seq += 1
        self.shm.gaze = g
        self.shm.gaze_seq += 1

    def close(self):
        del self.shm
        self._map.close()
