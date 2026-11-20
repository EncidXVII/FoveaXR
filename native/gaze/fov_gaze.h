// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
// Gaze sources (SPEC 7). This phase has the shared-memory source, which carries both the harness's test gaze and the
// app's forwarded OSC gaze; the OpenXR and OpenVR sources join it with their VR API paths.
#include "fov_shm.h"

namespace fov::gaze {

enum class Status {
    Valid,  // fresh sample, eyes open
    Blink,  // fresh sample, eyes closed: Fixed for this frame
    Stale,  // older than FOV_GAZE_STALE_MS, or flagged invalid
    None,   // no source has written anything
};

struct Sample {
    FovGazeSource source = FOV_GAZE_NONE;
    FovGazeKind kind = FOV_GAZE_UV;
    float eye[2][3] = {};
};

// Classifies a raw block `age_ms` old; pure, so the rules are unit tested without shared memory.
Status classify(const FovGaze& raw, double age_ms);

// Reads the shared-memory gaze block.
Status from_shm(Sample& out, int64_t now_qpc);

}  // namespace fov::gaze
