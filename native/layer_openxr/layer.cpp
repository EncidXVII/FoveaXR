// OpenXR API layer (SPEC 6.1). This phase: loader negotiation, pass-through, and a log line per session naming the
// graphics API and what that device supports for variable rate shading.
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <cstring>
#include "fov_core.h"
#include "fov_d3d11.h"
#include "fov_d3d12.h"
#include "fov_vulkan.h"
#include "negotiate.h"

namespace {
PFN_xrGetInstanceProcAddr g_next_gipa = nullptr;
PFN_xrCreateSession g_next_create_session = nullptr;

void log_graphics(const XrSessionCreateInfo* info) {
    for (auto* b = static_cast<const XrBaseInStructure*>(info->next); b; b = b->next) {
        if (b->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
            auto c = fov::d3d11::query(reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(b)->device);
            fov_log("session: D3D11, NVAPI %s, VRS %s, tile %ux%u", c.nvapi ? "yes" : "no", c.supported ? "yes" : "no",
                    c.tile_w, c.tile_h);
            return;
        }
        if (b->type == XR_TYPE_GRAPHICS_BINDING_D3D12_KHR) {
            auto c = fov::d3d12::query(reinterpret_cast<const XrGraphicsBindingD3D12KHR*>(b)->device);
            fov_log("session: D3D12, VRS tier %d, tile %u, usable %s", c.tier, c.tile, c.usable ? "yes" : "no");
            return;
        }
        if (b->type == XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR) {
            auto g = reinterpret_cast<const XrGraphicsBindingVulkanKHR*>(b);
            HMODULE vk = GetModuleHandleW(L"vulkan-1.dll");
            auto gipa = vk ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(vk, "vkGetInstanceProcAddr"))
                           : nullptr;
            if (!gipa) {
                fov_log("session: Vulkan, no vulkan-1.dll in the process");
                return;
            }
            auto c = fov::vulkan::query(g->instance, g->physicalDevice, gipa);
            fov_log("session: Vulkan, fragment shading rate %s, attachment %s, texel %u (%u..%u)",
                    c.extension ? "yes" : "no", c.attachment ? "yes" : "no", c.texel, c.min_texel, c.max_texel);
            return;
        }
    }
    fov_log("session: no D3D11, D3D12 or Vulkan binding");
}

XrResult XRAPI_CALL create_session(XrInstance instance, const XrSessionCreateInfo* info, XrSession* session) {
    XrResult r = g_next_create_session(instance, info, session);
    if (XR_SUCCEEDED(r)) log_graphics(info);
    return r;
}

XrResult XRAPI_CALL get_proc(XrInstance instance, const char* name, PFN_xrVoidFunction* fn) {
    if (!std::strcmp(name, "xrGetInstanceProcAddr")) {
        *fn = reinterpret_cast<PFN_xrVoidFunction>(get_proc);
        return XR_SUCCESS;
    }
    if (!std::strcmp(name, "xrCreateSession") && g_next_create_session) {
        *fn = reinterpret_cast<PFN_xrVoidFunction>(create_session);
        return XR_SUCCESS;
    }
    return g_next_gipa ? g_next_gipa(instance, name, fn) : XR_ERROR_HANDLE_INVALID;
}

XrResult XRAPI_CALL create_instance(const XrInstanceCreateInfo* info, const XrApiLayerCreateInfo* layer,
                                    XrInstance* instance) {
    XrResult r = fov::xr::create_next(info, layer, instance, g_next_gipa);
    if (XR_FAILED(r)) return r;
    g_next_gipa(*instance, "xrCreateSession", reinterpret_cast<PFN_xrVoidFunction*>(&g_next_create_session));
    fov_log("layer %s: instance for \"%s\" (engine \"%s\")", fov_version(), info->applicationInfo.applicationName,
            info->applicationInfo.engineName);
    return r;
}
}  // namespace

extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loader, const char* /*layerName*/, XrNegotiateApiLayerRequest* request) {
    return fov::xr::negotiate(loader, request, get_proc, create_instance);
}
