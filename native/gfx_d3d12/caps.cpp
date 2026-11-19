#include "fov_d3d12.h"

namespace fov::d3d12 {

bool usable(int tier, unsigned tile) { return tier >= D3D12_VARIABLE_SHADING_RATE_TIER_2 && tile >= 2; }

Caps query(ID3D12Device* device) {
    Caps caps;
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 o = {};
    if (device && SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &o, sizeof(o)))) {
        caps.tier = o.VariableShadingRateTier;
        caps.tile = o.ShadingRateImageTileSize;
        caps.extra_rates = o.AdditionalShadingRatesSupported;
    }
    caps.usable = usable(caps.tier, caps.tile);
    return caps;
}

}  // namespace fov::d3d12
