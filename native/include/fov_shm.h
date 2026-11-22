// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

/* Shared memory between the tray app (settings, forwarded gaze), the harness (test gaze) and every game process
 * (stats). Plain C so the Rust app (app/crates/shm) and the Python harness (harness/gaze.py) mirror it field for
 * field; all three check the offsets below in a unit test, so a change here must change all three.
 *
 * Consistency: every block has a sequence counter (seqlock). The writer makes it odd, writes, makes it even; a reader
 * copies the block and retries while the counter was odd or changed. One writer per block: the app writes settings
 * and gaze (or the harness writes gaze in tests), a game writes only the stats of the slot it claimed. */
#ifndef FOV_SHM_H
#define FOV_SHM_H
#include <stdint.h>

#define FOV_SHM_NAME L"Local\\FoveaXR.v1"
#define FOV_SHM_MAGIC 0x31564F46u /* "FOV1" little endian */
#define FOV_SHM_VERSION 1u
#define FOV_SHM_SLOTS 8
/* A gaze sample older than this is stale: the frame falls back to Fixed. */
#define FOV_GAZE_STALE_MS 100

enum FovMode { FOV_MODE_OFF = 0, FOV_MODE_FIXED = 1, FOV_MODE_EYE = 2 };
enum FovPreset { FOV_PRESET_LOW = 0, FOV_PRESET_MEDIUM = 1, FOV_PRESET_HIGH = 2, FOV_PRESET_CUSTOM = 3 };
enum FovRate {
    FOV_RATE_1X1 = 0, FOV_RATE_2X1 = 1, FOV_RATE_1X2 = 2, FOV_RATE_2X2 = 3, FOV_RATE_4X2 = 4, FOV_RATE_2X4 = 5,
    FOV_RATE_4X4 = 6
};
/* In priority order (SPEC 7): the first source with a fresh valid sample wins. */
enum FovGazeSource {
    FOV_GAZE_NONE = 0, FOV_GAZE_OPENXR = 1, FOV_GAZE_OPENVR_CENTER = 2, FOV_GAZE_OPENVR_ACTION = 3, FOV_GAZE_OSC = 4,
    FOV_GAZE_TEST = 5
};
/* UV: per-eye image position, 0..1 from the top-left. DIR: per-eye direction in view space (x right, y up, -z
 * forward), projected by the core with that frame's FOV. */
enum FovGazeKind { FOV_GAZE_UV = 0, FOV_GAZE_DIR = 1 };
enum FovGazeFlags { FOV_GAZE_VALID = 1, FOV_GAZE_BLINK = 2 };
enum FovApi { FOV_API_NONE = 0, FOV_API_OPENXR = 1, FOV_API_OPENVR = 2 };
enum FovGfx { FOV_GFX_NONE = 0, FOV_GFX_D3D11 = 1, FOV_GFX_D3D12 = 2, FOV_GFX_VULKAN = 3 };
enum FovLayout { FOV_LAYOUT_UNKNOWN = 0, FOV_LAYOUT_SEPARATE = 1, FOV_LAYOUT_ARRAY = 2, FOV_LAYOUT_WIDE = 3 };
/* Why a game process runs without foveation (SPEC 8); FOV_REASON_ACTIVE when it runs with it. */
enum FovReason {
    FOV_REASON_ACTIVE = 0, FOV_REASON_GLOBAL_OFF = 1, FOV_REASON_GAME_OFF = 2, FOV_REASON_ANTICHEAT_LIST = 3,
    FOV_REASON_ANTICHEAT_FILES = 4, FOV_REASON_NATIVE_FOVEATION = 5, FOV_REASON_NEVER = 6,
    FOV_REASON_UNSUPPORTED_GPU = 7, FOV_REASON_ERROR = 8
};

/* Effective settings, presets already resolved into radii and rates by the app. */
typedef struct FovSettings {
    uint32_t enabled;
    uint32_t mode;           /* FovMode */
    uint32_t preset;         /* FovPreset, for display; the values below are what applies */
    uint32_t debug_tint;
    uint32_t ab_off;         /* A/B toggle (Ctrl+F9) currently has foveation off */
    uint32_t saccade_expand;
    float inner_deg, mid_deg;
    uint32_t rate[3];        /* FovRate for the inner, mid and outer ring */
    float smooth_min_cutoff, smooth_beta, smooth_d_cutoff; /* 1-euro filter */
    float saccade_deg_s, saccade_factor, saccade_hold_ms;
    uint32_t reserved[7];
} FovSettings;

typedef struct FovGaze {
    uint32_t source; /* FovGazeSource */
    uint32_t kind;   /* FovGazeKind */
    uint32_t flags;  /* FovGazeFlags */
    uint32_t pad;
    int64_t qpc;     /* QueryPerformanceCounter when sampled */
    float eye[2][3]; /* left, right: u, v, 0 for UV; x, y, z for DIR */
} FovGaze;

typedef struct FovStats {
    char exe[64];
    uint32_t app_id;      /* Steam app id, 0 if unknown */
    uint32_t api;         /* FovApi */
    uint32_t gfx;         /* FovGfx */
    uint32_t layout;      /* FovLayout */
    uint32_t active;
    uint32_t reason;      /* FovReason */
    uint32_t gaze_source; /* FovGazeSource */
    float gaze_valid_pct;
    float eye_ms_on, eye_ms_off; /* eye-pass GPU time EMA with foveation on and off (A/B) */
    uint64_t frames, rings_bound, uncertain_eye;
    float focus[2][2];    /* last focus point per eye, UV (the harness checks it follows the test gaze) */
    uint64_t ui_skipped;  /* passes on eye-sized targets left unfoveated (no depth: UI, post) */
    uint64_t eye_passes;  /* eye passes recognised, foveated or not: 0 in a running game = nothing to foveate */
    char last_error[96];
} FovStats;

typedef struct FovSlot {
    uint32_t pid;          /* 0 = free; a game claims a slot with a compare-exchange from 0 */
    uint32_t settings_seq; /* written by the app */
    FovSettings settings;  /* this process's settings, per-game override applied */
    uint32_t stats_seq;    /* written by the game */
    uint32_t pad;
    FovStats stats;
} FovSlot;

typedef struct FovShm {
    uint32_t magic, version, size, pad;
    uint32_t settings_seq, gaze_seq;
    FovSettings settings; /* global */
    FovGaze gaze;
    FovSlot slots[FOV_SHM_SLOTS];
} FovShm;

#ifdef __cplusplus
#include <cstddef>
/* The numbers every mirror checks. */
static_assert(sizeof(FovSettings) == 96 && offsetof(FovSettings, inner_deg) == 24 && offsetof(FovSettings, rate) == 32
              && offsetof(FovSettings, saccade_hold_ms) == 64);
static_assert(sizeof(FovGaze) == 48 && offsetof(FovGaze, qpc) == 16 && offsetof(FovGaze, eye) == 24);
static_assert(sizeof(FovStats) == 256 && offsetof(FovStats, gaze_valid_pct) == 92 && offsetof(FovStats, frames) == 104
              && offsetof(FovStats, focus) == 128 && offsetof(FovStats, last_error) == 160);
static_assert(sizeof(FovSlot) == 368 && offsetof(FovSlot, settings) == 8 && offsetof(FovSlot, stats) == 112);
static_assert(sizeof(FovShm) == 3112 && offsetof(FovShm, settings_seq) == 16 && offsetof(FovShm, settings) == 24
              && offsetof(FovShm, gaze) == 120 && offsetof(FovShm, slots) == 168);
#endif
#endif
