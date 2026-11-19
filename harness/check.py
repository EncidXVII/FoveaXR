"""Quick foveation check: rate images bind on eye passes only, the rings follow the test gaze, the UI pass is untouched.

    python harness/check.py [--secondary]

D3D11: the same frame rendered with foveation off and with eye-tracked foveation at a scripted gaze; the blocks that
stayed identical must sit around the gaze point, the periphery must have changed, the UI rectangle must not.
D3D12 and OpenVR (through the openvr_api.dll proxy): the core's counters and focus point.
Prints one JSON line; exit code 0 when everything passed.
"""
import argparse
import json
import os
import sys

import numpy as np

import shm
import steamvr
from run import OUT, run

GAZE = (0.35, 0.45)
UI = (0.3, 0.62, 0.7, 0.76)  # fixture.h kUi
BLOCK = 32


def load_bmp(path):
    b = open(path, "rb").read()
    w, h = int.from_bytes(b[18:22], "little", signed=True), int.from_bytes(b[22:26], "little", signed=True)
    img = np.frombuffer(b, np.uint8, abs(w * h) * 4, 54).reshape(abs(h), w, 4)[..., :3]
    return (img if h < 0 else img[::-1]).astype(np.int32)


def rings(ref, fov):
    """Centroid (UV) of the blocks foveation left identical, share of changed blocks, UI rectangle difference."""
    diff = np.abs(fov - ref).mean(axis=2)
    h, w = diff.shape
    bh, bw = h // BLOCK, w // BLOCK
    blocks = diff[:bh * BLOCK, :bw * BLOCK].reshape(bh, BLOCK, bw, BLOCK).mean(axis=(1, 3))
    same = blocks < 1.0
    # The UI rectangle is unfoveated by design: it must not pull the fovea's centroid.
    cy, cx = (np.arange(bh)[:, None] + 0.5) / bh, (np.arange(bw)[None, :] + 0.5) / bw
    ui_blocks = (cx > UI[0]) & (cx < UI[2]) & (cy > UI[1]) & (cy < UI[3])
    ys, xs = np.nonzero(same & ~ui_blocks)
    centroid = [float((xs.mean() + 0.5) / bw), float((ys.mean() + 0.5) / bh)] if len(xs) else [None, None]
    ui = diff[int(UI[1] * h):int(UI[3] * h), int(UI[0] * w):int(UI[2] * w)].mean()
    return centroid, float(1 - same.mean()), float(ui)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--secondary", action="store_true")
    a = p.parse_args()
    screen = steamvr.secondary_monitor() if a.secondary else None
    ref, fov = os.path.join(OUT, "check_ref"), os.path.join(OUT, "check_fov")
    with steamvr.null_driver(window=screen):
        r_ref = run("xr-d3d11", frames=90, layer=True, mode=shm.MODE_OFF, dump=ref, screen=screen)
        r_fov = run("xr-d3d11", frames=90, layer=True, gaze=GAZE, mode=shm.MODE_EYE, dump=fov, screen=screen)
        r_12 = run("xr-d3d12", frames=90, layer=True, gaze=GAZE, mode=shm.MODE_EYE, screen=screen)
        r_vr = run("openvr-d3d11", frames=90, proxy=True, gaze=GAZE, mode=shm.MODE_EYE, screen=screen)

    checks = {}
    runs = (("d3d11", r_fov), ("d3d12", r_12), ("openvr_d3d11", r_vr))
    for name, r in (("d3d11_off", r_ref),) + runs:
        checks[name + " ran"] = r["passed"]
    for name, r in runs:
        s = r["stats"] or {}
        checks[name + " rings bound"] = s.get("rings_bound", 0) > 0
        checks[name + " UI pass skipped"] = s.get("ui_skipped", 0) > 0
        checks[name + " focus = gaze"] = bool(s) and all(abs(s["focus"][e][i] - GAZE[i]) < 0.005
                                                         for e in (0, 1) for i in (0, 1))
    checks["d3d11_off nothing bound"] = (r_ref["stats"] or {}).get("rings_bound", 1) == 0
    detail = {}
    for eye in (0, 1):
        try:
            c, changed, ui = rings(load_bmp(f"{ref}_eye{eye}.bmp"), load_bmp(f"{fov}_eye{eye}.bmp"))
        except OSError as e:
            checks[f"eye {eye} images"] = False
            detail[eye] = str(e)
            continue
        detail[eye] = {"centroid": c, "changed": round(changed, 3), "ui_diff": round(ui, 3)}
        checks[f"eye {eye} fovea at gaze"] = c[0] is not None and abs(c[0] - GAZE[0]) < 0.03 and abs(c[1] - GAZE[1]) < 0.03
        checks[f"eye {eye} periphery coarse"] = changed > 0.5
        checks[f"eye {eye} UI untouched"] = ui < 0.5
    passed = all(checks.values())
    print(json.dumps({"passed": passed, "checks": checks, "images": detail,
                      "logs": {name: r["core_log"] for name, r in runs}}))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
