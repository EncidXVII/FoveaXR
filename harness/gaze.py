"""Test gaze source (SPEC 7, source 5): writes scripted gaze into the shared memory every game process reads.

Mirrors native/include/fov_shm.h; tests/test_shm.py checks the offsets against the numbers the C++ and Rust sides
check too.
"""
import ctypes
import mmap

NAME = "Local\\FoveatedVR.v1"
MAGIC = 0x31564F46
VERSION = 1
SLOTS = 8
SOURCE_TEST = 5
KIND_UV = 0
VALID, BLINK = 1, 2


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
        ("uncertain_eye", ctypes.c_uint64), ("last_error", ctypes.c_char * 128),
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


class GazeWriter:
    """Holds the mapping open (it lives while any process holds it) and writes test gaze with the seqlock protocol.
    Only the gaze block: the header and settings belong to the core and the app, which fill them on first map."""

    def __init__(self):
        self._map = mmap.mmap(-1, ctypes.sizeof(Shm), tagname=NAME)
        self.shm = Shm.from_buffer(self._map)

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
