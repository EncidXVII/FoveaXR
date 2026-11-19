#pragma once
// D3D12 path (SPEC 5.2): VRS Tier 2 shading-rate images on every vendor that has them.
#include <d3d12.h>

namespace fov::d3d12 {

struct Caps {
    int tier = 0;       // D3D12_VARIABLE_SHADING_RATE_TIER as a number
    unsigned tile = 0;  // ShadingRateImageTileSize: differs per vendor (8 or 16)
    bool usable = false;
    bool extra_rates = false;  // 2x4, 4x2, 4x4
};

// Tier 2 with a real tile size; anything less leaves the path off.
bool usable(int tier, unsigned tile);

Caps query(ID3D12Device* device);

// Hooks render-target binds on every command list of the device; eye passes get the rate image. Rate images are
// updated on `queue` (the queue the eye passes run on). False (and the core disabled) without VRS Tier 2.
bool install(ID3D12Device* device, ID3D12CommandQueue* queue);

// Debug tint over one eye's image, which must be in RENDER_TARGET state; recorded and run on the queue.
void tint(ID3D12Resource* texture, DXGI_FORMAT format, int eye, const RECT& rect, UINT slice);

}  // namespace fov::d3d12
