// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fov_vulkan.h"
#include <algorithm>
#include <cstring>
#include <vector>

namespace fov::vulkan {

uint32_t choose_texel(uint32_t min_size, uint32_t max_size) {
    for (uint32_t t = 8; t <= max_size; t *= 2)
        if (t >= min_size) return t;
    return 0;
}

Caps query(VkInstance instance, VkPhysicalDevice gpu, PFN_vkGetInstanceProcAddr gipa) {
    Caps caps;
    auto extensions = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
        gipa(instance, "vkEnumerateDeviceExtensionProperties"));
    auto features2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(gipa(instance, "vkGetPhysicalDeviceFeatures2"));
    auto properties2 =
        reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(gipa(instance, "vkGetPhysicalDeviceProperties2"));
    if (!extensions || !features2 || !properties2) return caps;  // Vulkan 1.0 instance

    uint32_t n = 0;
    extensions(gpu, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> list(n);
    extensions(gpu, nullptr, &n, list.data());
    for (const auto& e : list)
        if (!std::strcmp(e.extensionName, VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME)) caps.extension = true;
    if (!caps.extension) return caps;

    VkPhysicalDeviceFragmentShadingRateFeaturesKHR f = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
    VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f};
    features2(gpu, &f2);
    caps.attachment = f.attachmentFragmentShadingRate;

    VkPhysicalDeviceFragmentShadingRatePropertiesKHR p = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 p2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &p};
    properties2(gpu, &p2);
    // Square texels only: the rate image maps one texel to one square tile, like the D3D paths.
    caps.min_texel = std::max(p.minFragmentShadingRateAttachmentTexelSize.width,
                              p.minFragmentShadingRateAttachmentTexelSize.height);
    caps.max_texel = std::min(p.maxFragmentShadingRateAttachmentTexelSize.width,
                              p.maxFragmentShadingRateAttachmentTexelSize.height);
    if (caps.attachment) caps.texel = choose_texel(caps.min_texel, caps.max_texel);
    return caps;
}

}  // namespace fov::vulkan
