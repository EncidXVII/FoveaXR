# Copyright (C) 2026 EncidXVII
# SPDX-License-Identifier: GPL-3.0-or-later

"""Runs one fixture under the SteamVR null driver and reports what happened.

    python harness/run.py xr-d3d11 [--frames 600] [--heavy 64] [--adapter N] [--layer] [--proxy] [--gaze U,V]
                                   [--secondary] [--timeout 120]

--adapter DXGI adapter index (0 NVIDIA, 1 AMD iGPU on the dev PC); OpenXR runs get it through the harness layer
--layer   loads our OpenXR layer from build/Release through XR_API_LAYER_PATH (no registry change)
--proxy   OpenVR fixtures: runs a copy of the fixture with our openvr_api.dll in front of the original
--gaze    keeps the test gaze source fresh at U,V for both eyes while the fixture runs
Prints one JSON line; exit code 0 when the run passed. Logs go to harness/out/.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import threading
import time

import shm
import steamvr
import windows

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
BIN = os.path.join(ROOT, "build", "Release")
OUT = os.path.join(ROOT, "harness", "out")
LAYER = "XR_APILAYER_FOVEAXR_core"
# Test-only layer, always loaded for OpenXR runs, listed last so it sits next to the runtime (see harness_layer.cpp).
HARNESS_LAYER = "XR_APILAYER_FOVEAXR_harness"
FIXTURES = {
    "openvr-d3d11": ("openvr", os.path.join(BIN, "fov_fixture_openvr_d3d11.exe"), []),
    "xr-d3d11": ("openxr", os.path.join(BIN, "fov_fixture_xr.exe"), ["--gfx", "d3d11"]),
    "xr-d3d12": ("openxr", os.path.join(BIN, "fov_fixture_xr.exe"), ["--gfx", "d3d12"]),
    "xr-vulkan": ("openxr", os.path.join(BIN, "fov_fixture_xr.exe"), ["--gfx", "vulkan"]),
    # Khronos sample (Apache-2.0), prebuilt in the OpenXR SDK release the build fetches. Runs until stopped.
    "hello_xr": ("openxr", os.path.join(ROOT, "build", "_deps", "openxr_sdk-src", "x64", "bin", "hello_xr.exe"),
                 ["-g", "D3D11"]),
}
# Windows that get moved off the main screen with --secondary.
WATCHED = {"vrmonitor.exe", "vrcompositor.exe", "vrserver.exe", "vrstartup.exe"}


def proxied_copy(exe, name):
    """A copy of an OpenVR fixture folder with the proxy as openvr_api.dll and the original renamed beside it."""
    folder = os.path.join(OUT, "proxy", name)
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder)
    shutil.copy2(exe, folder)
    shutil.copy2(os.path.join(BIN, "openvr_api.dll"), os.path.join(folder, "openvr_api.fov-orig.dll"))
    shutil.copy2(os.path.join(BIN, "openvr_proxy", "openvr_api.dll"), folder)
    return os.path.join(folder, os.path.basename(exe))


def run(fixture, frames=600, heavy=64, adapter=None, layer=False, proxy=False, gaze=None, mode=None, preset="medium",
        tint=False, dump=None, screen=None, timeout=120):
    """One fixture run inside an open null-driver session. mode: shm.MODE_* to set this run's settings (tests only).
    Returns a dict: passed, exit, out and core log lines, the core's published stats."""
    api, exe, args = FIXTURES[fixture]
    if not os.path.exists(exe):
        sys.exit(f"{exe} missing: build first (cmake --build build --config Release)")
    hello = fixture == "hello_xr"
    args = list(args)
    if not hello:
        args += ["--frames", str(frames), "--heavy", str(heavy)]
        if adapter is not None and api == "openvr":
            args += ["--adapter", str(adapter)]
        if dump:
            args += ["--dump", dump]
    if proxy:
        exe = proxied_copy(exe, fixture)

    os.makedirs(OUT, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    log_file = os.path.join(OUT, f"{fixture}-{stamp}.core.log")
    env = dict(os.environ, FOV_LOG_FILE=log_file)
    if api == "openxr":
        env["XR_RUNTIME_JSON"] = steamvr.runtime_json()
        env["XR_API_LAYER_PATH"] = BIN
        env["XR_ENABLE_API_LAYERS"] = ";".join([LAYER, HARNESS_LAYER] if layer else [HARNESS_LAYER])
        env["FOV_HARNESS_ADAPTER"] = str(adapter or 0)

    writer = shm.Writer()  # held for the whole run: the mapping, and the stats in it, outlive the game
    if mode is not None:
        writer.settings(mode=mode, preset=preset, tint=tint)
    stop_gaze = threading.Event()

    def keep_gaze_fresh():
        while not stop_gaze.wait(0.005):
            writer.write(gaze, gaze)

    if gaze:
        threading.Thread(target=keep_gaze_fresh, daemon=True).start()
    started = time.time()
    with windows.keep_on(screen, WATCHED | {os.path.basename(exe).lower()}):
        # stdin stays an open pipe: hello_xr quits on the first key it reads.
        proc = subprocess.Popen([exe, *args], cwd=os.path.dirname(exe), env=env, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
        lines = []
        reader = threading.Thread(target=lambda: lines.extend(proc.stdout), daemon=True)
        reader.start()
        try:
            # hello_xr never exits by itself: after 30 s it gets the key it waits for and shuts down cleanly (killing
            # it would lose its buffered log).
            code = proc.wait(timeout=30 if hello else timeout)
            if hello:
                code = "exited early"
        except subprocess.TimeoutExpired:
            code = "timeout"
            if hello:
                proc.stdin.write("q\n")
                proc.stdin.flush()
                try:
                    proc.wait(timeout=15)
                    code = "stopped"
                except subprocess.TimeoutExpired:
                    pass
            if proc.poll() is None:
                proc.kill()
        reader.join(5)
    stop_gaze.set()
    time.sleep(0.05)
    stats = writer.stats(os.path.basename(exe).lower())
    stats = {k: (list(map(list, getattr(stats, k))) if k == "focus" else getattr(stats, k)) for k, _ in
             shm.Stats._fields_ if k not in ("exe", "last_error")} if stats else None
    writer.close()

    core = []
    if os.path.exists(log_file):
        with open(log_file, encoding="utf-8", errors="replace") as f:
            core = [line.rstrip() for line in f]
    out = [line.rstrip() for line in lines]
    if hello:
        passed = code == "stopped" and any("XR_SESSION_STATE_FOCUSED" in line for line in out)
    else:
        passed = code == 0 and any(line.startswith("fixture: done:") for line in out)
    if layer or proxy:
        passed = passed and any("hooks on" in line for line in core)
    with open(os.path.join(OUT, f"{fixture}-{stamp}.out.log"), "w", encoding="utf-8") as f:
        f.write("\n".join(out))
    return {"fixture": fixture, "passed": passed, "exit": code, "seconds": round(time.time() - started, 1),
            "fixture_log": [line for line in out if line.startswith("fixture:")][-3:], "core_log": core[-8:],
            "stats": stats}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("fixture", choices=sorted(FIXTURES))
    p.add_argument("--frames", type=int, default=600)
    p.add_argument("--heavy", type=int, default=64)
    p.add_argument("--adapter", type=int)
    p.add_argument("--layer", action="store_true")
    p.add_argument("--proxy", action="store_true")
    p.add_argument("--gaze")
    p.add_argument("--eye", action="store_true", help="eye-tracked mode for this run (with --gaze)")
    p.add_argument("--tint", action="store_true")
    p.add_argument("--secondary", action="store_true")
    p.add_argument("--timeout", type=float, default=120)
    a = p.parse_args()
    if a.proxy and FIXTURES[a.fixture][0] != "openvr":
        sys.exit("--proxy is for OpenVR fixtures")
    gaze = tuple(float(x) for x in a.gaze.split(",")) if a.gaze else None
    mode = shm.MODE_EYE if a.eye else shm.MODE_FIXED if a.tint else None
    screen = steamvr.secondary_monitor() if a.secondary else None
    with steamvr.null_driver(window=screen):
        r = run(a.fixture, a.frames, a.heavy, a.adapter, a.layer, a.proxy, gaze, mode, tint=a.tint, screen=screen,
                timeout=a.timeout)
    print(json.dumps(r))
    return 0 if r["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
