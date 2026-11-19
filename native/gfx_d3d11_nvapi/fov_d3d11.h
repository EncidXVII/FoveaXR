#pragma once
// D3D11 path (SPEC 5.1): NVIDIA only, through NVAPI variable pixel rate shading (Turing and newer).
#include <d3d11.h>

namespace fov::d3d11 {

struct Caps {
    bool nvapi = false;      // NVAPI initialised (NVIDIA driver present)
    bool supported = false;  // device reports variable pixel rate shading
    unsigned tile_w = 0, tile_h = 0;
};

// Safe on any adapter: a non-NVIDIA device reports nvapi=false. NVAPI stays loaded for the life of the process
// (unloading it during teardown can crash).
Caps query(ID3D11Device* device);

}  // namespace fov::d3d11
