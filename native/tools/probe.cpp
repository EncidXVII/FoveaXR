// fov_probe: prints, as one JSON line, what the core sees on this PC.
//   --gaze  the shared-memory gaze sample (the harness test checks its own writes come through)
//   --caps  variable rate shading support per adapter and graphics API
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "fov_core.h"
#include "fov_d3d11.h"
#include "fov_d3d12.h"
#include "fov_gaze.h"
#include "fov_vulkan.h"

using Microsoft::WRL::ComPtr;

namespace {
const char* status_name(fov::gaze::Status s) {
    switch (s) {
    case fov::gaze::Status::Valid: return "valid";
    case fov::gaze::Status::Blink: return "blink";
    case fov::gaze::Status::Stale: return "stale";
    default: return "none";
    }
}

int gaze() {
    fov::gaze::Sample s;
    auto status = fov::gaze::from_shm(s, fov_qpc_now());
    printf("{\"status\":\"%s\",\"source\":%d,\"kind\":%d,\"left\":[%.6f,%.6f,%.6f],\"right\":[%.6f,%.6f,%.6f]}\n",
           status_name(status), s.source, s.kind, s.eye[0][0], s.eye[0][1], s.eye[0][2], s.eye[1][0], s.eye[1][1],
           s.eye[1][2]);
    return 0;
}

std::string narrow(const wchar_t* w) {
    std::string s(WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), (int)s.size(), nullptr, nullptr);
    s.pop_back();
    return s;
}

// Vulkan through the system loader, if present: one entry per physical device.
std::string vulkan_caps() {
    HMODULE vk = LoadLibraryW(L"vulkan-1.dll");
    if (!vk) return "[]";
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(vk, "vkGetInstanceProcAddr"));
    auto create = reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance"));
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ci = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    VkInstance instance;
    if (!create || create(&ci, nullptr, &instance) != VK_SUCCESS) return "[]";
    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(instance, "vkEnumeratePhysicalDevices"));
    auto props = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa(instance, "vkGetPhysicalDeviceProperties"));
    uint32_t n = 0;
    enumerate(instance, &n, nullptr);
    std::vector<VkPhysicalDevice> gpus(n);
    enumerate(instance, &n, gpus.data());
    std::string out = "[";
    for (auto gpu : gpus) {
        VkPhysicalDeviceProperties p;
        props(gpu, &p);
        auto c = fov::vulkan::query(instance, gpu, gipa);
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "%s{\"name\":\"%s\",\"extension\":%s,\"attachment\":%s,\"min_texel\":%u,\"max_texel\":%u,\"texel\":%u}",
                 out.size() > 1 ? "," : "", p.deviceName, c.extension ? "true" : "false",
                 c.attachment ? "true" : "false", c.min_texel, c.max_texel, c.texel);
        out += buf;
    }
    reinterpret_cast<PFN_vkDestroyInstance>(gipa(instance, "vkDestroyInstance"))(instance, nullptr);
    return out + "]";
}

int caps() {
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return 1;
    std::string out = "{\"adapters\":[";
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d;
        adapter->GetDesc1(&d);
        if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        fov::d3d11::Caps c11;
        ComPtr<ID3D11Device> dev11;
        if (SUCCEEDED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                        D3D11_SDK_VERSION, &dev11, nullptr, nullptr)))
            c11 = fov::d3d11::query(dev11.Get());
        fov::d3d12::Caps c12;
        ComPtr<ID3D12Device> dev12;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev12))))
            c12 = fov::d3d12::query(dev12.Get());
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "%s{\"name\":\"%s\",\"vendor\":%u,\"d3d11\":{\"nvapi\":%s,\"vrs\":%s,\"tile\":%u},"
                 "\"d3d12\":{\"tier\":%d,\"tile\":%u,\"usable\":%s}}",
                 out.back() == '[' ? "" : ",", narrow(d.Description).c_str(), d.VendorId, c11.nvapi ? "true" : "false",
                 c11.supported ? "true" : "false", c11.tile_w, c12.tier, c12.tile, c12.usable ? "true" : "false");
        out += buf;
    }
    printf("%s],\"vulkan\":%s}\n", out.c_str(), vulkan_caps().c_str());
    return 0;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && !std::strcmp(argv[1], "--gaze")) return gaze();
    if (argc == 2 && !std::strcmp(argv[1], "--caps")) return caps();
    fprintf(stderr, "usage: fov_probe --gaze | --caps\n");
    return 2;
}
