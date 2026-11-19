#pragma once
// The fixture scene on D3D11: eye pass (colour + depth bound) then UI pass (colour only), into any eye-sized RTV.
#include <d3d11.h>
#include "d3d_common.h"

namespace fixture {

class D3d11Scene {
public:
    explicit D3d11Scene(ID3D11Device* device);
    // Depth is the fixture's own, one per eye, made on first use at the eye's size.
    void render(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, int eye, UINT w, UINT h, const Constants& c);
    // A top-down 32-bit BMP of an 8-bit RGBA or BGRA texture, for the harness's image checks.
    void dump(ID3D11DeviceContext* ctx, ID3D11Texture2D* texture, const std::string& path);

private:
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> scene_, ui_;
    ComPtr<ID3D11Buffer> cb_;
    ComPtr<ID3D11DepthStencilState> depth_on_;
    ComPtr<ID3D11DepthStencilView> dsv_[2];
    UINT dsv_w_[2] = {}, dsv_h_[2] = {};
};

}  // namespace fixture
