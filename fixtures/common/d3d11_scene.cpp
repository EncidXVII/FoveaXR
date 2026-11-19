#include "d3d11_scene.h"

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

}  // namespace fixture
