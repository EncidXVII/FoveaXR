#include "d3d11_scene.h"
#include <cstring>
#include <vector>

namespace fixture {

D3d11Scene::D3d11Scene(ID3D11Device* device) : device_(device) {
    auto vs = compile("vs_main", "vs_5_0");
    auto scene = compile("ps_scene", "ps_5_0");
    auto ui = compile("ps_ui", "ps_5_0");
    check(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vs_), "CreateVertexShader");
    check(device->CreatePixelShader(scene->GetBufferPointer(), scene->GetBufferSize(), nullptr, &scene_), "ps_scene");
    check(device->CreatePixelShader(ui->GetBufferPointer(), ui->GetBufferSize(), nullptr, &ui_), "ps_ui");
    D3D11_BUFFER_DESC b = {sizeof(Constants), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER};
    check(device->CreateBuffer(&b, nullptr, &cb_), "CreateBuffer");
    D3D11_DEPTH_STENCIL_DESC d = {TRUE, D3D11_DEPTH_WRITE_MASK_ALL, D3D11_COMPARISON_LESS_EQUAL};
    check(device->CreateDepthStencilState(&d, &depth_on_), "CreateDepthStencilState");
}

void D3d11Scene::render(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, int eye, UINT w, UINT h,
                        const Constants& c) {
    if (!dsv_[eye] || dsv_w_[eye] != w || dsv_h_[eye] != h) {
        D3D11_TEXTURE2D_DESC t = {w, h, 1, 1, DXGI_FORMAT_D32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
                                  D3D11_BIND_DEPTH_STENCIL};
        ComPtr<ID3D11Texture2D> depth;
        check(device_->CreateTexture2D(&t, nullptr, &depth), "depth texture");
        check(device_->CreateDepthStencilView(depth.Get(), nullptr, &dsv_[eye]), "CreateDepthStencilView");
        dsv_w_[eye] = w, dsv_h_[eye] = h;
    }
    ctx->UpdateSubresource(cb_.Get(), 0, nullptr, &c, 0, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(nullptr);
    ctx->VSSetShader(vs_.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, cb_.GetAddressOf());

    // Eye pass: eye-sized colour with eye-sized depth.
    const float clear[4] = {0, 0, 0, 1};
    ctx->ClearRenderTargetView(rtv, clear);
    ctx->ClearDepthStencilView(dsv_[eye].Get(), D3D11_CLEAR_DEPTH, 1, 0);
    ctx->OMSetRenderTargets(1, &rtv, dsv_[eye].Get());
    ctx->OMSetDepthStencilState(depth_on_.Get(), 0);
    D3D11_VIEWPORT vp = {0, 0, (float)w, (float)h, 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->PSSetShader(scene_.Get(), nullptr, 0);
    ctx->Draw(3, 0);

    // UI pass: same colour target, no depth.
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->OMSetDepthStencilState(nullptr, 0);
    D3D11_VIEWPORT ui = {kUi[0] * w, kUi[1] * h, (kUi[2] - kUi[0]) * w, (kUi[3] - kUi[1]) * h, 0, 1};
    ctx->RSSetViewports(1, &ui);
    ctx->PSSetShader(ui_.Get(), nullptr, 0);
    ctx->Draw(3, 0);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
}

void D3d11Scene::dump(ID3D11DeviceContext* ctx, ID3D11Texture2D* texture, const std::string& path) {
    D3D11_TEXTURE2D_DESC d;
    texture->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING, d.BindFlags = 0, d.CPUAccessFlags = D3D11_CPU_ACCESS_READ, d.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    check(device_->CreateTexture2D(&d, nullptr, &staging), "staging texture");
    ctx->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE m;
    check(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m), "Map");
    bool rgba = d.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
                d.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS;
    std::vector<uint8_t> px(size_t(d.Width) * d.Height * 4);
    for (UINT y = 0; y < d.Height; ++y)
        for (UINT x = 0; x < d.Width; ++x) {
            const uint8_t* s = static_cast<uint8_t*>(m.pData) + size_t(y) * m.RowPitch + x * 4;
            uint8_t* o = &px[(size_t(y) * d.Width + x) * 4];
            o[0] = s[rgba ? 2 : 0], o[1] = s[1], o[2] = s[rgba ? 0 : 2], o[3] = s[3];
        }
    ctx->Unmap(staging.Get(), 0);
    uint8_t header[54] = {'B', 'M'};
    auto put = [&](int at, uint32_t v) { std::memcpy(header + at, &v, 4); };
    put(2, uint32_t(54 + px.size())), put(10, 54), put(14, 40), put(18, d.Width), put(22, uint32_t(-int32_t(d.Height)));
    header[26] = 1, header[28] = 32;
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") || !f) return;
    fwrite(header, 1, sizeof(header), f);
    fwrite(px.data(), 1, px.size(), f);
    fclose(f);
    log("dumped %s", path.c_str());
}

}  // namespace fixture
