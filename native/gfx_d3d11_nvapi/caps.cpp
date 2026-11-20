// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fov_d3d11.h"
#include <nvapi.h>

namespace fov::d3d11 {

Caps query(ID3D11Device* device) {
    Caps caps;
    static const bool initialised = NvAPI_Initialize() == NVAPI_OK;
    caps.nvapi = initialised;
    if (!initialised || !device) return caps;
    NV_D3D1x_GRAPHICS_CAPS g = {};
    if (NvAPI_D3D1x_GetGraphicsCapabilities(device, NV_D3D1x_GRAPHICS_CAPS_VER, &g) == NVAPI_OK)
        caps.supported = g.bVariablePixelRateShadingSupported != 0;
    if (caps.supported) caps.tile_w = NV_VARIABLE_PIXEL_SHADING_TILE_WIDTH, caps.tile_h = NV_VARIABLE_PIXEL_SHADING_TILE_HEIGHT;
    return caps;
}

}  // namespace fov::d3d11
