#include <d3d12.h>
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D12
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "d3d_common.h"
#include "xr_fixture.h"

namespace fixture {
namespace {

constexpr UINT kMaxImages = 16;  // per eye; runtimes use 3

class D3d12 : public Gfx {
public:
    D3d12(XrInstance instance, XrSystemId system, const Args& args) {
        auto get = xr_proc<PFN_xrGetD3D12GraphicsRequirementsKHR>(instance, "xrGetD3D12GraphicsRequirementsKHR");
        XrGraphicsRequirementsD3D12KHR req = {XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};
        xr_check(get(instance, system, &req), "xrGetD3D12GraphicsRequirementsKHR");
        auto adapter = pick_adapter(req.adapterLuid, args.adapter);
        check(D3D12CreateDevice(adapter.Get(), req.minFeatureLevel, IID_PPV_ARGS(&device_)), "D3D12CreateDevice");
        D3D12_COMMAND_QUEUE_DESC q = {D3D12_COMMAND_LIST_TYPE_DIRECT};
        check(device_->CreateCommandQueue(&q, IID_PPV_ARGS(&queue_)), "CreateCommandQueue");
        binding_.device = device_.Get();
        binding_.queue = queue_.Get();

        // Root signature: FrameData as 8 root constants at b0.
        D3D12_ROOT_PARAMETER p = {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS};
        p.Constants = {0, 0, sizeof(Constants) / 4};
        D3D12_ROOT_SIGNATURE_DESC rs = {1, &p};
        ComPtr<ID3DBlob> blob, errors;
        check(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors), "root signature");
        check(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_)),
              "CreateRootSignature");

        D3D12_DESCRIPTOR_HEAP_DESC h = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2 * kMaxImages};
        check(device_->CreateDescriptorHeap(&h, IID_PPV_ARGS(&rtv_heap_)), "RTV heap");
        h = {D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2};
        check(device_->CreateDescriptorHeap(&h, IID_PPV_ARGS(&dsv_heap_)), "DSV heap");
        rtv_step_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        dsv_step_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    }

    ~D3d12() override {
        for (auto& e : eyes_) wait(e);
    }

    const void* binding() const override { return &binding_; }

    int64_t format(const std::vector<int64_t>& offered) override {
        format_ = pick_format(offered);
        auto vs = compile("vs_main", "vs_5_1");
        scene_pso_ = pso(vs.Get(), compile("ps_scene", "ps_5_1").Get(), true);
        ui_pso_ = pso(vs.Get(), compile("ps_ui", "ps_5_1").Get(), false);
        return format_;
    }

    void images(int eye, XrSwapchain swapchain, uint32_t w, uint32_t h) override {
        Eye& e = eyes_[eye];
        uint32_t n = 0;
        xrEnumerateSwapchainImages(swapchain, 0, &n, nullptr);
        std::vector<XrSwapchainImageD3D12KHR> images(n, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        xr_check(xrEnumerateSwapchainImages(swapchain, n, &n, reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),
                 "xrEnumerateSwapchainImages");
        if (n > kMaxImages) check(E_FAIL, "swapchain image count");
        for (uint32_t i = 0; i < n; ++i) {
            D3D12_RENDER_TARGET_VIEW_DESC d = {format_, D3D12_RTV_DIMENSION_TEXTURE2D};
            device_->CreateRenderTargetView(images[i].texture, &d, rtv(eye, i));
        }
        D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC t = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, w, h, 1, 1, DXGI_FORMAT_D32_FLOAT, {1, 0},
                                 D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL};
        D3D12_CLEAR_VALUE clear = {DXGI_FORMAT_D32_FLOAT};
        clear.DepthStencil.Depth = 1;
        check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &t, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                                               IID_PPV_ARGS(&e.depth)),
              "depth");
        device_->CreateDepthStencilView(e.depth.Get(), nullptr, dsv(eye));
        check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&e.alloc)), "allocator");
        check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, e.alloc.Get(), nullptr, IID_PPV_ARGS(&e.list)),
              "CreateCommandList");
        e.list->Close();
        check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&e.fence)), "CreateFence");
        e.w = w, e.h = h;
    }

    void render(int eye, uint32_t image, const Constants& c) override {
        Eye& e = eyes_[eye];
        wait(e);
        e.alloc->Reset();
        auto* l = e.list.Get();
        l->Reset(e.alloc.Get(), scene_pso_.Get());
        l->SetGraphicsRootSignature(root_.Get());
        l->SetGraphicsRoot32BitConstants(0, sizeof(Constants) / 4, &c, 0);
        l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        auto target = rtv(eye, image);
        auto depth = dsv(eye);
        // Eye pass: eye-sized colour with eye-sized depth. Swapchain images arrive and leave in RENDER_TARGET state.
        const float clear[4] = {0, 0, 0, 1};
        l->ClearRenderTargetView(target, clear, 0, nullptr);
        l->ClearDepthStencilView(depth, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
        l->OMSetRenderTargets(1, &target, FALSE, &depth);
        viewport(l, 0, 0, 1, 1, e);
        l->DrawInstanced(3, 1, 0, 0);
        // UI pass: same colour target, no depth.
        l->SetPipelineState(ui_pso_.Get());
        l->OMSetRenderTargets(1, &target, FALSE, nullptr);
        viewport(l, kUi[0], kUi[1], kUi[2], kUi[3], e);
        l->DrawInstanced(3, 1, 0, 0);
        check(l->Close(), "Close");
        ID3D12CommandList* lists[] = {l};
        queue_->ExecuteCommandLists(1, lists);
        queue_->Signal(e.fence.Get(), ++e.value);
    }

private:
    struct Eye {
        uint32_t w = 0, h = 0;
        ComPtr<ID3D12Resource> depth;
        ComPtr<ID3D12CommandAllocator> alloc;
        ComPtr<ID3D12GraphicsCommandList> list;
        ComPtr<ID3D12Fence> fence;
        UINT64 value = 0;
    };

    static void wait(Eye& e) {
        if (!e.fence || e.fence->GetCompletedValue() >= e.value) return;
        e.fence->SetEventOnCompletion(e.value, nullptr);  // null event: blocks until reached
    }

    static void viewport(ID3D12GraphicsCommandList* l, float x0, float y0, float x1, float y1, const Eye& e) {
        D3D12_VIEWPORT vp = {x0 * e.w, y0 * e.h, (x1 - x0) * e.w, (y1 - y0) * e.h, 0, 1};
        D3D12_RECT r = {(LONG)vp.TopLeftX, (LONG)vp.TopLeftY, (LONG)(x1 * e.w), (LONG)(y1 * e.h)};
        l->RSSetViewports(1, &vp);
        l->RSSetScissorRects(1, &r);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv(int eye, uint32_t image) const {
        auto h = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        h.ptr += (eye * kMaxImages + image) * rtv_step_;
        return h;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsv(int eye) const {
        auto h = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
        h.ptr += eye * dsv_step_;
        return h;
    }

    ComPtr<ID3D12PipelineState> pso(ID3DBlob* vs, ID3DBlob* ps, bool depth) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
        d.pRootSignature = root_.Get();
        d.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        d.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        d.SampleMask = UINT_MAX;
        d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        d.RasterizerState.DepthClipEnable = TRUE;
        d.DepthStencilState.DepthEnable = depth;
        d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        d.NumRenderTargets = 1;
        d.RTVFormats[0] = format_;
        d.DSVFormat = depth ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_UNKNOWN;
        d.SampleDesc = {1, 0};
        ComPtr<ID3D12PipelineState> p;
        check(device_->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&p)), "CreateGraphicsPipelineState");
        return p;
    }

    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12RootSignature> root_;
    ComPtr<ID3D12PipelineState> scene_pso_, ui_pso_;
    ComPtr<ID3D12DescriptorHeap> rtv_heap_, dsv_heap_;
    UINT rtv_step_ = 0, dsv_step_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    XrGraphicsBindingD3D12KHR binding_ = {XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};
    Eye eyes_[2];
};

}  // namespace

std::unique_ptr<Gfx> make_d3d12(XrInstance instance, XrSystemId system, const Args& args) {
    return std::make_unique<D3d12>(instance, system, args);
}

}  // namespace fixture
