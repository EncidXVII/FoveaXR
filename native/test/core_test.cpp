// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

// Unit tests for the core, gaze and backend rules that need no GPU. Exit code = number of failures.
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include "fov_core.h"
#include "fov_d3d12.h"
#include "fov_frame.h"
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

// The app's part: settings into the shared memory (the file FOV_SHM_FILE names, mapped a second time).
static void write_settings(uint32_t mode, uint32_t preset) {
    wchar_t path[MAX_PATH];
    GetEnvironmentVariableW(L"FOV_SHM_FILE", path, MAX_PATH);
    HANDLE file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE map = CreateFileMappingW(file, nullptr, PAGE_READWRITE, 0, sizeof(FovShm), nullptr);
    auto* shm = static_cast<FovShm*>(MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FovShm)));
    CHECK(shm != nullptr);
    if (!shm) return;
    FovSettings s;
    fov_settings_default(&s, preset);
    s.mode = mode;
    shm->settings_seq++;
    shm->settings = s;
    shm->settings_seq++;
    UnmapViewOfFile(shm);
    CloseHandle(map);
    CloseHandle(file);
}

static bool close_to(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// What a frame does with the settings, the gaze and the render targets a game binds.
static void frame_rules() {
    auto& core = fov::Core::get();
    CHECK(core.start(FOV_API_OPENXR));
    const fov::Fov fov[2] = {{-1.2f, 0.8f, 1.0f, -1.1f}, {-0.8f, 1.2f, 1.0f, -1.1f}};
    float t[8];

    // Fixed, Medium: the rings sit on the optical centre; the tint is yellow from the inner and red from the mid
    // radius.
    write_settings(FOV_MODE_FIXED, FOV_PRESET_MEDIUM);
    core.begin_frame(fov, nullptr, FOV_GAZE_NONE);
    CHECK(core.foveating());
    core.tint_params(0, t);
    CHECK(close_to(t[0], 0.6f) && close_to(t[1], 1.0f / 2.1f) && t[2] == 22 && t[3] == 35);
    // A gaze sample changes nothing in Fixed.
    const float gaze[2][2] = {{0.3f, 0.4f}, {0.7f, 0.45f}};
    core.begin_frame(fov, gaze, FOV_GAZE_TEST);
    core.tint_params(1, t);
    CHECK(close_to(t[0], 0.4f) && t[2] == 22);

    // Eye-tracked: the rings follow the gaze and are smaller. Low keeps full rate out to its mid radius and has
    // no 4 px ring: yellow from there, never red.
    write_settings(FOV_MODE_EYE, FOV_PRESET_LOW);
    core.begin_frame(fov, gaze, FOV_GAZE_TEST);
    core.tint_params(1, t);
    CHECK(close_to(t[0], 0.7f) && close_to(t[1], 0.45f) && close_to(t[2], 45 * 0.6f) && t[3] > 180);
    // No sample: Fixed for that frame, at Fixed's size.
    core.begin_frame(fov, nullptr, FOV_GAZE_NONE);
    core.tint_params(1, t);
    CHECK(close_to(t[0], 0.4f) && t[2] == 45);
    // A sample that is no number is no sample; one outside the image is taken at its edge.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float broken[2][2] = {{nan, 0.4f}, {0.7f, 0.45f}};
    core.begin_frame(fov, broken, FOV_GAZE_TEST);
    core.tint_params(1, t);
    CHECK(close_to(t[0], 0.4f) && t[2] == 45);
    const float outside[2][2] = {{-0.2f, 0.4f}, {1.3f, 0.45f}};
    core.begin_frame(fov, outside, FOV_GAZE_TEST);
    core.tint_params(0, t);
    CHECK(t[0] == 0);
    core.tint_params(1, t);
    CHECK(t[0] == 1 && close_to(t[2], 45 * 0.6f));
    // No projection yet: nothing is foveated.
    const fov::Fov none[2] = {{0, 0, 0, 0}, {0, 0, 0, 0}};
    core.begin_frame(none, nullptr, FOV_GAZE_NONE);
    CHECK(!core.foveating());
    // Mode Off.
    write_settings(FOV_MODE_OFF, FOV_PRESET_MEDIUM);
    core.begin_frame(fov, nullptr, FOV_GAZE_NONE);
    CHECK(!core.foveating());
    write_settings(FOV_MODE_FIXED, FOV_PRESET_MEDIUM);
    core.begin_frame(fov, nullptr, FOV_GAZE_NONE);
    CHECK(core.foveating());

    // Eye passes: an eye-sized colour target bound with depth of its size.
    core.set_eye_size(2000, 2200);
    core.set_layout(FOV_LAYOUT_SEPARATE);
    int eye = 0;
    CHECK(core.classify(&eye, 2000, 2200, 2000, 2200).foveate);
    CHECK(!core.classify(&eye, 2000, 2200, 0, 0).eye);            // no depth: UI, post
    CHECK(core.classify(&eye, 1600, 1760, 1600, 1760).eye);       // render scale 80%
    CHECK(!core.classify(&eye, 2048, 2048, 2048, 2048).eye);      // a shadow map
    CHECK(!core.classify(&eye, 500, 550, 500, 550).eye);          // too small to be an eye
    CHECK(!core.classify(&eye, 3200, 1760, 3200, 1760).eye);      // double-wide shape, not a double-wide game
    CHECK(core.classify(&eye, 2000, 2200, 2000, 2200).target == fov::Target::Both);  // which eye is not known
    core.register_eye_image(&eye, 1);
    CHECK(core.classify(&eye, 2000, 2200, 2000, 2200).target == fov::Target::Right);
    // A game that submits both eyes in one image renders them that way, at full size or at its render scale.
    core.set_layout(FOV_LAYOUT_WIDE);
    fov::Core::Bind wide = core.classify(nullptr, 4000, 2200, 4000, 2200);
    CHECK(wide.eye && wide.target == fov::Target::Wide);
    wide = core.classify(nullptr, 3200, 1760, 3200, 1760);
    CHECK(wide.eye && wide.target == fov::Target::Wide);
    CHECK(!core.classify(nullptr, 1920, 1200, 1920, 1200).eye);   // a desktop view
}

int main() {
    // Nothing of the PC it runs on: its own shared memory, no settings file, its own log.
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring dir = std::wstring(temp) + L"fov-core-test";
    const std::wstring shm = dir + L"/shm", log = dir + L"/log";
    CreateDirectoryW(dir.c_str(), nullptr);
    DeleteFileW(shm.c_str());  // the last run's: this one starts from an empty mapping
    DeleteFileW(log.c_str());
    SetEnvironmentVariableW(L"FOV_SHM_FILE", shm.c_str());
    SetEnvironmentVariableW(L"FOV_LOG_FILE", log.c_str());
    SetEnvironmentVariableW(L"APPDATA", dir.c_str());
    presets();
    gaze_rules();
    shm_round_trip();
    backend_rules();
    frame_rules();
    std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "ok", g_failures);
    return g_failures;
}
