// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

// Fixture scene, shared by every fixture and graphics API. The eye pass draws a full-screen wall at depth 0.5 with a
// high-frequency pattern fixed in view angle (0.5 degree checks + rings) and a heavy per-pixel loop, so shading
// cost dominates and coarse shading is visible. The UI pass draws stripes into a sub-rectangle with no depth bound.
// Same frame index -> same image: the harness compares frames across runs.
struct FrameData {
    float4 tan;     // eye FOV tangents: left, right, up, down (left and down negative)
    float4 params;  // x: seconds (frame index / 90), y: heavy iterations
};
#ifdef __spirv__
[[vk::push_constant]] FrameData frame;
#else
cbuffer Frame : register(b0) { FrameData frame; };
#endif

struct VsOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;  // 0..1 from the top-left
};

VsOut vs_main(uint id : SV_VertexID) {
    VsOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0.5, 1);
    return o;
}

float4 ps_scene(VsOut i) : SV_Target {
    float2 t = float2(lerp(frame.tan.x, frame.tan.y, i.uv.x), lerp(frame.tan.z, frame.tan.w, i.uv.y));
    float2 deg = degrees(float2(atan(t.x), atan(t.y / sqrt(1 + t.x * t.x))));
    float checks = (int(floor(deg.x * 2) + floor(deg.y * 2)) & 1) ? 1.0 : 0.0;
    float rings = 0.5 + 0.5 * sin(length(deg) * 6.0);
    float2 p = deg * 0.05;
    float heavy = 0;
    int n = (int)frame.params.y;
    for (int k = 0; k < n; ++k) {
        // Bounded by construction (sin/cos), so long loops never overflow to NaN.
        p = float2(sin(p.y * 1.9 + k * 0.37 + frame.params.x), cos(p.x * 1.7 - k * 0.23));
        heavy += abs(sin(p.x * 3.1 + p.y * 1.7));
    }
    heavy /= max(n, 1);
    float3 c = lerp(float3(0.08, 0.08, 0.1), float3(0.92, 0.9, 0.86), checks);
    return float4(c * (0.6 + 0.4 * rings) * (0.75 + 0.25 * heavy), 1);
}

float4 ps_ui(VsOut i) : SV_Target {
    float text = step(0.5, frac(i.uv.y * 8)) * step(0.15, frac(i.uv.x * 24));
    return float4(lerp(float3(0.05, 0.05, 0.06), float3(0.78, 1.0, 0.1), text), 1);
}
