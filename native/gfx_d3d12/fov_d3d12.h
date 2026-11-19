#pragma once
// D3D12 path (SPEC 5.2): VRS Tier 2 shading-rate images on every vendor that has them.
#include <d3d12.h>

namespace fov::d3d12 {

struct Caps {
    int tier = 0;       // D3D12_VARIABLE_SHADING_RATE_TIER as a number
    unsigned tile = 0;  // ShadingRateImageTileSize: differs per vendor (8 or 16)
    bool usable = false;
};

// Tier 2 with a real tile size; anything less leaves the path off.
bool usable(int tier, unsigned tile);

Caps query(ID3D12Device* device);

}  // namespace fov::d3d12
