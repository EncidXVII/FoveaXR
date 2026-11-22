// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

// D3D12 VRS Tier 2: render-target descriptors are mapped to their resources when created; every bind on a command
// list (OMSetRenderTargets, render passes) is classified; eye passes get the rate image, everything else none, and
// nothing stays bound when a list closes (SPEC 5.2).
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <atomic>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <tuple>
#include <unordered_map>
#include <vector>
#include "fov_d3d12.h"
#include "fov_dxgi.h"
#include "fov_frame.h"

extern "C" const unsigned fov_d3d12_slot_create_rtv, fov_d3d12_slot_create_dsv, fov_d3d12_slot_close,
    fov_d3d12_slot_reset, fov_d3d12_slot_om_set, fov_d3d12_slot_begin_pass, fov_d3d12_slot_end_pass;

namespace fov::d3d12 {
namespace {
using Microsoft::WRL::ComPtr;
using List = ID3D12GraphicsCommandList;

using CreateView = void(STDMETHODCALLTYPE*)(ID3D12Device*, ID3D12Resource*, const void*, D3D12_CPU_DESCRIPTOR_HANDLE);
using CloseFn = HRESULT(STDMETHODCALLTYPE*)(List*);
using ResetFn = HRESULT(STDMETHODCALLTYPE*)(List*, ID3D12CommandAllocator*, ID3D12PipelineState*);
using OmSet = void(STDMETHODCALLTYPE*)(List*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, BOOL,
                                       const D3D12_CPU_DESCRIPTOR_HANDLE*);
using BeginPass = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList4*, UINT, const D3D12_RENDER_PASS_RENDER_TARGET_DESC*,
                                           const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC*, D3D12_RENDER_PASS_FLAGS);
using EndPass = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList4*);
CreateView g_create_rtv, g_create_dsv;
CloseFn g_close;
ResetFn g_reset;
OmSet g_om_set;
BeginPass g_begin_pass;
EndPass g_end_pass;

ID3D12Device* g_device = nullptr;
ID3D12CommandQueue* g_queue = nullptr;
UINT g_tile = 16;
bool g_extra_rates = false;
thread_local bool t_inside = false;  // our own recording is never classified

// Descriptor handle -> what it views.
struct View {
    ID3D12Resource* res = nullptr;
    UINT w = 0, h = 0;
};
std::shared_mutex g_views_m;
std::unordered_map<SIZE_T, View> g_views;

View lookup(const D3D12_CPU_DESCRIPTOR_HANDLE* h) {
    if (!h) return {};
    std::shared_lock lock(g_views_m);
    auto it = g_views.find(h->ptr);
    return it == g_views.end() ? View{} : it->second;
}

void remember(ID3D12Resource* res, D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    std::unique_lock lock(g_views_m);
    if (!res) {
        g_views.erase(handle.ptr);
        return;
    }
    D3D12_RESOURCE_DESC d = res->GetDesc();
    g_views[handle.ptr] = {res, (UINT)d.Width, d.Height};
}

void STDMETHODCALLTYPE create_rtv(ID3D12Device* dev, ID3D12Resource* res, const void* desc,
                                  D3D12_CPU_DESCRIPTOR_HANDLE h) {
    g_create_rtv(dev, res, desc, h);
    remember(res, h);
}

void STDMETHODCALLTYPE create_dsv(ID3D12Device* dev, ID3D12Resource* res, const void* desc,
                                  D3D12_CPU_DESCRIPTOR_HANDLE h) {
    g_create_dsv(dev, res, desc, h);
    remember(res, h);
}

// Our own command list for rate-image uploads and the tint, run on the app's queue so ordering with its eye
// passes holds. Allocators are recycled once their fence passed.
struct Ours {
    std::mutex m;
    ComPtr<ID3D12Fence> fence;
    UINT64 value = 0;
    ComPtr<ID3D12CommandAllocator> alloc[8];
    UINT64 alloc_at[8] = {};
    int next = 0;
    ComPtr<List> list;
} g_ours;

void wait(UINT64 v) {
    if (g_ours.fence->GetCompletedValue() < v) g_ours.fence->SetEventOnCompletion(v, nullptr);
}

List* begin_ours() {  // g_ours.m held
    int i = g_ours.next;
    g_ours.next = (i + 1) % 8;
    wait(g_ours.alloc_at[i]);
    g_ours.alloc[i]->Reset();
    g_ours.list->Reset(g_ours.alloc[i].Get(), nullptr);
    g_ours.alloc_at[i] = g_ours.value + 1;
    return g_ours.list.Get();
}

UINT64 submit_ours() {  // g_ours.m held
    g_ours.list->Close();
    ID3D12CommandList* lists[] = {g_ours.list.Get()};
    g_queue->ExecuteCommandLists(1, lists);
    g_queue->Signal(g_ours.fence.Get(), ++g_ours.value);
    return g_ours.value;
}

D3D12_RESOURCE_BARRIER transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b = {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION};
    b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to};
    return b;
}

// Rate images: 3 per pattern so an update never touches one a frame in flight still reads.
struct RateImage {
    ComPtr<ID3D12Resource> tex[3], upload;
    bool ready[3] = {};
    UINT64 done_at[3] = {};
    UINT tw = 0, th = 0, pitch = 0;
    int cur = -1;
    uint64_t version = 0;
    uint8_t* mapped = nullptr;
};
std::mutex g_images_m;
std::map<std::tuple<int, UINT, UINT>, RateImage> g_images;
std::vector<uint8_t> g_bytes;

uint8_t d3d12_rate(uint8_t r) {
    static const D3D12_SHADING_RATE full[] = {D3D12_SHADING_RATE_1X1, D3D12_SHADING_RATE_2X1, D3D12_SHADING_RATE_1X2,
                                              D3D12_SHADING_RATE_2X2, D3D12_SHADING_RATE_4X2, D3D12_SHADING_RATE_2X4,
                                              D3D12_SHADING_RATE_4X4};
    if (r > FOV_RATE_2X2 && !g_extra_rates) r = FOV_RATE_2X2;  // 2x4, 4x2, 4x4 are optional
    return (uint8_t)full[r < 7 ? r : 0];
}

ID3D12Resource* image(Target t, UINT w, UINT h) {
    std::lock_guard lock(g_images_m);
    RateImage& img = g_images[{(int)t, w, h}];
    if (!img.upload) {
        img.tw = (w + g_tile - 1) / g_tile, img.th = (h + g_tile - 1) / g_tile;
        img.pitch = (img.tw + 255) & ~255u;
        D3D12_HEAP_PROPERTIES def = {D3D12_HEAP_TYPE_DEFAULT}, up = {D3D12_HEAP_TYPE_UPLOAD};
        D3D12_RESOURCE_DESC td = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, img.tw, img.th, 1, 1, DXGI_FORMAT_R8_UINT, {1, 0}};
        D3D12_RESOURCE_DESC bd = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, UINT64(img.pitch) * img.th * 3, 1, 1, 1,
                                  DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
        for (auto& tex : img.tex)
            if (FAILED(g_device->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST,
                                                         nullptr, IID_PPV_ARGS(&tex))))
                return nullptr;
        if (FAILED(g_device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                     nullptr, IID_PPV_ARGS(&img.upload))) ||
            FAILED(img.upload->Map(0, nullptr, reinterpret_cast<void**>(&img.mapped))))
            return nullptr;
        fov_log("d3d12: rate images %ux%u tiles for %ux%u (target %d)", img.tw, img.th, w, h, (int)t);
    }
    uint64_t v = Core::get().pattern_version();
    if (img.version != v) {
        int slot = (img.cur + 1) % 3;
        std::lock_guard ours(g_ours.m);
        wait(img.done_at[slot]);
        Core::get().pattern(t, w, h, g_tile, g_bytes);
        for (UINT y = 0; y < img.th; ++y)
            for (UINT x = 0; x < img.tw; ++x)
                img.mapped[(size_t(slot) * img.th + y) * img.pitch + x] = d3d12_rate(g_bytes[size_t(y) * img.tw + x]);
        t_inside = true;
        List* l = begin_ours();
        ID3D12Resource* tex = img.tex[slot].Get();
        if (img.ready[slot]) {
            auto b = transition(tex, D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
            l->ResourceBarrier(1, &b);
        }
        D3D12_TEXTURE_COPY_LOCATION dst = {tex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        D3D12_TEXTURE_COPY_LOCATION src = {img.upload.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        src.PlacedFootprint = {UINT64(img.pitch) * img.th * slot, {DXGI_FORMAT_R8_UINT, img.tw, img.th, 1, img.pitch}};
        l->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        auto b = transition(tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE);
        l->ResourceBarrier(1, &b);
        img.done_at[slot] = submit_ours();
        t_inside = false;
        img.ready[slot] = true;
        img.cur = slot;
        img.version = v;
    }
    return img.tex[img.cur].Get();
}

// Eye-pass GPU time: a timestamp pair around each eye pass, resolved in the same list, read 3 frames later.
constexpr UINT kFrames = 4, kPerFrame = 512;
struct FrameTimes {
    uint64_t frame = 0;
    bool foveated = false;
    std::atomic<UINT> next{0};
    std::vector<UINT> done;
};
std::mutex g_time_m;
FrameTimes g_frames[kFrames];
int g_cur = 0;
ComPtr<ID3D12QueryHeap> g_heap;
ComPtr<ID3D12Resource> g_readback;
const UINT64* g_stamps = nullptr;
double g_freq = 0;

int frame_slot() {  // g_time_m held
    uint64_t f = Core::get().frame();
    if (g_frames[g_cur].frame != f) {
        g_cur = (g_cur + 1) % kFrames;
        FrameTimes& old = g_frames[g_cur];  // 3 frames back: its lists ran by now
        if (old.frame && !old.done.empty()) {
            UINT64 sum = 0;
            for (UINT i : old.done) {
                UINT64 a = g_stamps[g_cur * kPerFrame + i], b = g_stamps[g_cur * kPerFrame + i + 1];
                if (b > a && (b - a) / g_freq < 0.1) sum += b - a;
            }
            Core::get().gpu_ms(sum * 1000.0 / g_freq, old.foveated);
        }
        old.frame = f, old.foveated = Core::get().foveating(), old.next = 0;
        old.done.clear();
    }
    return g_cur;
}

struct ListState {
    bool checked = false, direct = false;
    ComPtr<ID3D12GraphicsCommandList5> l5;
    ID3D12Resource* bound = nullptr;
    int span_slot = -1;
    UINT span = 0;
    uint64_t span_frame = 0;
};
std::mutex g_lists_m;
std::unordered_map<List*, ListState> g_lists;

ListState& state(List* list) {
    std::lock_guard lock(g_lists_m);
    ListState& s = g_lists[list];
    if (!s.checked) {
        s.checked = true;
        s.direct = list->GetType() == D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (s.direct) list->QueryInterface(IID_PPV_ARGS(&s.l5));
    }
    return s;
}

void span(List* list, ListState& s, bool eye) {
    if (!g_heap || eye == (s.span_slot >= 0)) return;
    std::lock_guard lock(g_time_m);
    if (eye) {
        int slot = frame_slot();
        UINT i = g_frames[slot].next.fetch_add(2);
        if (i + 1 >= kPerFrame) return;
        list->EndQuery(g_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * kPerFrame + i);
        s.span_slot = slot, s.span = i, s.span_frame = g_frames[slot].frame;
    } else {
        UINT base = s.span_slot * kPerFrame + s.span;
        list->EndQuery(g_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, base + 1);
        list->ResolveQueryData(g_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, base, 2, g_readback.Get(), UINT64(base) * 8);
        if (g_frames[s.span_slot].frame == s.span_frame) g_frames[s.span_slot].done.push_back(s.span);
        s.span_slot = -1;
    }
}

void set_rate(ListState& s, ID3D12Resource* img) {
    if (!s.l5) return;
    if (img) {
        // Combiners: the per-draw rate passes through, then the coarser of it and the image wins.
        D3D12_SHADING_RATE_COMBINER c[2] = {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_MAX};
        s.l5->RSSetShadingRate(D3D12_SHADING_RATE_1X1, c);
        s.l5->RSSetShadingRateImage(img);
    } else {
        s.l5->RSSetShadingRateImage(nullptr);
        s.l5->RSSetShadingRate(D3D12_SHADING_RATE_1X1, nullptr);
    }
    s.bound = img;
}

void apply(List* list, const D3D12_CPU_DESCRIPTOR_HANDLE* rtv, const D3D12_CPU_DESCRIPTOR_HANDLE* dsv) {
    if (t_inside) return;
    ListState& s = state(list);
    if (!s.direct) return;
    View rt = lookup(rtv), ds = lookup(dsv);
    Core::Bind b = Core::get().classify(rt.res, rt.w, rt.h, ds.w, ds.h);
    span(list, s, b.eye);
    ID3D12Resource* img = b.foveate ? image(b.target, rt.w, rt.h) : nullptr;
    if (img != s.bound) set_rate(s, img);
}

void STDMETHODCALLTYPE om_set(List* list, UINT n, const D3D12_CPU_DESCRIPTOR_HANDLE* rtvs, BOOL single,
                              const D3D12_CPU_DESCRIPTOR_HANDLE* dsv) {
    g_om_set(list, n, rtvs, single, dsv);
    apply(list, n ? rtvs : nullptr, dsv);
}

void STDMETHODCALLTYPE begin_pass(ID3D12GraphicsCommandList4* list, UINT n, const D3D12_RENDER_PASS_RENDER_TARGET_DESC* rts,
                                  const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC* ds, D3D12_RENDER_PASS_FLAGS flags) {
    // Rate state goes in before the pass starts.
    apply(list, n ? &rts[0].cpuDescriptor : nullptr, ds ? &ds->cpuDescriptor : nullptr);
    g_begin_pass(list, n, rts, ds, flags);
}

void STDMETHODCALLTYPE end_pass(ID3D12GraphicsCommandList4* list) {
    g_end_pass(list);
    apply(list, nullptr, nullptr);
}

HRESULT STDMETHODCALLTYPE close(List* list) {
    if (!t_inside) {
        ListState& s = state(list);
        // Rate state never leaks into the next list.
        span(list, s, false);
        if (s.bound) set_rate(s, nullptr);
    }
    return g_close(list);
}

HRESULT STDMETHODCALLTYPE reset(List* list, ID3D12CommandAllocator* alloc, ID3D12PipelineState* pso) {
    HRESULT hr = g_reset(list, alloc, pso);
    if (!t_inside) {
        ListState& s = state(list);
        s.bound = nullptr, s.span_slot = -1;
    }
    return hr;
}

bool hook_all() {
    void** dvt = *reinterpret_cast<void***>(g_device);
    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList4> probe;
    if (FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))) ||
        FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&probe))))
        return false;
    probe->Close();
    void** lvt = *reinterpret_cast<void***>(probe.Get());
    return hook(dvt[fov_d3d12_slot_create_rtv], (void*)&create_rtv, (void**)&g_create_rtv) &&
           hook(dvt[fov_d3d12_slot_create_dsv], (void*)&create_dsv, (void**)&g_create_dsv) &&
           hook(lvt[fov_d3d12_slot_om_set], (void*)&om_set, (void**)&g_om_set) &&
           hook(lvt[fov_d3d12_slot_begin_pass], (void*)&begin_pass, (void**)&g_begin_pass) &&
           hook(lvt[fov_d3d12_slot_end_pass], (void*)&end_pass, (void**)&g_end_pass) &&
           hook(lvt[fov_d3d12_slot_close], (void*)&close, (void**)&g_close) &&
           hook(lvt[fov_d3d12_slot_reset], (void*)&reset, (void**)&g_reset);
}
}  // namespace

bool install(ID3D12Device* device, ID3D12CommandQueue* queue) {
    Core& core = Core::get();
    core.set_graphics(FOV_GFX_D3D12);
    if (g_device) return g_device == device;
    Caps caps = query(device);
    if (!caps.usable) {
        core.disable(FOV_REASON_UNSUPPORTED_GPU, "D3D12: this GPU has no VRS Tier 2");
        return false;
    }
    // Before anything is kept: without our own list there is nothing to upload rate images with.
    bool made = SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_ours.fence)));
    for (auto& a : g_ours.alloc)
        made = made && SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&a)));
    made = made && SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_ours.alloc[0].Get(), nullptr,
                                                       IID_PPV_ARGS(&g_ours.list)));
    if (!made) {
        core.disable(FOV_REASON_ERROR, "D3D12: could not create the command list for rate images");
        return false;
    }
    g_ours.list->Close();
    g_device = device;
    g_queue = queue;
    g_tile = caps.tile;
    g_extra_rates = caps.extra_rates;
    device->AddRef();
    queue->AddRef();  // both held for the life of the process, like the hooks

    UINT64 freq = 0;
    D3D12_QUERY_HEAP_DESC qh = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, kFrames * kPerFrame};
    D3D12_HEAP_PROPERTIES rb = {D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC bd = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, UINT64(kFrames) * kPerFrame * 8, 1, 1, 1,
                              DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    if (SUCCEEDED(queue->GetTimestampFrequency(&freq)) && SUCCEEDED(device->CreateQueryHeap(&qh, IID_PPV_ARGS(&g_heap))) &&
        SUCCEEDED(device->CreateCommittedResource(&rb, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                  IID_PPV_ARGS(&g_readback))) &&
        SUCCEEDED(g_readback->Map(0, nullptr, (void**)&g_stamps))) {
        g_freq = (double)freq;
    } else {
        g_heap.Reset();  // no timing, foveation still works
    }

    bool ok = hook_all();
    fov_log("d3d12: VRS tier %d, tile %u, extra rates %s, hooks %s", caps.tier, g_tile, g_extra_rates ? "yes" : "no",
            ok ? "on" : "failed");
    if (!ok) core.disable(FOV_REASON_ERROR, "D3D12: hooks failed");
    return ok;
}

void tint(ID3D12Resource* texture, DXGI_FORMAT format, int eye, const RECT& rect, UINT slice) {
    if (!g_device || !Core::get().tint()) return;
    static ComPtr<ID3D12RootSignature> root;
    static ComPtr<ID3DBlob> vs, ps;
    static std::map<DXGI_FORMAT, ComPtr<ID3D12PipelineState>> psos;
    static ComPtr<ID3D12DescriptorHeap> rtvs;
    static UINT next = 0;
    format = typed(format);
    std::lock_guard lock(g_ours.m);
    if (!root) {
        D3D12_ROOT_PARAMETER p = {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS};
        p.Constants = {0, 0, 8};
        D3D12_ROOT_SIGNATURE_DESC rs = {1, &p};
        ComPtr<ID3DBlob> blob;
        D3D12_DESCRIPTOR_HEAP_DESC h = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 8};
        if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, nullptr)) ||
            FAILED(g_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root))) ||
            FAILED(D3DCompile(kTintHlsl, sizeof(kTintHlsl) - 1, "tint", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vs, nullptr)) ||
            FAILED(D3DCompile(kTintHlsl, sizeof(kTintHlsl) - 1, "tint", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &ps, nullptr)) ||
            FAILED(g_device->CreateDescriptorHeap(&h, IID_PPV_ARGS(&rtvs)))) {
            root.Reset();
            return;
        }
    }
    auto& pso = psos[format];
    if (!pso) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
        d.pRootSignature = root.Get();
        d.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        d.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        d.BlendState.RenderTarget[0] = {TRUE, FALSE, D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
                                        D3D12_BLEND_ZERO, D3D12_BLEND_ONE, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP,
                                        D3D12_COLOR_WRITE_ENABLE_ALL};
        d.SampleMask = UINT_MAX;
        d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        d.NumRenderTargets = 1;
        d.RTVFormats[0] = format;
        d.SampleDesc = {1, 0};
        if (FAILED(g_device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso)))) return;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvs->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += SIZE_T(next++ % 8) * g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_RENDER_TARGET_VIEW_DESC rd = {format, texture->GetDesc().DepthOrArraySize > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DARRAY
                                                                                        : D3D12_RTV_DIMENSION_TEXTURE2D};
    if (rd.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2DARRAY) rd.Texture2DArray = {0, slice, 1, 0};
    t_inside = true;
    g_device->CreateRenderTargetView(texture, &rd, rtv);
    float params[8];
    Core::get().tint_params(eye, params);
    List* l = begin_ours();
    l->SetGraphicsRootSignature(root.Get());
    l->SetPipelineState(pso.Get());
    l->SetGraphicsRoot32BitConstants(0, 8, params, 0);
    l->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT vp = {(float)rect.left, (float)rect.top, (float)(rect.right - rect.left),
                         (float)(rect.bottom - rect.top), 0, 1};
    l->RSSetViewports(1, &vp);
    l->RSSetScissorRects(1, &rect);
    l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    l->DrawInstanced(3, 1, 0, 0);
    submit_ours();
    t_inside = false;
}

}  // namespace fov::d3d12
