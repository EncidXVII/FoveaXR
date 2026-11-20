// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

#include <d3d11.h>
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "d3d11_scene.h"
#include "xr_fixture.h"
#include <string>

namespace fixture {
namespace {

class D3d11 : public Gfx {
public:
    D3d11(XrInstance instance, XrSystemId system, const Args& args) : dump_(args.dump), frames_(args.frames) {
        auto get = xr_proc<PFN_xrGetD3D11GraphicsRequirementsKHR>(instance, "xrGetD3D11GraphicsRequirementsKHR");
        XrGraphicsRequirementsD3D11KHR req = {XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
        xr_check(get(instance, system, &req), "xrGetD3D11GraphicsRequirementsKHR");
        auto adapter = pick_adapter(req.adapterLuid, args.adapter);
        check(D3D11CreateDevice(adapter.Get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                0, nullptr, 0, D3D11_SDK_VERSION, &device_, nullptr, &ctx_),
              "D3D11CreateDevice");
        scene_ = std::make_unique<D3d11Scene>(device_.Get());
        binding_.device = device_.Get();
    }

    const void* binding() const override { return &binding_; }

    int64_t format(const std::vector<int64_t>& offered) override { return format_ = pick_format(offered); }

    void images(int eye, XrSwapchain swapchain, uint32_t w, uint32_t h) override {
        uint32_t n = 0;
        xrEnumerateSwapchainImages(swapchain, 0, &n, nullptr);
        std::vector<XrSwapchainImageD3D11KHR> images(n, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        xr_check(xrEnumerateSwapchainImages(swapchain, n, &n, reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),
                 "xrEnumerateSwapchainImages");
        for (auto& image : images) {
            // Swapchain textures may be typeless: the view names the format.
            D3D11_RENDER_TARGET_VIEW_DESC d = {format_, D3D11_RTV_DIMENSION_TEXTURE2D};
            ComPtr<ID3D11RenderTargetView> rtv;
            check(device_->CreateRenderTargetView(image.texture, &d, &rtv), "CreateRenderTargetView");
            rtv_[eye].push_back(rtv);
            tex_[eye].push_back(image.texture);
        }
        w_[eye] = w, h_[eye] = h;
    }

    void render(int eye, uint32_t image, const Constants& c) override {
        scene_->render(ctx_.Get(), rtv_[eye][image].Get(), eye, w_[eye], h_[eye], c);
        if (!dump_.empty() && ++rendered_[eye] == frames_)
            scene_->dump(ctx_.Get(), tex_[eye][image], dump_ + "_eye" + std::to_string(eye) + ".bmp");
    }

private:
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> ctx_;
    std::unique_ptr<D3d11Scene> scene_;
    XrGraphicsBindingD3D11KHR binding_ = {XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    std::vector<ComPtr<ID3D11RenderTargetView>> rtv_[2];
    uint32_t w_[2] = {}, h_[2] = {};
    std::vector<ID3D11Texture2D*> tex_[2];
    std::string dump_;
    int frames_, rendered_[2] = {};
};

}  // namespace

std::unique_ptr<Gfx> make_d3d11(XrInstance instance, XrSystemId system, const Args& args) {
    return std::make_unique<D3d11>(instance, system, args);
}

}  // namespace fixture
