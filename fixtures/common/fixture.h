#pragma once
// What every fixture shares: arguments, frame constants, the UI rectangle, logging.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace fixture {

struct Args {
    int frames = 600;    // exit cleanly after this many submitted frames; 0 = run until the runtime stops us
    int heavy = 64;      // iterations of the heavy pixel loop
    int adapter = -1;    // DXGI adapter index when the runtime names none (null driver); -1 = first
    std::string gfx = "d3d11";  // OpenXR fixture: d3d11, d3d12, vulkan
    std::string dump;           // D3D11: write each eye's last frame to <dump>_eye<N>.bmp
};

inline Args parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--frames")) a.frames = std::atoi(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--heavy")) a.heavy = std::atoi(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--adapter")) a.adapter = std::atoi(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--gfx")) a.gfx = argv[i + 1];
        else if (!std::strcmp(argv[i], "--dump")) a.dump = argv[i + 1];
    }
    return a;
}

// Mirrors FrameData in scene.hlsl.
struct Constants {
    float tan[4];     // left, right, up, down
    float params[4];  // seconds, heavy iterations, 0, 0
};

inline Constants constants(const float tan[4], long long frame, int heavy) {
    return {{tan[0], tan[1], tan[2], tan[3]}, {frame / 90.0f, (float)heavy, 0, 0}};
}

// UI pass rectangle in eye-image UV: x0, y0, x1, y1.
constexpr float kUi[4] = {0.3f, 0.62f, 0.7f, 0.76f};

// One line per event on stdout, flushed: the harness reads it live.
inline void log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::printf("fixture: ");
    std::vprintf(fmt, args);
    std::printf("\n");
    std::fflush(stdout);
    va_end(args);
}

}  // namespace fixture
