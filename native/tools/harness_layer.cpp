// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

// fov_harness_layer: test-only OpenXR layer, loaded by the harness next to the runtime. The SteamVR null driver
// has no display adapter, so the runtime reports LUID 0 and cannot name a Vulkan device; apps that look their GPU
// up by LUID (hello_xr) or ask for the Vulkan device then fail. This layer answers with the adapter at index
// FOV_HARNESS_ADAPTER (default 0), which is also how GPU gates pick the NVIDIA or the AMD GPU. Never shipped.
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <vulkan/vulkan.h>
#include <wrl/client.h>
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D12
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "fov_core.h"
#include "negotiate.h"

namespace {
PFN_xrGetInstanceProcAddr g_next = nullptr;
PFN_xrGetD3D11GraphicsRequirementsKHR g_d3d11 = nullptr;
PFN_xrGetD3D12GraphicsRequirementsKHR g_d3d12 = nullptr;
PFN_xrGetVulkanGraphicsDevice2KHR g_vulkan = nullptr;

LUID chosen_luid() {
    char buf[16] = "0";
    GetEnvironmentVariableA("FOV_HARNESS_ADAPTER", buf, sizeof(buf));
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 d = {};
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->EnumAdapters1(std::atoi(buf), &adapter)))
        adapter->GetDesc1(&d);
    return d.AdapterLuid;
}

bool unnamed(const LUID& l) { return !l.LowPart && !l.HighPart; }

XrResult XRAPI_CALL d3d11_requirements(XrInstance i, XrSystemId s, XrGraphicsRequirementsD3D11KHR* r) {
    XrResult res = g_d3d11(i, s, r);
    if (XR_SUCCEEDED(res) && unnamed(r->adapterLuid)) r->adapterLuid = chosen_luid();
    return res;
}

XrResult XRAPI_CALL d3d12_requirements(XrInstance i, XrSystemId s, XrGraphicsRequirementsD3D12KHR* r) {
    XrResult res = g_d3d12(i, s, r);
    if (XR_SUCCEEDED(res) && unnamed(r->adapterLuid)) r->adapterLuid = chosen_luid();
    return res;
}

XrResult XRAPI_CALL vulkan_device(XrInstance i, const XrVulkanGraphicsDeviceGetInfoKHR* info, VkPhysicalDevice* out) {
    XrResult res = g_vulkan(i, info, out);
    if (XR_SUCCEEDED(res)) return res;
    HMODULE vk = GetModuleHandleW(L"vulkan-1.dll");
    auto gipa = vk ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(vk, "vkGetInstanceProcAddr")) : nullptr;
    if (!gipa) return res;
    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        gipa(info->vulkanInstance, "vkEnumeratePhysicalDevices"));
    auto props2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
        gipa(info->vulkanInstance, "vkGetPhysicalDeviceProperties2"));
    uint32_t n = 0;
    enumerate(info->vulkanInstance, &n, nullptr);
    std::vector<VkPhysicalDevice> gpus(n);
    enumerate(info->vulkanInstance, &n, gpus.data());
    LUID want = chosen_luid();
    for (auto gpu : gpus) {
        VkPhysicalDeviceIDProperties id = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 p = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &id};
        props2(gpu, &p);
        if (id.deviceLUIDValid && !std::memcmp(id.deviceLUID, &want, sizeof(want))) {
            fov_log("harness layer: runtime named no Vulkan device (%d); using %s", (int)res, p.properties.deviceName);
            *out = gpu;
            return XR_SUCCESS;
        }
    }
    return res;
}

template <typename F>
bool wrap(const char* name, const char* want, F ours, F& next, XrInstance instance, PFN_xrVoidFunction* fn,
          XrResult& r) {
    if (std::strcmp(name, want)) return false;
    r = g_next(instance, name, reinterpret_cast<PFN_xrVoidFunction*>(&next));
    if (XR_SUCCEEDED(r)) *fn = reinterpret_cast<PFN_xrVoidFunction>(ours);
    return true;
}

XrResult XRAPI_CALL get_proc(XrInstance instance, const char* name, PFN_xrVoidFunction* fn) {
    if (!std::strcmp(name, "xrGetInstanceProcAddr")) {
        *fn = reinterpret_cast<PFN_xrVoidFunction>(get_proc);
        return XR_SUCCESS;
    }
    if (!g_next) return XR_ERROR_HANDLE_INVALID;
    XrResult r = XR_SUCCESS;
    if (wrap("xrGetD3D11GraphicsRequirementsKHR", name, &d3d11_requirements, g_d3d11, instance, fn, r) ||
        wrap("xrGetD3D12GraphicsRequirementsKHR", name, &d3d12_requirements, g_d3d12, instance, fn, r) ||
        wrap("xrGetVulkanGraphicsDevice2KHR", name, &vulkan_device, g_vulkan, instance, fn, r))
        return r;
    return g_next(instance, name, fn);
}

XrResult XRAPI_CALL create_instance(const XrInstanceCreateInfo* info, const XrApiLayerCreateInfo* layer,
                                    XrInstance* instance) {
    return fov::xr::create_next(info, layer, instance, g_next);
}
}  // namespace

extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loader, const char* /*layerName*/, XrNegotiateApiLayerRequest* request) {
    return fov::xr::negotiate(loader, request, get_proc, create_instance);
}
