#pragma once
// Vulkan path (SPEC 5.3): VK_KHR_fragment_shading_rate attachments, on every vendor that exposes them.
#include <cstdint>
#include <vulkan/vulkan.h>

namespace fov::vulkan {

struct Caps {
    bool extension = false;   // device lists VK_KHR_fragment_shading_rate
    bool attachment = false;  // attachmentFragmentShadingRate feature
    uint32_t min_texel = 0, max_texel = 0;  // square bounds of the attachment texel size
    uint32_t texel = 0;       // chosen texel size, 0 when the path is off
};

// Smallest supported square power-of-two texel size >= 8, or 0 if none fits.
uint32_t choose_texel(uint32_t min_size, uint32_t max_size);

// gipa: the vkGetInstanceProcAddr of the instance that owns `gpu` (the app's, or the loader's in tools).
Caps query(VkInstance instance, VkPhysicalDevice gpu, PFN_vkGetInstanceProcAddr gipa);

}  // namespace fov::vulkan
