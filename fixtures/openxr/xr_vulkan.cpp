#include <volk.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <cstring>
#include "scene_spirv.h"
#include "xr_fixture.h"

namespace fixture {
namespace {

void vk_check(VkResult r, const char* what) {
    if (r != VK_SUCCESS) {
        log("%s failed: %d", what, (int)r);
        std::exit(3);
    }
}

class Vulkan : public Gfx {
public:
    Vulkan(XrInstance xr, XrSystemId system, const Args&) {
        vk_check(volkInitialize(), "volkInitialize (no Vulkan loader)");
        auto requirements =
            xr_proc<PFN_xrGetVulkanGraphicsRequirements2KHR>(xr, "xrGetVulkanGraphicsRequirements2KHR");
        auto create_instance = xr_proc<PFN_xrCreateVulkanInstanceKHR>(xr, "xrCreateVulkanInstanceKHR");
        auto get_device = xr_proc<PFN_xrGetVulkanGraphicsDevice2KHR>(xr, "xrGetVulkanGraphicsDevice2KHR");
        auto create_device = xr_proc<PFN_xrCreateVulkanDeviceKHR>(xr, "xrCreateVulkanDeviceKHR");
        XrGraphicsRequirementsVulkan2KHR req = {XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
        xr_check(requirements(xr, system, &req), "xrGetVulkanGraphicsRequirements2KHR");

        VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "fov_fixture_xr";
        app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ici.pApplicationInfo = &app;
        XrVulkanInstanceCreateInfoKHR xci = {XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
        xci.systemId = system;
        xci.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
        xci.vulkanCreateInfo = &ici;
        VkResult r;
        xr_check(create_instance(xr, &xci, &instance_, &r), "xrCreateVulkanInstanceKHR");
        vk_check(r, "vkCreateInstance");
        volkLoadInstance(instance_);

        XrVulkanGraphicsDeviceGetInfoKHR gi = {XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
        gi.systemId = system;
        gi.vulkanInstance = instance_;
        xr_check(get_device(xr, &gi, &gpu_), "xrGetVulkanGraphicsDevice2KHR");
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(gpu_, &props);
        log("vulkan device: %s", props.deviceName);

        uint32_t n = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu_, &n, nullptr);
        std::vector<VkQueueFamilyProperties> families(n);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu_, &n, families.data());
        while (family_ < n && !(families[family_].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family_;
        float priority = 1;
        VkDeviceQueueCreateInfo q = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        q.queueFamilyIndex = family_;
        q.queueCount = 1;
        q.pQueuePriorities = &priority;
        VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &q;
        XrVulkanDeviceCreateInfoKHR xdci = {XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
        xdci.systemId = system;
        xdci.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
        xdci.vulkanPhysicalDevice = gpu_;
        xdci.vulkanCreateInfo = &dci;
        xr_check(create_device(xr, &xdci, &device_, &r), "xrCreateVulkanDeviceKHR");
        vk_check(r, "vkCreateDevice");
        volkLoadDevice(device_);
        vkGetDeviceQueue(device_, family_, 0, &queue_);

        binding_.instance = instance_;
        binding_.physicalDevice = gpu_;
        binding_.device = device_;
        binding_.queueFamilyIndex = family_;
        VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = family_;
        vk_check(vkCreateCommandPool(device_, &pci, nullptr, &pool_), "vkCreateCommandPool");
    }

    // Objects die with the process; the GPU must be idle before the session and device go.
    ~Vulkan() override { vkDeviceWaitIdle(device_); }

    const void* binding() const override { return &binding_; }

    int64_t format(const std::vector<int64_t>& offered) override {
        format_ = (VkFormat)offered.at(0);
        for (int64_t want : {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB})  // the later one wins
            for (int64_t f : offered)
                if (f == want) format_ = (VkFormat)f;
        scene_pass_ = render_pass(true);
        ui_pass_ = render_pass(false);
        VkPushConstantRange range = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Constants)};
        VkPipelineLayoutCreateInfo lci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        lci.pushConstantRangeCount = 1;
        lci.pPushConstantRanges = &range;
        vk_check(vkCreatePipelineLayout(device_, &lci, nullptr, &layout_), "vkCreatePipelineLayout");
        VkShaderModule vs = module(kVsMain, sizeof(kVsMain));
        scene_pipe_ = pipeline(scene_pass_, vs, module(kPsScene, sizeof(kPsScene)), true);
        ui_pipe_ = pipeline(ui_pass_, vs, module(kPsUi, sizeof(kPsUi)), false);
        return format_;
    }

    void images(int eye, XrSwapchain swapchain, uint32_t w, uint32_t h) override {
        Eye& e = eyes_[eye];
        e.w = w, e.h = h;
        uint32_t n = 0;
        xrEnumerateSwapchainImages(swapchain, 0, &n, nullptr);
        std::vector<XrSwapchainImageVulkan2KHR> images(n, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
        xr_check(xrEnumerateSwapchainImages(swapchain, n, &n, reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),
                 "xrEnumerateSwapchainImages");

        VkImageCreateInfo ici = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = VK_FORMAT_D32_SFLOAT;
        ici.extent = {w, h, 1};
        ici.mipLevels = ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        vk_check(vkCreateImage(device_, &ici, nullptr, &e.depth), "depth image");
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(device_, e.depth, &mr);
        VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vk_check(vkAllocateMemory(device_, &mai, nullptr, &e.memory), "depth memory");
        vk_check(vkBindImageMemory(device_, e.depth, e.memory, 0), "vkBindImageMemory");
        VkImageView depth_view = view(e.depth, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);

        for (auto& image : images) {
            VkImageView colour = view(image.image, format_, VK_IMAGE_ASPECT_COLOR_BIT);
            VkImageView both[] = {colour, depth_view};
            e.scene_fb.push_back(framebuffer(scene_pass_, both, 2, w, h));
            e.ui_fb.push_back(framebuffer(ui_pass_, both, 1, w, h));
        }
        VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cai.commandPool = pool_;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        vk_check(vkAllocateCommandBuffers(device_, &cai, &e.cmd), "vkAllocateCommandBuffers");
        VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vk_check(vkCreateFence(device_, &fci, nullptr, &e.fence), "vkCreateFence");
    }

    void render(int eye, uint32_t image, const Constants& c) override {
        Eye& e = eyes_[eye];
        vkWaitForFences(device_, 1, &e.fence, VK_TRUE, UINT64_MAX);
        vkResetFences(device_, 1, &e.fence);
        VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(e.cmd, &bi);
        vkCmdPushConstants(e.cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(c), &c);

        // Eye pass: eye-sized colour with eye-sized depth.
        VkClearValue clear[2] = {};
        clear[0].color.float32[3] = 1;
        clear[1].depthStencil.depth = 1;
        VkRenderPassBeginInfo rp = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass = scene_pass_;
        rp.framebuffer = e.scene_fb[image];
        rp.renderArea.extent = {e.w, e.h};
        rp.clearValueCount = 2;
        rp.pClearValues = clear;
        vkCmdBeginRenderPass(e.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(e.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scene_pipe_);
        viewport(e, 0, 0, 1, 1);
        vkCmdDraw(e.cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(e.cmd);

        // UI pass: same colour target, no depth attachment.
        rp.renderPass = ui_pass_;
        rp.framebuffer = e.ui_fb[image];
        rp.clearValueCount = 0;
        vkCmdBeginRenderPass(e.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(e.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_pipe_);
        viewport(e, kUi[0], kUi[1], kUi[2], kUi[3]);
        vkCmdDraw(e.cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(e.cmd);
        vk_check(vkEndCommandBuffer(e.cmd), "vkEndCommandBuffer");

        VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &e.cmd;
        vk_check(vkQueueSubmit(queue_, 1, &si, e.fence), "vkQueueSubmit");
    }

private:
    struct Eye {
        uint32_t w = 0, h = 0;
        VkImage depth = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> scene_fb, ui_fb;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
    };

    void viewport(const Eye& e, float x0, float y0, float x1, float y1) {
        VkViewport vp = {x0 * e.w, y0 * e.h, (x1 - x0) * e.w, (y1 - y0) * e.h, 0, 1};
        VkRect2D r = {{(int32_t)vp.x, (int32_t)vp.y}, {(uint32_t)vp.width, (uint32_t)vp.height}};
        vkCmdSetViewport(e.cmd, 0, 1, &vp);
        vkCmdSetScissor(e.cmd, 0, 1, &r);
    }

    // Swapchain images arrive and must leave in COLOR_ATTACHMENT_OPTIMAL.
    VkRenderPass render_pass(bool scene) {
        VkAttachmentDescription a[2] = {};
        a[0].format = format_;
        a[0].samples = VK_SAMPLE_COUNT_1_BIT;
        a[0].loadOp = scene ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        a[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a[0].initialLayout = scene ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a[1] = a[0];
        a[1].format = VK_FORMAT_D32_SFLOAT;
        a[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        a[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference colour = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depth = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub = {};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &colour;
        sub.pDepthStencilAttachment = scene ? &depth : nullptr;
        // The UI pass draws over what the eye pass wrote.
        VkSubpassDependency dep = {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
        VkRenderPassCreateInfo ci = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        ci.attachmentCount = scene ? 2 : 1;
        ci.pAttachments = a;
        ci.subpassCount = 1;
        ci.pSubpasses = &sub;
        ci.dependencyCount = 1;
        ci.pDependencies = &dep;
        VkRenderPass pass;
        vk_check(vkCreateRenderPass(device_, &ci, nullptr, &pass), "vkCreateRenderPass");
        return pass;
    }

    VkShaderModule module(const unsigned char* code, size_t size) {
        std::vector<uint32_t> words((size + 3) / 4);  // SPIR-V must be 4-byte aligned
        std::memcpy(words.data(), code, size);
        VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = size;
        ci.pCode = words.data();
        VkShaderModule m;
        vk_check(vkCreateShaderModule(device_, &ci, nullptr, &m), "vkCreateShaderModule");
        return m;
    }

    VkPipeline pipeline(VkRenderPass pass, VkShaderModule vs, VkShaderModule ps, bool depth) {
        VkPipelineShaderStageCreateInfo stages[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                                     {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs;
        stages[0].pName = "vs_main";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = ps;
        stages[1].pName = depth ? "ps_scene" : "ps_ui";
        VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable = ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        VkPipelineColorBlendAttachmentState blend = {};
        blend.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1;
        cb.pAttachments = &blend;
        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dy = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dy.dynamicStateCount = 2;
        dy.pDynamicStates = dyn;
        VkGraphicsPipelineCreateInfo ci = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        ci.stageCount = 2;
        ci.pStages = stages;
        ci.pVertexInputState = &vi;
        ci.pInputAssemblyState = &ia;
        ci.pViewportState = &vp;
        ci.pRasterizationState = &rs;
        ci.pMultisampleState = &ms;
        ci.pDepthStencilState = depth ? &ds : nullptr;
        ci.pColorBlendState = &cb;
        ci.pDynamicState = &dy;
        ci.layout = layout_;
        ci.renderPass = pass;
        VkPipeline p;
        vk_check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, &p), "vkCreateGraphicsPipelines");
        return p;
    }

    VkImageView view(VkImage image, VkFormat format, VkImageAspectFlags aspect) {
        VkImageViewCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        ci.image = image;
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = format;
        ci.subresourceRange = {aspect, 0, 1, 0, 1};
        VkImageView v;
        vk_check(vkCreateImageView(device_, &ci, nullptr, &v), "vkCreateImageView");
        return v;
    }

    VkFramebuffer framebuffer(VkRenderPass pass, const VkImageView* views, uint32_t count, uint32_t w, uint32_t h) {
        VkFramebufferCreateInfo ci = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        ci.renderPass = pass;
        ci.attachmentCount = count;
        ci.pAttachments = views;
        ci.width = w, ci.height = h, ci.layers = 1;
        VkFramebuffer fb;
        vk_check(vkCreateFramebuffer(device_, &ci, nullptr, &fb), "vkCreateFramebuffer");
        return fb;
    }

    uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags want) {
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(gpu_, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
        vk_check(VK_ERROR_OUT_OF_DEVICE_MEMORY, "memory type");
        return 0;
    }

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t family_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkRenderPass scene_pass_ = VK_NULL_HANDLE, ui_pass_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline scene_pipe_ = VK_NULL_HANDLE, ui_pipe_ = VK_NULL_HANDLE;
    XrGraphicsBindingVulkan2KHR binding_ = {XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    Eye eyes_[2];
};

}  // namespace

std::unique_ptr<Gfx> make_vulkan(XrInstance instance, XrSystemId system, const Args& args) {
    return std::make_unique<Vulkan>(instance, system, args);
}

}  // namespace fixture
