// GPU check of the raw NVAPI VRS mechanism, outside any game: a per-pixel noise shader drawn with a rate image of
// all 4x4 must come out in uniform 4x4 blocks. Local only (needs an NVIDIA GPU).
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <nvapi.h>
#include <cstdio>
#include <vector>

using Microsoft::WRL::ComPtr;

static const char kHlsl[] = R"(
float4 vs(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0.5, 1);
}
float4 ps(float4 p : SV_Position) : SV_Target {
    return float4(frac(sin(dot(p.xy, float2(12.9898, 78.233))) * 43758.5453), 0, 0, 1);
}
)";

int main() {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                 nullptr, &ctx)))
        return 2;
    if (NvAPI_Initialize() != NVAPI_OK) return printf("no NVAPI\n"), 3;
    const UINT n = 256;
    D3D11_TEXTURE2D_DESC td = {n, n, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET};
    ComPtr<ID3D11Texture2D> rt, staging, rate;
    ComPtr<ID3D11RenderTargetView> rtv;
    dev->CreateTexture2D(&td, nullptr, &rt);
    dev->CreateRenderTargetView(rt.Get(), nullptr, &rtv);
    td.Usage = D3D11_USAGE_STAGING, td.BindFlags = 0, td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    dev->CreateTexture2D(&td, nullptr, &staging);
    std::vector<uint8_t> rates(16 * 16, 6);
    D3D11_TEXTURE2D_DESC rd = {16, 16, 1, 1, DXGI_FORMAT_R8_UINT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE};
    D3D11_SUBRESOURCE_DATA init = {rates.data(), 16};
    dev->CreateTexture2D(&rd, &init, &rate);
    NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC vd = {NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC_VER, DXGI_FORMAT_R8_UINT,
                                                   NV_SRRV_DIMENSION_TEXTURE2D};
    ComPtr<ID3D11NvShadingRateResourceView> view;
    if (NvAPI_D3D11_CreateShadingRateResourceView(dev.Get(), rate.Get(), &vd, &view) != NVAPI_OK) return 4;
    ComPtr<ID3DBlob> v, p;
    D3DCompile(kHlsl, sizeof(kHlsl) - 1, "t", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &v, nullptr);
    D3DCompile(kHlsl, sizeof(kHlsl) - 1, "t", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &p, nullptr);
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    dev->CreateVertexShader(v->GetBufferPointer(), v->GetBufferSize(), nullptr, &vs);
    dev->CreatePixelShader(p->GetBufferPointer(), p->GetBufferSize(), nullptr, &ps);

    ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
    D3D11_VIEWPORT vp = {0, 0, (float)n, (float)n, 0, 1};
    ctx->RSSetViewports(1, &vp);
    NV_D3D11_VIEWPORT_SHADING_RATE_DESC vps[NV_MAX_NUM_VIEWPORTS] = {};
    for (auto& d : vps) {
        d.enableVariablePixelShadingRate = true;
        for (int i = 0; i < NV_MAX_PIXEL_SHADING_RATES; ++i) d.shadingRateTable[i] = NV_PIXEL_X1_PER_4X4_RASTER_PIXELS;
    }
    NV_D3D11_VIEWPORTS_SHADING_RATE_DESC all = {NV_D3D11_VIEWPORTS_SHADING_RATE_DESC_VER, NV_MAX_NUM_VIEWPORTS, vps};
    if (NvAPI_D3D11_RSSetViewportsPixelShadingRates(ctx.Get(), &all) != NVAPI_OK ||
        NvAPI_D3D11_RSSetShadingRateResourceView(ctx.Get(), view.Get()) != NVAPI_OK)
        return 5;
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs.Get(), nullptr, 0);
    ctx->PSSetShader(ps.Get(), nullptr, 0);
    ctx->Draw(3, 0);
    ctx->CopyResource(staging.Get(), rt.Get());
    D3D11_MAPPED_SUBRESOURCE m;
    ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m);
    auto px = [&](UINT x, UINT y) { return static_cast<uint8_t*>(m.pData)[y * m.RowPitch + x * 4]; };
    int uniform = 0, blocks = 0;
    for (UINT by = 0; by < n; by += 4)
        for (UINT bx = 0; bx < n; bx += 4) {
            bool same = true;
            for (UINT y = 0; y < 4; ++y)
                for (UINT x = 0; x < 4; ++x) same &= px(bx + x, by + y) == px(bx, by);
            uniform += same, ++blocks;
        }
    printf("uniform 4x4 blocks: %d of %d\n", uniform, blocks);
    return uniform * 10 > blocks * 9 ? 0 : 1;
}
