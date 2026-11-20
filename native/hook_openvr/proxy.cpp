// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

// openvr_api.dll proxy (SPEC 6.2): every export forwards to the game's original, renamed openvr_api.fov-orig.dll
// beside it. In an active process the compositor the game gets is wrapped: WaitGetPoses returning starts a frame,
// Submit tells which texture is which eye and installs the graphics backend, the second eye ends the frame.
// Any failure leaves pure forwarding.
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <openvr.h>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include "exports.h"
#include "fov_core.h"
#include "fov_d3d11.h"
#include "fov_d3d12.h"
#include "fov_frame.h"
#include "fov_gaze.h"

namespace {
#define FOV_NAME(n) #n,
constexpr const char* kNames[] = {FOV_OPENVR_EXPORTS(FOV_NAME)};
#undef FOV_NAME
constexpr size_t kCount = sizeof(kNames) / sizeof(kNames[0]);
constexpr size_t kGetGenericInterface = 5, kShutdown = 17;  // indices in exports.h

// What a stub lands on when the original lacks that export (an older openvr_api) or is missing: 0 / null / false
// for every return type the API uses.
uintptr_t missing_export() { return 0; }

std::wstring own_dir() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&own_dir), &self);
    wchar_t path[MAX_PATH];
    std::wstring s(path, GetModuleFileNameW(self, path, MAX_PATH));
    return s.substr(0, s.find_last_of(L"\\/") + 1);
}

using GetInterface = void*(VR_CALLTYPE*)(const char*, vr::EVRInitError*);
GetInterface g_get_interface = nullptr;
vr::IVRSystem* g_system = nullptr;
bool g_gaze_device = false;

// --- frame ----------------------------------------------------------------------------------------------------

void begin_frame() {
    auto& core = fov::Core::get();
    if (!g_system) {
        vr::EVRInitError e;
        g_system = static_cast<vr::IVRSystem*>(g_get_interface(vr::IVRSystem_Version, &e));
        if (!g_system) return;
        g_gaze_device = g_system->GetBoolTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd,
                                                               vr::Prop_SupportsXrEyeGazeInteraction_Bool);
        fov_log("openvr: eye gaze %s", g_gaze_device ? "supported" : "not supported by this headset");
    }
    fov::Fov fov[2];
    for (int e = 0; e < 2; ++e) {
        float l, r, t, b;
        g_system->GetProjectionRaw((vr::EVREye)e, &l, &r, &t, &b);
        fov[e] = {l, r, -t, -b};  // OpenVR's raw projection is y-down
    }
    float uv[2][2];
    FovGazeSource source = FOV_GAZE_OPENVR_CENTER;
    vr::HmdVector2_t ndc[2];
    bool have = g_gaze_device && g_system->GetEyeTrackedFoveationCenter(&ndc[0], &ndc[1]);
    if (have) {
        for (int e = 0; e < 2; ++e) uv[e][0] = (ndc[e].v[0] + 1) / 2, uv[e][1] = (1 - ndc[e].v[1]) / 2;
    } else {
        fov::gaze::Sample s;
        if (fov::gaze::from_shm(s, fov_qpc_now()) == fov::gaze::Status::Valid && s.kind == FOV_GAZE_UV) {
            for (int e = 0; e < 2; ++e) uv[e][0] = s.eye[e][0], uv[e][1] = s.eye[e][1];
            source = s.source;
            have = true;
        }
    }
    core.begin_frame(fov, have ? uv : nullptr, source);
}

// The first submit names the graphics API; every submit names its eye's texture and size.
void submitted(vr::EVREye eye, const vr::Texture_t* t, const vr::VRTextureBounds_t* bounds) {
    auto& core = fov::Core::get();
    if (!t || !t->handle || eye > vr::Eye_Right) return;
    static bool installed = false;
    float bu = 1, bv = 1;
    if (bounds) bu = std::fabs(bounds->uMax - bounds->uMin), bv = std::fabs(bounds->vMax - bounds->vMin);
    bool wide = bu < 0.75f;  // each eye a half of one texture
    UINT w = 0, h = 0;
    if (t->eType == vr::TextureType_DirectX) {
        auto* tex = static_cast<ID3D11Texture2D*>(t->handle);
        D3D11_TEXTURE2D_DESC d;
        tex->GetDesc(&d);
        w = d.Width, h = d.Height;
        if (!installed) {
            installed = true;
            ID3D11Device* dev = nullptr;
            tex->GetDevice(&dev);
            fov::d3d11::install(dev);
            dev->Release();  // the context reference the backend keeps holds the device
        }
        if (core.tint()) {
            RECT r = {LONG(bounds ? bounds->uMin * w : 0), LONG(bounds ? bounds->vMin * h : 0),
                      LONG(bounds ? bounds->uMax * w : w), LONG(bounds ? bounds->vMax * h : h)};
            fov::d3d11::tint(tex, d.Format, eye, r, 0);
        }
    } else if (t->eType == vr::TextureType_DirectX12) {
        auto* data = static_cast<const vr::D3D12TextureData_t*>(t->handle);
        D3D12_RESOURCE_DESC d = data->m_pResource->GetDesc();
        w = (UINT)d.Width, h = d.Height;
        if (!installed) {
            installed = true;
            ID3D12Device* dev = nullptr;
            data->m_pResource->GetDevice(IID_PPV_ARGS(&dev));
            fov::d3d12::install(dev, data->m_pCommandQueue);
            dev->Release();
        }
    } else {
        if (!installed) {
            installed = true;
            core.set_graphics(t->eType == vr::TextureType_Vulkan ? FOV_GFX_VULKAN : FOV_GFX_NONE);
            core.disable(FOV_REASON_UNSUPPORTED_GPU, "graphics API not supported (D3D11 and D3D12 only)");
        }
        return;
    }
    core.set_layout(wide ? FOV_LAYOUT_WIDE : FOV_LAYOUT_SEPARATE);
    core.set_eye_size(UINT(w * bu + 0.5f), UINT(h * bv + 0.5f));
    if (!wide) core.register_eye_image(t->eType == vr::TextureType_DirectX12
                                           ? static_cast<const vr::D3D12TextureData_t*>(t->handle)->m_pResource
                                           : t->handle,
                                       eye);
}

// --- compositor wrapping: C++ interfaces (vtable slots) and FnTable (the C API Unity uses) --------------------

using WaitFn = vr::EVRCompositorError (*)(void*, vr::TrackedDevicePose_t*, uint32_t, vr::TrackedDevicePose_t*, uint32_t);
using SubmitFn = vr::EVRCompositorError (*)(void*, vr::EVREye, const vr::Texture_t*, const vr::VRTextureBounds_t*,
                                            vr::EVRSubmitFlags);
using FnWait = vr::EVRCompositorError (*)(vr::TrackedDevicePose_t*, uint32_t, vr::TrackedDevicePose_t*, uint32_t);
using FnSubmit = vr::EVRCompositorError (*)(vr::EVREye, const vr::Texture_t*, const vr::VRTextureBounds_t*,
                                            vr::EVRSubmitFlags);

struct Patched {
    void** table;
    WaitFn wait;
    SubmitFn submit;
};
Patched g_patched[8];
int g_patched_count = 0;
FnWait g_fn_wait = nullptr;
FnSubmit g_fn_submit = nullptr;
std::mutex g_patch_m;

Patched* originals(void* self) {
    void** table = *static_cast<void***>(self);
    for (int i = 0; i < g_patched_count; ++i)
        if (g_patched[i].table == table) return &g_patched[i];
    return nullptr;
}

vr::EVRCompositorError wait_hook(void* self, vr::TrackedDevicePose_t* a, uint32_t n, vr::TrackedDevicePose_t* b,
                                 uint32_t m) {
    vr::EVRCompositorError r = originals(self)->wait(self, a, n, b, m);
    begin_frame();
    return r;
}

vr::EVRCompositorError submit_hook(void* self, vr::EVREye eye, const vr::Texture_t* t, const vr::VRTextureBounds_t* b,
                                   vr::EVRSubmitFlags f) {
    submitted(eye, t, b);
    vr::EVRCompositorError r = originals(self)->submit(self, eye, t, b, f);
    if (eye == vr::Eye_Right) fov::Core::get().end_frame();
    return r;
}

vr::EVRCompositorError fn_wait_hook(vr::TrackedDevicePose_t* a, uint32_t n, vr::TrackedDevicePose_t* b, uint32_t m) {
    vr::EVRCompositorError r = g_fn_wait(a, n, b, m);
    begin_frame();
    return r;
}

vr::EVRCompositorError fn_submit_hook(vr::EVREye eye, const vr::Texture_t* t, const vr::VRTextureBounds_t* b,
                                      vr::EVRSubmitFlags f) {
    submitted(eye, t, b);
    vr::EVRCompositorError r = g_fn_submit(eye, t, b, f);
    if (eye == vr::Eye_Right) fov::Core::get().end_frame();
    return r;
}

bool write_slot(void** slot, void* value) {
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return false;
    *slot = value;
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

// IVRCompositor_009 .. 028 keep Submit at slot 5; 029 added GetSubmitTexture before it. Older versions had another
// Submit and are left alone.
int submit_slot(int version) { return version >= 29 ? 6 : version >= 9 ? 5 : -1; }

void wrap(const char* name, void* iface) {
    bool fn_table = !std::strncmp(name, "FnTable:", 8);
    const char* v = fn_table ? name + 8 : name;
    if (std::strncmp(v, "IVRCompositor_", 14)) return;
    int version = std::atoi(v + 14), slot = submit_slot(version);
    if (slot < 0) return;
    std::lock_guard lock(g_patch_m);
    if (fn_table) {
        // A table of plain function pointers in the same order as the vtable.
        void** table = static_cast<void**>(iface);
        if (table[2] == (void*)&fn_wait_hook) return;
        g_fn_wait = (FnWait)table[2], g_fn_submit = (FnSubmit)table[slot];
        if (write_slot(&table[2], (void*)&fn_wait_hook) && write_slot(&table[slot], (void*)&fn_submit_hook))
            fov_log("openvr: wrapped %s", name);
        return;
    }
    void** table = *static_cast<void***>(iface);
    if (table[2] == (void*)&wait_hook) return;
    // A table we know, found unpatched: the game shut OpenVR down and started it again, and the runtime's DLL came
    // back at the same address with fresh tables.
    Patched* p = originals(iface);
    if (!p) {
        if (g_patched_count == 8) return;
        p = &g_patched[g_patched_count++];
    }
    // Recorded before the slots change: a call may arrive between the two writes.
    *p = {table, (WaitFn)table[2], (SubmitFn)table[slot]};
    if (write_slot(&table[2], (void*)&wait_hook) && write_slot(&table[slot], (void*)&submit_hook))
        fov_log("openvr: wrapped %s", name);
}

// A game that shuts OpenVR down and starts it again gets new interfaces: ours are fetched again too.
using Shutdown = void(VR_CALLTYPE*)();
Shutdown g_shutdown = nullptr;
void VR_CALLTYPE shutdown() {
    g_system = nullptr;
    g_shutdown();
}

void* VR_CALLTYPE get_interface(const char* name, vr::EVRInitError* err) {
    void* iface = g_get_interface(name, err);
    if (iface && name) wrap(name, iface);
    return iface;
}
}  // namespace

extern "C" void* fov_proxy_targets[kCount] = {};

extern "C" void* fov_proxy_resolve(uint64_t index) {
    static std::once_flag once;
    std::call_once(once, [] {
        // Full path: the search order must never find this proxy again, or a stray copy elsewhere.
        std::wstring path = own_dir() + L"openvr_api.fov-orig.dll";
        HMODULE orig = LoadLibraryW(path.c_str());
        if (!orig) fov_log("openvr proxy: cannot load %ls (%lu); every call returns 0", path.c_str(), GetLastError());
        for (size_t i = 0; i < kCount; ++i) {
            FARPROC f = orig ? GetProcAddress(orig, kNames[i]) : nullptr;
            fov_proxy_targets[i] = f ? reinterpret_cast<void*>(f) : reinterpret_cast<void*>(&missing_export);
        }
        if (!orig || !fov::Core::get().start(FOV_API_OPENVR)) return;
        g_get_interface = reinterpret_cast<GetInterface>(fov_proxy_targets[kGetGenericInterface]);
        if (g_get_interface != reinterpret_cast<void*>(&missing_export))
            fov_proxy_targets[kGetGenericInterface] = reinterpret_cast<void*>(&get_interface);
        g_shutdown = reinterpret_cast<Shutdown>(fov_proxy_targets[kShutdown]);
        fov_proxy_targets[kShutdown] = reinterpret_cast<void*>(&shutdown);
    });
    return index < kCount ? fov_proxy_targets[index] : reinterpret_cast<void*>(&missing_export);
}
