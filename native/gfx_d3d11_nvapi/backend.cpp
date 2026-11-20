// D3D11 + NVAPI: hooks OMSetRenderTargets(*) on the immediate context; eye passes get a shading-rate resource view
// and the per-viewport lookup table, everything else gets VRS off. Deferred contexts stay unfoveated (SPEC 5.1).
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <nvapi.h>
#include <map>
#include <mutex>
#include <tuple>
#include <vector>
#include "fov_d3d11.h"
#include "fov_dxgi.h"
#include "fov_frame.h"

extern "C" const unsigned fov_d3d11_slot_om_set, fov_d3d11_slot_om_set_uav, fov_d3d11_slot_execute_list;

namespace fov::d3d11 {
namespace {
using Microsoft::WRL::ComPtr;

using OmSet = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*,
                                       ID3D11DepthStencilView*);
using OmSetUav = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*,
                                          ID3D11DepthStencilView*, UINT, UINT, ID3D11UnorderedAccessView* const*,
                                          const UINT*);
using ExecuteList = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11CommandList*, BOOL);
OmSet g_om_set = nullptr;
OmSetUav g_om_set_uav = nullptr;
ExecuteList g_execute_list = nullptr;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
unsigned g_tile = 16;
thread_local bool t_inside = false;

struct RateImage {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11NvShadingRateResourceView> view;
    uint64_t version = 0;
};
std::map<std::tuple<int, UINT, UINT>, RateImage> g_images;
ID3D11NvShadingRateResourceView* g_bound = nullptr;
std::vector<uint8_t> g_bytes;

void fov_log_once(const char* what) {
    static bool reported = false;
    if (!reported) fov_log("d3d11: NVAPI %s call failed", what);
    reported = true;
}

// Eye-pass GPU time: timestamps around each run of eye passes, one disjoint query per frame, read 3 frames later.
constexpr int kFrames = 4, kStamps = 64;
struct FrameQueries {
    ComPtr<ID3D11Query> disjoint, stamps[kStamps];
    int used = 0;
    uint64_t frame = 0;
    bool foveated = false, pending = false;
};
FrameQueries g_q[kFrames];
int g_cur = -1;
bool g_span = false;

void set_viewport_rates(bool on) {
    static NV_D3D11_VIEWPORT_SHADING_RATE_DESC on_table[NV_MAX_NUM_VIEWPORTS], off[NV_MAX_NUM_VIEWPORTS];
    static bool init = [] {
        // Rate image texels are FovRate values, used as indices into this table on every viewport (stereo
        // renderers draw both eyes through several viewports).
        const NV_PIXEL_SHADING_RATE table[] = {NV_PIXEL_X1_PER_RASTER_PIXEL, NV_PIXEL_X1_PER_2X1_RASTER_PIXELS,
                                               NV_PIXEL_X1_PER_1X2_RASTER_PIXELS, NV_PIXEL_X1_PER_2X2_RASTER_PIXELS,
                                               NV_PIXEL_X1_PER_4X2_RASTER_PIXELS, NV_PIXEL_X1_PER_2X4_RASTER_PIXELS,
                                               NV_PIXEL_X1_PER_4X4_RASTER_PIXELS};
        for (auto& v : on_table) {
            v.enableVariablePixelShadingRate = true;
            for (int i = 0; i < NV_MAX_PIXEL_SHADING_RATES; ++i)
                v.shadingRateTable[i] = i < 7 ? table[i] : NV_PIXEL_X1_PER_RASTER_PIXEL;
        }
        return true;
    }();
    (void)init;
    NV_D3D11_VIEWPORTS_SHADING_RATE_DESC d = {NV_D3D11_VIEWPORTS_SHADING_RATE_DESC_VER, NV_MAX_NUM_VIEWPORTS,
                                             on ? on_table : off};
    if (NvAPI_D3D11_RSSetViewportsPixelShadingRates(g_ctx, &d) != NVAPI_OK) fov_log_once("viewport rates");
}

void set_vrs(ID3D11NvShadingRateResourceView* view) {
    set_viewport_rates(view != nullptr);
    if (NvAPI_D3D11_RSSetShadingRateResourceView(g_ctx, view) != NVAPI_OK) fov_log_once("shading rate view");
    g_bound = view;
}

ID3D11NvShadingRateResourceView* image(Target t, UINT w, UINT h) {
    RateImage& img = g_images[{(int)t, w, h}];
    UINT tw = (w + g_tile - 1) / g_tile, th = (h + g_tile - 1) / g_tile;
    if (!img.texture) {
        D3D11_TEXTURE2D_DESC d = {tw, th, 1, 1, DXGI_FORMAT_R8_UINT, {1, 0}, D3D11_USAGE_DEFAULT,
                                  D3D11_BIND_SHADER_RESOURCE};
        NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC v = {NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC_VER, DXGI_FORMAT_R8_UINT,
                                                      NV_SRRV_DIMENSION_TEXTURE2D};
        if (FAILED(g_device->CreateTexture2D(&d, nullptr, &img.texture)) ||
            NvAPI_D3D11_CreateShadingRateResourceView(g_device, img.texture.Get(), &v, &img.view) != NVAPI_OK) {
            fov_log("d3d11: rate image %ux%u failed", tw, th);
            img.texture.Reset();
            return nullptr;
        }
        fov_log("d3d11: rate image %ux%u tiles for %ux%u (target %d)", tw, th, w, h, (int)t);
    }
    uint64_t v = Core::get().pattern_version();
    if (img.version != v) {
        Core::get().pattern(t, w, h, g_tile, g_bytes);
        g_ctx->UpdateSubresource(img.texture.Get(), 0, nullptr, g_bytes.data(), tw, 0);
        img.version = v;
    }
    return img.view.Get();
}

void collect(FrameQueries& q) {
    if (!q.pending) return;
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
    if (g_ctx->GetData(q.disjoint.Get(), &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) return;
    q.pending = false;
    if (dj.Disjoint || !q.used) return;
    UINT64 sum = 0, t[2];
    for (int i = 0; i + 1 < q.used; i += 2) {
        if (g_ctx->GetData(q.stamps[i].Get(), &t[0], 8, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            g_ctx->GetData(q.stamps[i + 1].Get(), &t[1], 8, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
            return;
        sum += t[1] - t[0];
    }
    Core::get().gpu_ms(sum * 1000.0 / dj.Frequency, q.foveated);
}

void timing(bool eye) {
    uint64_t f = Core::get().frame();
    if (g_cur < 0 || g_q[g_cur].frame != f) {
        if (g_cur >= 0) {
            FrameQueries& q = g_q[g_cur];
            if (g_span) g_ctx->End(q.stamps[q.used++].Get());
            g_ctx->End(q.disjoint.Get());
            q.pending = true;
        }
        g_span = false;
        for (auto& q : g_q) collect(q);
        g_cur = (g_cur + 1) % kFrames;
        FrameQueries& q = g_q[g_cur];
        if (!q.disjoint) {
            D3D11_QUERY_DESC d = {D3D11_QUERY_TIMESTAMP_DISJOINT};
            g_device->CreateQuery(&d, &q.disjoint);
            d.Query = D3D11_QUERY_TIMESTAMP;
            for (auto& s : q.stamps) g_device->CreateQuery(&d, &s);
        }
        q.pending = false;  // a frame never read back is dropped
        q.used = 0, q.frame = f, q.foveated = Core::get().foveating();
        g_ctx->Begin(q.disjoint.Get());
    }
    FrameQueries& q = g_q[g_cur];
    if (eye != g_span && q.used < kStamps - 1) {
        g_ctx->End(q.stamps[q.used++].Get());
        g_span = eye;
    }
}

void after_bind(ID3D11DeviceContext* ctx, UINT n, ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv) {
    if (ctx != g_ctx || t_inside) return;
    t_inside = true;
    UINT w = 0, h = 0, dw = 0, dh = 0;
    ComPtr<ID3D11Resource> res;
    auto size = [](ID3D11View* view, ComPtr<ID3D11Resource>& r, UINT& sw, UINT& sh) {
        view->GetResource(&r);
        ComPtr<ID3D11Texture2D> t;
        if (SUCCEEDED(r.As(&t))) {
            D3D11_TEXTURE2D_DESC d;
            t->GetDesc(&d);
            sw = d.Width, sh = d.Height;
        }
    };
    if (n && rtvs && rtvs[0]) size(rtvs[0], res, w, h);
    if (dsv) {
        ComPtr<ID3D11Resource> depth;
        size(dsv, depth, dw, dh);
    }
    Core::Bind b = Core::get().classify(res.Get(), w, h, dw, dh);
    timing(b.eye);
    ID3D11NvShadingRateResourceView* view = b.foveate ? image(b.target, w, h) : nullptr;
    if (view != g_bound) set_vrs(view);
    t_inside = false;
}

void STDMETHODCALLTYPE om_set(ID3D11DeviceContext* ctx, UINT n, ID3D11RenderTargetView* const* rtvs,
                              ID3D11DepthStencilView* dsv) {
    g_om_set(ctx, n, rtvs, dsv);
    after_bind(ctx, n, rtvs, dsv);
}

void STDMETHODCALLTYPE om_set_uav(ID3D11DeviceContext* ctx, UINT n, ID3D11RenderTargetView* const* rtvs,
                                  ID3D11DepthStencilView* dsv, UINT slot, UINT nuav,
                                  ID3D11UnorderedAccessView* const* uavs, const UINT* counts) {
    g_om_set_uav(ctx, n, rtvs, dsv, slot, nuav, uavs, counts);
    if (n != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) after_bind(ctx, n, rtvs, dsv);
}

// A command list was recorded on a deferred context, whose binds we never saw: the rate image of the last eye pass
// must not reach its draws. The app's next bind on the immediate context decides again.
void STDMETHODCALLTYPE execute_list(ID3D11DeviceContext* ctx, ID3D11CommandList* list, BOOL restore) {
    if (ctx == g_ctx && g_bound && !t_inside) {
        t_inside = true;
        set_vrs(nullptr);
        t_inside = false;
    }
    g_execute_list(ctx, list, restore);
}
}  // namespace

bool install(ID3D11Device* device) {
    Core& core = Core::get();
    core.set_graphics(FOV_GFX_D3D11);
    if (g_device) return g_device == device;
    Caps caps = query(device);
    if (!caps.supported) {
        // Under Proton, NVAPI is dxvk-nvapi, which has no D3D11 variable rate shading (it reports it unsupported).
        core.disable(FOV_REASON_UNSUPPORTED_GPU,
                     fov_is_wine() ? "D3D11 games: not available on Linux (Proton has no D3D11 variable rate shading)"
                     : caps.nvapi  ? "D3D11: this NVIDIA GPU has no variable rate shading"
                                   : "D3D11: foveation needs an NVIDIA GPU (NVAPI)");
        return false;
    }
    g_device = device;
    g_tile = caps.tile_w;
    device->GetImmediateContext(&g_ctx);  // one reference held for the life of the process
    void** vt = *reinterpret_cast<void***>(g_ctx);
    bool ok = hook(vt[fov_d3d11_slot_om_set], (void*)&om_set, (void**)&g_om_set) &&
              hook(vt[fov_d3d11_slot_om_set_uav], (void*)&om_set_uav, (void**)&g_om_set_uav);
    // Only games that render through deferred contexts need this one: without it the rest still works.
    if (ok) hook(vt[fov_d3d11_slot_execute_list], (void*)&execute_list, (void**)&g_execute_list);
    fov_log("d3d11: NVAPI VRS, tile %u, hooks %s", g_tile, ok ? "on" : "failed");
    if (!ok) core.disable(FOV_REASON_ERROR, "D3D11: hooks failed");
    return ok;
}

void tint(ID3D11Texture2D* texture, DXGI_FORMAT format, int eye, const RECT& rect, UINT slice) {
    if (!g_device || !Core::get().tint()) return;
    static ComPtr<ID3DDeviceContextState> state;
    static ComPtr<ID3D11VertexShader> vs;
    static ComPtr<ID3D11PixelShader> ps;
    static ComPtr<ID3D11Buffer> cb;
    static ComPtr<ID3D11BlendState> blend;
    static std::map<std::pair<ID3D11Texture2D*, UINT>, ComPtr<ID3D11RenderTargetView>> rtvs;
    ComPtr<ID3D11Device1> dev1;
    ComPtr<ID3D11DeviceContext1> ctx1;
    if (FAILED(g_device->QueryInterface(IID_PPV_ARGS(&dev1))) || FAILED(g_ctx->QueryInterface(IID_PPV_ARGS(&ctx1))))
        return;
    if (!state) {
        D3D_FEATURE_LEVEL level = g_device->GetFeatureLevel();
        ComPtr<ID3DBlob> v, p;
        if (FAILED(D3DCompile(kTintHlsl, sizeof(kTintHlsl) - 1, "tint", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &v,
                              nullptr)) ||
            FAILED(D3DCompile(kTintHlsl, sizeof(kTintHlsl) - 1, "tint", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &p,
                              nullptr)) ||
            FAILED(dev1->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr,
                                                  &state)))
            return;
        g_device->CreateVertexShader(v->GetBufferPointer(), v->GetBufferSize(), nullptr, &vs);
        g_device->CreatePixelShader(p->GetBufferPointer(), p->GetBufferSize(), nullptr, &ps);
        D3D11_BUFFER_DESC b = {32, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER};
        g_device->CreateBuffer(&b, nullptr, &cb);
        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0] = {TRUE, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD,
                              D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL};
        g_device->CreateBlendState(&bd, &blend);
    }
    auto& rtv = rtvs[{texture, slice}];
    if (!rtv) {
        D3D11_TEXTURE2D_DESC td;
        texture->GetDesc(&td);
        D3D11_RENDER_TARGET_VIEW_DESC d = {typed(format), td.ArraySize > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DARRAY
                                                                           : D3D11_RTV_DIMENSION_TEXTURE2D};
        if (td.ArraySize > 1) d.Texture2DArray = {0, slice, 1};
        if (FAILED(g_device->CreateRenderTargetView(texture, &d, &rtv))) return;
    }
    float params[8];
    Core::get().tint_params(eye, params);
    ComPtr<ID3DDeviceContextState> previous;
    t_inside = true;  // our own binds are not classified
    if (g_bound) set_vrs(nullptr);  // the tint itself at full rate; the app's next bind decides again
    ctx1->SwapDeviceContextState(state.Get(), &previous);
    ctx1->UpdateSubresource(cb.Get(), 0, nullptr, params, 0, 0);
    ctx1->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
    ctx1->OMSetBlendState(blend.Get(), nullptr, 0xffffffff);
    D3D11_VIEWPORT vp = {(float)rect.left, (float)rect.top, (float)(rect.right - rect.left),
                         (float)(rect.bottom - rect.top), 0, 1};
    ctx1->RSSetViewports(1, &vp);
    ctx1->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx1->VSSetShader(vs.Get(), nullptr, 0);
    ctx1->PSSetShader(ps.Get(), nullptr, 0);
    ctx1->PSSetConstantBuffers(0, 1, cb.GetAddressOf());
    ctx1->Draw(3, 0);
    ctx1->SwapDeviceContextState(previous.Get(), nullptr);
    t_inside = false;
}

}  // namespace fov::d3d11
