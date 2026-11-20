// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

// Unit tests for the core, gaze and backend rules that need no GPU. Exit code = number of failures.
#include <cstdio>
#include <cstring>
#include "fov_core.h"
#include "fov_d3d12.h"
#include "fov_gaze.h"
#include "fov_vulkan.h"

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

static void presets() {
    FovSettings s;
    fov_settings_default(&s, FOV_PRESET_LOW);
    CHECK(s.inner_deg == 30 && s.mid_deg == 45 && s.rate[2] == FOV_RATE_2X2);
    fov_settings_default(&s, FOV_PRESET_HIGH);
    CHECK(s.inner_deg == 15 && s.rate[2] == FOV_RATE_4X4);
    fov_settings_default(&s, 99);
    CHECK(s.preset == FOV_PRESET_MEDIUM && s.inner_deg == 22 && s.mid_deg == 35);
    CHECK(s.enabled && s.mode == FOV_MODE_FIXED && s.saccade_deg_s == 180);
}

static void gaze_rules() {
    using fov::gaze::Status;
    FovGaze g = {};
    CHECK(fov::gaze::classify(g, 0) == Status::None);
    g.source = FOV_GAZE_TEST;
    CHECK(fov::gaze::classify(g, 0) == Status::Stale);  // not flagged valid
    g.flags = FOV_GAZE_VALID;
    CHECK(fov::gaze::classify(g, 99) == Status::Valid);
    CHECK(fov::gaze::classify(g, 101) == Status::Stale);
    CHECK(fov::gaze::classify(g, -5) == Status::Valid);
    g.flags |= FOV_GAZE_BLINK;
    CHECK(fov::gaze::classify(g, 10) == Status::Blink);
}

static void shm_round_trip() {
    CHECK(fov_shm_connect());
    FovSettings s;
    CHECK(fov_shm_read_settings(&s));
    FovGaze in = {};
    in.source = FOV_GAZE_TEST;
    in.flags = FOV_GAZE_VALID;
    in.qpc = fov_qpc_now();
    in.eye[0][0] = 0.25f, in.eye[0][1] = 0.5f, in.eye[1][0] = 0.75f, in.eye[1][1] = 0.5f;
    CHECK(fov_shm_write_gaze(&in));
    fov::gaze::Sample out;
    CHECK(fov::gaze::from_shm(out, fov_qpc_now()) == fov::gaze::Status::Valid);
    CHECK(out.source == FOV_GAZE_TEST && out.eye[0][0] == 0.25f && out.eye[1][0] == 0.75f);
    in.qpc -= (int64_t)(200.0 / fov_qpc_ms(0, 1));  // 200 ms ago
    CHECK(fov_shm_write_gaze(&in));
    CHECK(fov::gaze::from_shm(out, fov_qpc_now()) == fov::gaze::Status::Stale);
}

static void backend_rules() {
    CHECK(fov::vulkan::choose_texel(8, 32) == 8);
    CHECK(fov::vulkan::choose_texel(4, 16) == 8);
    CHECK(fov::vulkan::choose_texel(16, 32) == 16);
    CHECK(fov::vulkan::choose_texel(1, 4) == 0);
    CHECK(fov::vulkan::choose_texel(0, 0) == 0);
    CHECK(fov::d3d12::usable(2, 16) && fov::d3d12::usable(2, 8));
    CHECK(!fov::d3d12::usable(1, 16) && !fov::d3d12::usable(2, 0));
}

int main() {
    presets();
    gaze_rules();
    shm_round_trip();
    backend_rules();
    std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "ok", g_failures);
    return g_failures;
}
