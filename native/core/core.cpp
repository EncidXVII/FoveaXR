// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include "fov_frame.h"

namespace fov {

namespace {
// Eye-tracked mode: the fovea is tracked, so the rings can be smaller (SPEC 3).
constexpr float kTrackedScale = 0.6f;
// Focus moves smaller than this (UV) keep the current rate images.
constexpr float kFocusEpsilon = 0.002f;
constexpr double kEmaAlpha = 0.05;
constexpr int kAbMinFrames = 60;

std::wstring module_dir() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&module_dir), &self);
    wchar_t p[MAX_PATH];
    std::wstring s(p, GetModuleFileNameW(self, p, MAX_PATH));
    return s.substr(0, s.find_last_of(L"\\/"));
}

// Profiles beside the DLL (installed layer), else in the install folder (the OpenVR proxy lives in a game).
std::string profile(const wchar_t* name) {
    std::string text = read_text(module_dir() + L"\\profiles\\" + name);
    if (text.empty()) text = read_text(host_dir(Host::Install) + L"\\profiles\\" + name);
    return text;
}

const char* reason_text(uint32_t r) {
    switch (r) {
    case FOV_REASON_GAME_OFF: return "turned off for this game";
    case FOV_REASON_ANTICHEAT_LIST: return "anti-cheat game (block list)";
    case FOV_REASON_ANTICHEAT_FILES: return "anti-cheat files in the game folder";
    case FOV_REASON_NATIVE_FOVEATION: return "game has its own foveated rendering";
    case FOV_REASON_NEVER: return "never runs in this process";
    case FOV_REASON_UNSUPPORTED_GPU: return "no variable rate shading on this GPU and API";
    default: return "error";
    }
}

bool pressed(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
}  // namespace

struct Core::State {
    std::mutex m;
    FovSettings settings = {};
    Rings rings = {};
    bool tracked = false;
    std::atomic<bool> fove{false}, tint{false}, disabled{false}, eye_mode{false};
    std::atomic<uint64_t> frame{0}, version{1}, binds{0}, skips{0}, uncertain{0}, passes{0};
    std::atomic<uint32_t> eye_w{0}, eye_h{0}, layout{FOV_LAYOUT_UNKNOWN};
    std::shared_mutex eyes_m;
    std::unordered_map<const void*, int> eyes;
    bool ab_off = false, f9 = false, f10 = false;
    int mode_override = -1;
    // The app's on/off and mode as last seen: a change there ends what the hotkeys set in this game.
    bool seen = false;
    uint32_t seen_enabled = 0, seen_mode = 0;
    int slot = -1;
    FovStats stats = {};
    double ema[2] = {0, 0};  // [foveated]
    int ema_frames[2] = {0, 0};
    double gaze_valid = 0;
};

Core& Core::get() {
    static Core core;
    return core;
}

bool Core::start(FovApi api) {
    if (s_) return active_;
    s_ = new State();
    wchar_t path[MAX_PATH];
    std::wstring exe_path(path, GetModuleFileNameW(nullptr, path, MAX_PATH));
    std::wstring exe_dir = exe_path.substr(0, exe_path.find_last_of(L"\\/"));
    std::string exe;
    for (wchar_t c : exe_path.substr(exe_dir.size() + 1)) exe += (char)towlower(c);
    char id[16] = "0";
    GetEnvironmentVariableA("SteamAppId", id, sizeof(id));  // set by Steam for the games it starts
    uint32_t app_id = (uint32_t)strtoul(id, nullptr, 10);

    uint32_t reason = safety_reason(exe, app_id, exe_dir, profile(L"blocklist_anticheat.toml"),
                                    profile(L"native_foveation.toml"));
    std::vector<std::string> off;
    FovSettings ignored;
    apply_settings_toml(read_text(appdata_file(L"settings.toml")), ignored, &off);
    for (auto& g : off)
        if (reason == FOV_REASON_ACTIVE && g == exe) reason = FOV_REASON_GAME_OFF;

    snprintf(s_->stats.exe, sizeof(s_->stats.exe), "%s", exe.c_str());
    s_->stats.app_id = app_id;
    s_->stats.api = api;
    s_->stats.reason = reason;
    active_ = reason == FOV_REASON_ACTIVE;
    s_->stats.active = active_;
    if (active_) {
        // Hooks and patched tables will point into this module: it stays loaded until the process ends. The OpenXR
        // loader unloads a layer with its instance, and a game may free openvr_api.dll, while both keep rendering.
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&module_dir), &self);
    }
    if (fov_shm_connect()) s_->slot = fov_shm_claim_slot();
    if (!fov_shm_read_settings(&s_->settings)) fov_settings_load(&s_->settings);
    fov_log("core %s: %s (app %u), %s: %s", fov_version(), exe.c_str(), app_id,
            api == FOV_API_OPENXR ? "OpenXR" : "OpenVR", active_ ? "active" : reason_text(reason));
    publish();
    return active_;
}

void Core::set_graphics(FovGfx gfx) {
    {
        std::lock_guard lock(s_->m);
        s_->stats.gfx = gfx;
    }
    publish();
}

void Core::disable(FovReason reason, const char* why) {
    s_->disabled = true;
    s_->fove = false;
    {
        std::lock_guard lock(s_->m);
        s_->stats.active = 0;
        s_->stats.reason = reason;
        snprintf(s_->stats.last_error, sizeof(s_->stats.last_error), "%s", why);
    }
    fov_log("off: %s", why);
    publish();
}

void Core::begin_frame(const Fov fov[2], const float (*gaze_uv)[2], FovGazeSource source) {
    if (!active_ || s_->disabled) return;
    // No projection yet (a runtime may answer zeros until tracking starts): nothing to centre the rings on, and a
    // pattern built from it would shade the whole image coarsely.
    for (int e = 0; e < 2; ++e) {
        if (!(fov[e].r > fov[e].l && fov[e].u > fov[e].d)) {
            s_->fove = false;
            return;
        }
    }
    FovSettings set;
    if (fov_shm_read_settings(&set)) s_->settings = set;
    set = s_->settings;
    // What the user sets in the app applies: a mode or an A/B state left by a hotkey would keep this game deaf to
    // the app's switch and Mode until it restarts.
    if (s_->seen && (s_->seen_enabled != set.enabled || s_->seen_mode != set.mode)) {
        if (s_->mode_override >= 0 || s_->ab_off) fov_log("the app's setting changed: hotkey state cleared");
        s_->mode_override = -1;
        s_->ab_off = false;
    }
    s_->seen = true, s_->seen_enabled = set.enabled, s_->seen_mode = set.mode;

    // An eye tracker's sample is used as it is, if it is a point in the eye image: clamped into it, and dropped
    // when it is no number at all (a pattern built around NaN would shade the whole image coarsely).
    float gaze[2][2] = {};
    bool have_gaze = gaze_uv != nullptr;
    for (int e = 0; e < 2 && have_gaze; ++e) {
        for (int i = 0; i < 2; ++i) {
            if (!std::isfinite(gaze_uv[e][i])) have_gaze = false;
            else gaze[e][i] = std::fmin(1.0f, std::fmax(0.0f, gaze_uv[e][i]));
        }
    }

    // Hotkeys, read here so they work without the app: Ctrl+F9 A/B, Ctrl+F10 cycles Off -> Fixed -> Eye-tracked.
    bool ctrl = pressed(VK_CONTROL), f9 = ctrl && pressed(VK_F9), f10 = ctrl && pressed(VK_F10);
    if (f9 && !s_->f9) {
        s_->ab_off = !s_->ab_off;
        {
            // Only the state now entered starts over. The one just left keeps its settled time, so both are there to
            // compare once this one has settled too (resetting both left one of them empty at all times).
            std::lock_guard lock(s_->m);
            s_->ema_frames[s_->ab_off ? 0 : 1] = 0;
        }
        fov_log("A/B: foveation %s", s_->ab_off ? "off" : "on");
    }
    if (f10 && !s_->f10) {
        int m = s_->mode_override >= 0 ? s_->mode_override : (int)set.mode;
        s_->mode_override = (m + 1) % 3;
        fov_log("mode: %s", s_->mode_override == FOV_MODE_OFF ? "off" : s_->mode_override == FOV_MODE_FIXED ? "fixed" : "eye-tracked");
    }
    s_->f9 = f9, s_->f10 = f10;
    uint32_t mode = s_->mode_override >= 0 ? (uint32_t)s_->mode_override : set.mode;
    s_->eye_mode = mode == FOV_MODE_EYE;

    Rings r = {};
    bool tracked = mode == FOV_MODE_EYE && have_gaze;
    for (int e = 0; e < 2; ++e) {
        r.fov[e] = fov[e];
        if (tracked) r.focus[e][0] = gaze[e][0], r.focus[e][1] = gaze[e][1];
        else optical_centre(fov[e], r.focus[e]);
    }
    float scale = tracked ? kTrackedScale : 1.0f;
    r.inner_deg = set.inner_deg * scale, r.mid_deg = set.mid_deg * scale;
    std::memcpy(r.rate, set.rate, sizeof(r.rate));
    {
        std::lock_guard lock(s_->m);
        const Rings& o = s_->rings;
        bool moved = std::memcmp(o.fov, r.fov, sizeof(r.fov)) || o.inner_deg != r.inner_deg ||
                     o.mid_deg != r.mid_deg || std::memcmp(o.rate, r.rate, sizeof(r.rate));
        for (int e = 0; e < 2; ++e)
            for (int i = 0; i < 2; ++i) moved |= std::fabs(o.focus[e][i] - r.focus[e][i]) > kFocusEpsilon;
        if (moved) {
            s_->rings = r;
            s_->version++;
        }
        s_->tracked = tracked;
        s_->gaze_valid += ((have_gaze ? 100.0 : 0.0) - s_->gaze_valid) * kEmaAlpha;
        s_->stats.gaze_source = have_gaze ? source : FOV_GAZE_NONE;
    }
    s_->tint = set.debug_tint != 0;
    s_->fove = set.enabled && mode != FOV_MODE_OFF && !s_->ab_off && !s_->disabled;
}

void Core::end_frame() {
    if (!active_) return;
    uint64_t f = ++s_->frame;
    if (f % 10 == 0) publish();
}

uint64_t Core::frame() const { return s_ ? s_->frame.load() : 0; }

void Core::set_eye_size(uint32_t w, uint32_t h) {
    if (s_->eye_w == w && s_->eye_h == h) return;
    s_->eye_w = w, s_->eye_h = h;
    // A game with dynamic resolution changes it every few frames: the first changes are enough for the log.
    static std::atomic<int> logged{0};
    constexpr int kLogged = 8;
    int n = logged.load();
    if (n > kLogged) return;
    logged = n + 1;
    if (n < kLogged) fov_log("eye size %ux%u", w, h);
    else fov_log("eye size keeps changing (dynamic resolution): not logged from here on");
}

void Core::set_layout(FovLayout layout) {
    if (s_->layout.exchange(layout) == (uint32_t)layout) return;
    std::lock_guard lock(s_->m);
    s_->stats.layout = layout;
    fov_log("layout: %s", layout == FOV_LAYOUT_ARRAY ? "texture array" : layout == FOV_LAYOUT_WIDE ? "double wide" : "separate");
}

void Core::register_eye_image(const void* resource, int eye) {
    std::unique_lock lock(s_->eyes_m);
    s_->eyes[resource] = eye;
}

Core::Bind Core::classify(const void* resource, uint32_t w, uint32_t h, uint32_t dw, uint32_t dh) {
    Bind b;
    uint32_t ew = s_->eye_w, eh = s_->eye_h;
    if (!active_ || s_->disabled || !ew || !w || !h) return b;
    auto close_to = [](uint32_t a, uint32_t b, uint32_t tol) { return a + tol >= b && b + tol >= a; };
    bool wide = close_to(w, 2 * ew, 4) && close_to(h, eh, 2);
    bool exact = close_to(w, ew, 2) && close_to(h, eh, 2);
    // Render scale / upscaler input: same aspect, at least a third of the eye width.
    auto shaped = [&](double aspect) { return std::fabs(double(w) / h - aspect) < 0.01 * aspect; };
    bool scaled = !exact && !wide && w * 3 >= ew && shaped(double(ew) / eh);
    // The same for both eyes side by side (an engine that submits double wide renders double wide at its render
    // scale). Only in a game that submits that way: the shape alone is close to a 16:9 desktop view.
    bool scaled_wide = !exact && !wide && !scaled && s_->layout == FOV_LAYOUT_WIDE && w * 3 >= 2 * ew &&
                       shaped(2.0 * ew / eh);
    wide |= scaled_wide;
    if (w == h && ew != eh) return b;  // shadow maps and the like
    if (!exact && !wide && !scaled) return b;
    // Scene passes bind depth of the same size; full-screen post and UI passes bind none.
    if (dw != w || dh != h) {
        s_->skips++;
        return b;
    }
    b.eye = true;
    s_->passes++;
    b.foveate = s_->fove;
    if (wide) {
        b.target = Target::Wide;
    } else {
        std::shared_lock lock(s_->eyes_m);
        auto it = s_->eyes.find(resource);
        b.target = it == s_->eyes.end() || it->second == 2 ? Target::Both : it->second ? Target::Right : Target::Left;
        if (it == s_->eyes.end() && b.foveate) s_->uncertain++;
    }
    if (b.foveate) s_->binds++;
    return b;
}

bool Core::foveating() const { return s_ && s_->fove; }
bool Core::eye_mode() const { return s_ && s_->eye_mode; }
uint64_t Core::pattern_version() const { return s_->version; }

void Core::pattern(Target t, uint32_t w, uint32_t h, uint32_t tile, std::vector<uint8_t>& out) const {
    Rings r;
    {
        std::lock_guard lock(s_->m);
        r = s_->rings;
    }
    build_pattern(r, t, w, h, tile, out);
}

bool Core::tint() const { return s_ && s_->tint && s_->fove; }

void Core::tint_params(int eye, float out[8]) const {
    std::lock_guard lock(s_->m);
    const Rings& r = s_->rings;
    // The tint says what is shaded how: yellow from where the rate first drops (2 px blocks), red from where it
    // reaches 4 px blocks. Low keeps full rate out to its mid radius and never goes past 2x2: no tint inside the
    // mid radius, no red at all.
    const float edge[3] = {0, r.inner_deg, r.mid_deg};
    float yellow = 181, red = 181;  // further than any angle between two view directions
    for (int i = 2; i >= 0; --i) {
        if (r.rate[i] >= FOV_RATE_2X1) yellow = edge[i];
        if (r.rate[i] >= FOV_RATE_4X2) red = edge[i];
    }
    out[0] = r.focus[eye][0], out[1] = r.focus[eye][1], out[2] = yellow, out[3] = red;
    out[4] = r.fov[eye].l, out[5] = r.fov[eye].r, out[6] = r.fov[eye].u, out[7] = r.fov[eye].d;
}

void Core::gpu_ms(double ms, bool foveated) {
    std::lock_guard lock(s_->m);
    int i = foveated ? 1 : 0;
    s_->ema[i] = s_->ema_frames[i]++ ? s_->ema[i] + (ms - s_->ema[i]) * kEmaAlpha : ms;
}

void Core::publish() {
    if (!s_ || s_->slot < 0) return;
    FovStats st;
    {
        std::lock_guard lock(s_->m);
        st = s_->stats;
        st.frames = s_->frame;
        st.rings_bound = s_->binds;
        st.ui_skipped = s_->skips;
        st.eye_passes = s_->passes;
        st.uncertain_eye = s_->uncertain;
        st.gaze_valid_pct = (float)s_->gaze_valid;
        // Only once both A/B states have settled: a fresh toggle would compare mismatched scenes.
        st.eye_ms_on = s_->ema_frames[1] >= kAbMinFrames ? (float)s_->ema[1] : 0;
        st.eye_ms_off = s_->ema_frames[0] >= kAbMinFrames ? (float)s_->ema[0] : 0;
        for (int e = 0; e < 2; ++e) st.focus[e][0] = s_->rings.focus[e][0], st.focus[e][1] = s_->rings.focus[e][1];
    }
    fov_shm_write_stats(s_->slot, &st);
}

}  // namespace fov
