#include <cmath>
#include "fov_frame.h"

namespace fov {

void optical_centre(const Fov& f, float uv[2]) {
    uv[0] = -f.l / (f.r - f.l);
    uv[1] = f.u / (f.u - f.d);
}

namespace {
void direction(const Fov& f, const float uv[2], float out[3]) {
    float x = f.l + uv[0] * (f.r - f.l), y = f.u - uv[1] * (f.u - f.d);
    float n = 1.0f / std::sqrt(x * x + y * y + 1.0f);
    out[0] = x * n, out[1] = y * n, out[2] = -n;
}
}  // namespace

float angle_deg(const Fov& f, const float a[2], const float b[2]) {
    float da[3], db[3];
    direction(f, a, da);
    direction(f, b, db);
    float dot = da[0] * db[0] + da[1] * db[1] + da[2] * db[2];
    return std::acos(std::fmin(1.0f, std::fmax(-1.0f, dot))) * 57.2957795f;
}

void build_pattern(const Rings& g, Target t, uint32_t w, uint32_t h, uint32_t tile, std::vector<uint8_t>& out) {
    uint32_t tw = (w + tile - 1) / tile, th = (h + tile - 1) / tile;
    out.resize(size_t(tw) * th);
    // Focus directions per eye, computed once.
    float focus_dir[2][3];
    for (int e = 0; e < 2; ++e) direction(g.fov[e], g.focus[e], focus_dir[e]);
    const float cos_inner = std::cos(g.inner_deg / 57.2957795f), cos_mid = std::cos(g.mid_deg / 57.2957795f);
    for (uint32_t y = 0; y < th; ++y) {
        for (uint32_t x = 0; x < tw; ++x) {
            float px = (x + 0.5f) * tile, py = (y + 0.5f) * tile;
            float best = -2;  // largest cosine = smallest angle to a focus point
            auto test = [&](int eye, float u, float v) {
                float uv[2] = {u, v}, d[3];
                direction(g.fov[eye], uv, d);
                best = std::fmax(best, d[0] * focus_dir[eye][0] + d[1] * focus_dir[eye][1] + d[2] * focus_dir[eye][2]);
            };
            float v = py / h;
            switch (t) {
            case Target::Left: test(0, px / w, v); break;
            case Target::Right: test(1, px / w, v); break;
            case Target::Both: test(0, px / w, v), test(1, px / w, v); break;
            case Target::Wide: px < w * 0.5f ? test(0, px / (w * 0.5f), v) : test(1, px / (w * 0.5f) - 1, v); break;
            }
            out[size_t(y) * tw + x] = uint8_t(best >= cos_inner ? g.rate[0] : best >= cos_mid ? g.rate[1] : g.rate[2]);
        }
    }
}

}  // namespace fov
