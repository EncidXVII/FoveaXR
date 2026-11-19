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

import gaze
import steamvr
import windows

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
BIN = os.path.join(ROOT, "build", "Release")
OUT = os.path.join(ROOT, "harness", "out")
LAYER = "XR_APILAYER_FOVEATEDVR_core"
# Test-only layer, always loaded for OpenXR runs, listed last so it sits next to the runtime (see harness_layer.cpp).
HARNESS_LAYER = "XR_APILAYER_FOVEATEDVR_harness"
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


def keep_gaze_fresh(uv, stop):
    writer = gaze.GazeWriter()
    while not stop.wait(0.005):
        writer.write(uv, uv)
    writer.close()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("fixture", choices=sorted(FIXTURES))
    p.add_argument("--frames", type=int, default=600)
    p.add_argument("--heavy", type=int, default=64)
    p.add_argument("--adapter", type=int)
    p.add_argument("--layer", action="store_true")
    p.add_argument("--proxy", action="store_true")
    p.add_argument("--gaze")
    p.add_argument("--secondary", action="store_true")
    p.add_argument("--timeout", type=float, default=120)
    a = p.parse_args()

    api, exe, args = FIXTURES[a.fixture]
    if not os.path.exists(exe):
        sys.exit(f"{exe} missing: build first (cmake --build build --config Release)")
    hello = a.fixture == "hello_xr"
    if not hello:
        args += ["--frames", str(a.frames), "--heavy", str(a.heavy)]
        if a.adapter is not None and api == "openvr":
            args += ["--adapter", str(a.adapter)]
    if a.proxy:
        if api != "openvr":
            sys.exit("--proxy is for OpenVR fixtures")
        exe = proxied_copy(exe, a.fixture)

    os.makedirs(OUT, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    log_file = os.path.join(OUT, f"{a.fixture}-{stamp}.core.log")
    env = dict(os.environ, FOV_LOG_FILE=log_file)
    if api == "openxr":
        env["XR_RUNTIME_JSON"] = steamvr.runtime_json()
        env["XR_API_LAYER_PATH"] = BIN
        env["XR_ENABLE_API_LAYERS"] = ";".join([LAYER, HARNESS_LAYER] if a.layer else [HARNESS_LAYER])
        env["FOV_HARNESS_ADAPTER"] = str(a.adapter or 0)

    screen = steamvr.secondary_monitor() if a.secondary else None
    stop_gaze = threading.Event()
    if a.gaze:
        uv = tuple(float(x) for x in a.gaze.split(","))
        threading.Thread(target=keep_gaze_fresh, args=(uv, stop_gaze), daemon=True).start()

    started = time.time()
    with steamvr.null_driver(window=screen), windows.keep_on(screen, WATCHED | {os.path.basename(exe).lower()}):
        # stdin stays an open pipe: hello_xr quits on the first key it reads.
        proc = subprocess.Popen([exe, *args], cwd=os.path.dirname(exe), env=env, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, errors="replace")
        lines = []
        reader = threading.Thread(target=lambda: lines.extend(proc.stdout), daemon=True)
        reader.start()
        try:
            # hello_xr never exits by itself: after 30 s it gets the key it waits for and shuts down cleanly (killing
            # it would lose its buffered log).
            code = proc.wait(timeout=30 if hello else a.timeout)
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

    core = []
    if os.path.exists(log_file):
        with open(log_file, encoding="utf-8", errors="replace") as f:
            core = [line.rstrip() for line in f]
    out = [line.rstrip() for line in lines]
    if hello:
        passed = code == "stopped" and any("XR_SESSION_STATE_FOCUSED" in line for line in out)
    else:
        passed = code == 0 and any(line.startswith("fixture: done:") for line in out)
    if a.layer:
        passed = passed and any("layer" in line and "instance" in line for line in core) \
            and any("session:" in line for line in core)
    if a.proxy:
        passed = passed and any("openvr proxy" in line and "forwarding" in line for line in core)
    with open(os.path.join(OUT, f"{a.fixture}-{stamp}.out.log"), "w", encoding="utf-8") as f:
        f.write("\n".join(out))
    print(json.dumps({
        "fixture": a.fixture, "passed": passed, "exit": code, "seconds": round(time.time() - started, 1),
        "fixture_log": [line for line in out if line.startswith("fixture:")][-4:],
        "core_log": core[-6:],
    }))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
