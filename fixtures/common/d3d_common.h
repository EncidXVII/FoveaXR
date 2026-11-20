// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
// D3D11 and D3D12 fixture helpers: adapter choice and runtime shader compilation of scene.hlsl.
#include <windows.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>
#include "fixture.h"
#include "scene_hlsl.h"

namespace fixture {
using Microsoft::WRL::ComPtr;

// --adapter if given (iGPU runs), else the runtime's adapter by LUID, else the first (the null driver names none).
inline ComPtr<IDXGIAdapter1> pick_adapter(LUID luid, int index) {
    ComPtr<IDXGIFactory1> factory;
    CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    ComPtr<IDXGIAdapter1> a;
    bool named = luid.LowPart || luid.HighPart;
    for (UINT i = 0; factory->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d;
        a->GetDesc1(&d);
        bool same = d.AdapterLuid.LowPart == luid.LowPart && d.AdapterLuid.HighPart == luid.HighPart;
        if (index >= 0 ? (int)i == index : named ? same : i == 0) {
            log("adapter %u: %ls", i, d.Description);
            return a;
        }
    }
    log("no adapter matches; using the default");
    return nullptr;
}

inline ComPtr<ID3DBlob> compile(const char* entry, const char* target) {
    ComPtr<ID3DBlob> code, errors;
    if (FAILED(D3DCompile(kSceneHlsl, sizeof(kSceneHlsl) - 1, "scene.hlsl", nullptr, nullptr, entry, target,
                          D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors))) {
        log("shader %s: %s", entry, errors ? (const char*)errors->GetBufferPointer() : "?");
        std::exit(3);
    }
    return code;
}

// sRGB colour preferred, else the runtime's first choice.
inline DXGI_FORMAT pick_format(const std::vector<int64_t>& offered) {
    for (int64_t want : {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB})
        for (int64_t f : offered)
            if (f == want) return (DXGI_FORMAT)f;
    return (DXGI_FORMAT)offered.at(0);
}

inline void check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        log("%s failed: 0x%08lx", what, (unsigned long)hr);
        std::exit(3);
    }
}

}  // namespace fixture
