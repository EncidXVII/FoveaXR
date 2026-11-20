// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

#include <windows.h>
#include <cmath>
#include "xr_fixture.h"

using namespace fixture;

int main(int argc, char** argv) {
    Args args = parse(argc, argv);
    // Names spelled out: their macros live in openxr_platform.h, which needs every graphics API's headers.
    const char* ext = args.gfx == "d3d12"    ? "XR_KHR_D3D12_enable"
                      : args.gfx == "vulkan" ? "XR_KHR_vulkan_enable2"
                                             : "XR_KHR_D3D11_enable";
    XrInstanceCreateInfo ici = {XR_TYPE_INSTANCE_CREATE_INFO};
    snprintf(ici.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "fov_fixture_xr");
    snprintf(ici.applicationInfo.engineName, XR_MAX_ENGINE_NAME_SIZE, "fixture");
    ici.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ici.enabledExtensionCount = 1;
    ici.enabledExtensionNames = &ext;
    XrInstance instance;
    xr_check(xrCreateInstance(&ici, &instance), "xrCreateInstance");
    XrSystemGetInfo sgi = {XR_TYPE_SYSTEM_GET_INFO};
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system;
    xr_check(xrGetSystem(instance, &sgi, &system), "xrGetSystem");

    std::unique_ptr<Gfx> gfx = args.gfx == "d3d12"    ? make_d3d12(instance, system, args)
                               : args.gfx == "vulkan" ? make_vulkan(instance, system, args)
                                                      : make_d3d11(instance, system, args);
    XrSessionCreateInfo sci = {XR_TYPE_SESSION_CREATE_INFO, gfx->binding()};
    sci.systemId = system;
    XrSession session;
    xr_check(xrCreateSession(instance, &sci, &session), "xrCreateSession");
    XrReferenceSpaceCreateInfo rci = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rci.poseInReferenceSpace.orientation.w = 1;
    XrSpace space;
    xr_check(xrCreateReferenceSpace(session, &rci, &space), "xrCreateReferenceSpace");

    const auto stereo = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    uint32_t n = 0;
    std::vector<XrViewConfigurationView> configs(2, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    xr_check(xrEnumerateViewConfigurationViews(instance, system, stereo, 2, &n, configs.data()), "views");
    xrEnumerateSwapchainFormats(session, 0, &n, nullptr);
    std::vector<int64_t> formats(n);
    xr_check(xrEnumerateSwapchainFormats(session, n, &n, formats.data()), "xrEnumerateSwapchainFormats");
    int64_t format = gfx->format(formats);
    XrSwapchain swapchain[2];
    for (int eye = 0; eye < 2; ++eye) {
        XrSwapchainCreateInfo c = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
        c.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        c.format = format;
        c.sampleCount = 1;
        c.width = configs[eye].recommendedImageRectWidth;
        c.height = configs[eye].recommendedImageRectHeight;
        c.faceCount = c.arraySize = c.mipCount = 1;
        xr_check(xrCreateSwapchain(session, &c, &swapchain[eye]), "xrCreateSwapchain");
        gfx->images(eye, swapchain[eye], c.width, c.height);
    }
    log("openxr %s: %ux%u per eye, format %lld, heavy %d", args.gfx.c_str(), configs[0].recommendedImageRectWidth,
        configs[0].recommendedImageRectHeight, (long long)format, args.heavy);

    bool running = false, quit = false, exit_requested = false;
    long long frame = 0;
    LARGE_INTEGER f, t0 = {}, t1;
    QueryPerformanceFrequency(&f);
    while (!quit) {
        XrEventDataBuffer event = {XR_TYPE_EVENT_DATA_BUFFER};
        while (xrPollEvent(instance, &event) == XR_SUCCESS) {
            if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                auto state = reinterpret_cast<XrEventDataSessionStateChanged&>(event).state;
                if (state == XR_SESSION_STATE_READY) {
                    XrSessionBeginInfo b = {XR_TYPE_SESSION_BEGIN_INFO};
                    b.primaryViewConfigurationType = stereo;
                    xr_check(xrBeginSession(session, &b), "xrBeginSession");
                    running = true;
                    log("session running");
                } else if (state == XR_SESSION_STATE_STOPPING) {
                    xrEndSession(session);
                    running = false;
                    quit = true;
                } else if (state == XR_SESSION_STATE_EXITING || state == XR_SESSION_STATE_LOSS_PENDING) {
                    quit = true;
                }
            } else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
                quit = true;
            }
            event = {XR_TYPE_EVENT_DATA_BUFFER};
        }
        if (!running) {
            Sleep(10);
            continue;
        }
        XrFrameState fs = {XR_TYPE_FRAME_STATE};
        xr_check(xrWaitFrame(session, nullptr, &fs), "xrWaitFrame");
        xr_check(xrBeginFrame(session, nullptr), "xrBeginFrame");
        XrCompositionLayerProjectionView pv[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                                  {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
        XrCompositionLayerProjection layer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        const XrCompositionLayerBaseHeader* layers[] = {reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer)};
        XrFrameEndInfo end = {XR_TYPE_FRAME_END_INFO};
        end.displayTime = fs.predictedDisplayTime;
        end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        if (fs.shouldRender) {
            XrViewLocateInfo li = {XR_TYPE_VIEW_LOCATE_INFO};
            li.viewConfigurationType = stereo;
            li.displayTime = fs.predictedDisplayTime;
            li.space = space;
            XrViewState vs = {XR_TYPE_VIEW_STATE};
            XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
            xr_check(xrLocateViews(session, &li, &vs, 2, &n, views), "xrLocateViews");
            for (int eye = 0; eye < 2; ++eye) {
                uint32_t image;
                xr_check(xrAcquireSwapchainImage(swapchain[eye], nullptr, &image), "xrAcquireSwapchainImage");
                XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                wi.timeout = XR_INFINITE_DURATION;
                xr_check(xrWaitSwapchainImage(swapchain[eye], &wi), "xrWaitSwapchainImage");
                const XrFovf& fov = views[eye].fov;
                float tan[4] = {std::tan(fov.angleLeft), std::tan(fov.angleRight), std::tan(fov.angleUp),
                                std::tan(fov.angleDown)};
                gfx->render(eye, image, constants(tan, frame, args.heavy));
                xr_check(xrReleaseSwapchainImage(swapchain[eye], nullptr), "xrReleaseSwapchainImage");
                pv[eye].pose = views[eye].pose;
                pv[eye].fov = fov;
                pv[eye].subImage.swapchain = swapchain[eye];
                pv[eye].subImage.imageRect.extent = {(int32_t)configs[eye].recommendedImageRectWidth,
                                                     (int32_t)configs[eye].recommendedImageRectHeight};
            }
            layer.space = space;
            layer.viewCount = 2;
            layer.views = pv;
            end.layerCount = 1;
            end.layers = layers;
        }
        xr_check(xrEndFrame(session, &end), "xrEndFrame");
        if (!fs.shouldRender) continue;
        if (frame++ == 0) {
            QueryPerformanceCounter(&t0);
            log("first frame submitted");
        }
        if (args.frames && frame >= args.frames && !exit_requested) {
            QueryPerformanceCounter(&t1);
            log("done: %lld frames, %.2f ms per frame", frame,
                (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart / (frame - 1 ? frame - 1 : 1));
            xrRequestExitSession(session);
            exit_requested = true;
        }
    }
    xrDestroySpace(space);
    for (auto s : swapchain) xrDestroySwapchain(s);
    xrDestroySession(session);
    gfx.reset();
    xrDestroyInstance(instance);
    return args.frames && frame < args.frames ? 5 : 0;
}
