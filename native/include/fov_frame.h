// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
// The per-process foveation state, shared by the VR API integration (OpenXR layer, OpenVR proxy) and the graphics
// backends. The VR side feeds frames, FOVs, eye images and gaze; the backends ask which render-target binds to
// foveate and with which rate pattern.
#include <cstdint>
#include <string>
#include <vector>
#include "fov_core.h"

namespace fov {

// What a rate image is built for.
enum class Target {
    Left,
    Right,
    Both,  // one image used for either eye (unknown eye, texture array): union of both eyes' rings
    Wide,  // one double-wide image: left eye in the left half, right eye in the right half
};

// Eye FOV as tangents; left and down are negative.
struct Fov {
    float l = -1, r = 1, u = 1, d = -1;
};

// Pure helpers, unit tested.
// Optical centre in UV (top-left origin).
void optical_centre(const Fov& f, float uv[2]);
// Angle in degrees between the view directions through two UV points of the same eye image.
float angle_deg(const Fov& f, const float a[2], const float b[2]);
// A rate per tile (FovRate) for an image of tiles_w x tiles_h tiles of `tile` px covering w x h px.
struct Rings {
    Fov fov[2];
    float focus[2][2];
    float inner_deg, mid_deg;
    uint32_t rate[3];
};
void build_pattern(const Rings& rings, Target t, uint32_t w, uint32_t h, uint32_t tile, std::vector<uint8_t>& out);
// Exe and folder checks of SPEC 8; returns FOV_REASON_ACTIVE or why not. `lists` holds the profile lists' text.
uint32_t safety_reason(const std::string& exe_lower, uint32_t app_id, const std::wstring& exe_dir,
                       const std::string& anticheat_list, const std::string& native_list);
// settings.toml: the keys the app writes. Unknown keys are ignored here (the app owns the file).
void apply_settings_toml(const std::string& text, FovSettings& s, std::vector<std::string>* off_games);
// Whole file as text, "" if missing; a file in the app's settings folder.
std::string read_text(const std::wstring& path);
std::wstring appdata_file(const wchar_t* name);

// The app's folders: settings, logs, the install. Windows AppData, or the Linux XDG folders through Z: inside
// Wine/Proton (the app runs natively there). For Flatpak or Snap Steam, the same folders under the sandbox's home.
enum class Host { Config, State, Install };
std::wstring host_dir(Host kind);
// Inside Wine/Proton under Flatpak or Snap Steam, whose sandbox has a home folder and /dev/shm of its own.
bool in_sandbox();

// Inline hook on a function (taken from a vtable); the detour reaches the original through *original.
bool hook(void* target, void* detour, void** original);

// Debug tint shader (D3D11 and D3D12): ring colours over an eye image. Constants: focus uv, the angles in
// degrees from which it is yellow (2 px blocks) and red (4 px blocks), then the eye's fov tangents l r u d. Drawn as one full-screen triangle into the eye's viewport.
constexpr char kTintHlsl[] = R"(
cbuffer P : register(b0) { float4 ring; float4 fov; };
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
V vs(uint id : SV_VertexID) {
    V o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float3 dir(float2 uv) { return normalize(float3(lerp(fov.x, fov.y, uv.x), lerp(fov.z, fov.w, uv.y), -1)); }
float4 ps(V i) : SV_Target {
    float a = degrees(acos(saturate(dot(dir(i.uv), dir(ring.xy)))));
    if (a < ring.z) discard;
    return a < ring.w ? float4(1, 0.8, 0, 0.2) : float4(1, 0.15, 0.1, 0.25);
}
)";

class Core {
public:
    static Core& get();

    // Process start: safety decision, per-game switch, stats slot. False = stay out of the way (zero hooks).
    bool start(FovApi api);
    bool active() const { return active_; }
    void set_graphics(FovGfx gfx);
    // The graphics path cannot foveate (no VRS): logged once, stats say so, everything passes through.
    void disable(FovReason reason, const char* why);

    // Per frame, from the VR API's frame thread. gaze_uv: per-eye focus from an eye tracker, or null.
    void begin_frame(const Fov fov[2], const float (*gaze_uv)[2], FovGazeSource source);
    void end_frame();
    uint64_t frame() const;

    void set_eye_size(uint32_t w, uint32_t h);
    void set_layout(FovLayout layout);
    void register_eye_image(const void* resource, int eye);  // 0 left, 1 right, 2 both (array)

    struct Bind {
        bool eye = false;      // an eye pass: timed in both A/B states
        bool foveate = false;  // and foveation is on: bind the rate image
        Target target = Target::Both;
    };
    // A render-target bind: colour target size, bound depth size (0 if none).
    Bind classify(const void* resource, uint32_t w, uint32_t h, uint32_t depth_w, uint32_t depth_h);

    bool foveating() const;
    uint64_t pattern_version() const;
    void pattern(Target t, uint32_t w, uint32_t h, uint32_t tile, std::vector<uint8_t>& out) const;
    // Debug tint: per eye focus (2), the angles in degrees from which it is yellow and red (2), fov (4).
    bool tint() const;
    void tint_params(int eye, float out[8]) const;

    void gpu_ms(double ms, bool foveated);

private:
    Core() = default;
    void publish();
    struct State;
    State* s_ = nullptr;
    bool active_ = false;
};

}  // namespace fov
