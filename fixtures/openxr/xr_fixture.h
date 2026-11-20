// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
// OpenXR fixture: one app, three graphics backends, separate eye swapchains.
#include <openxr/openxr.h>
#include <cstdint>
#include <memory>
#include <vector>
#include "fixture.h"

namespace fixture {

class Gfx {
public:
    virtual ~Gfx() = default;
    virtual const void* binding() const = 0;  // the XrGraphicsBinding*KHR for xrCreateSession
    // Picks a colour format from the runtime's list (sRGB preferred) and builds what depends on it.
    virtual int64_t format(const std::vector<int64_t>& offered) = 0;
    virtual void images(int eye, XrSwapchain swapchain, uint32_t w, uint32_t h) = 0;
    virtual void render(int eye, uint32_t image, const Constants& c) = 0;
};

std::unique_ptr<Gfx> make_d3d11(XrInstance instance, XrSystemId system, const Args& args);
std::unique_ptr<Gfx> make_d3d12(XrInstance instance, XrSystemId system, const Args& args);
std::unique_ptr<Gfx> make_vulkan(XrInstance instance, XrSystemId system, const Args& args);

inline void xr_check(XrResult r, const char* what) {
    if (XR_FAILED(r)) {
        log("%s failed: %d", what, (int)r);
        std::exit(4);
    }
}

template <typename F>
F xr_proc(XrInstance instance, const char* name) {
    PFN_xrVoidFunction f = nullptr;
    xr_check(xrGetInstanceProcAddr(instance, name, &f), name);
    return reinterpret_cast<F>(f);
}

}  // namespace fixture
