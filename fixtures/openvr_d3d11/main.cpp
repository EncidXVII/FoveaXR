// OpenVR D3D11 fixture: separate eye textures submitted to IVRCompositor, like most OpenVR games.
#include <d3d11.h>
#include <openvr.h>
#include "d3d11_scene.h"

using namespace fixture;

int main(int argc, char** argv) {
    Args args = parse(argc, argv);
    vr::EVRInitError err = vr::VRInitError_None;
    vr::IVRSystem* system = vr::VR_Init(&err, vr::VRApplication_Scene);
    if (!system) {
        log("VR_Init failed: %s", vr::VR_GetVRInitErrorAsEnglishDescription(err));
        return 2;
    }
    int32_t index = -1;
    system->GetDXGIOutputInfo(&index);
    auto adapter = pick_adapter({}, args.adapter >= 0 ? args.adapter : index);
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> ctx;
    check(D3D11CreateDevice(adapter.Get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                            nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &ctx),
          "D3D11CreateDevice");
    D3d11Scene scene(device.Get());

    uint32_t w = 0, h = 0;
    system->GetRecommendedRenderTargetSize(&w, &h);
    ComPtr<ID3D11Texture2D> tex[2];
    ComPtr<ID3D11RenderTargetView> rtv[2];
    for (int eye = 0; eye < 2; ++eye) {
        D3D11_TEXTURE2D_DESC t = {w, h, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, {1, 0}, D3D11_USAGE_DEFAULT,
                                  D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE};
        check(device->CreateTexture2D(&t, nullptr, &tex[eye]), "eye texture");
        check(device->CreateRenderTargetView(tex[eye].Get(), nullptr, &rtv[eye]), "eye RTV");
    }
    // OpenVR's raw projection is y-down (top negative); the scene wants up positive.
    float tan[2][4];
    for (int eye = 0; eye < 2; ++eye) {
        float l, r, t, b;
        system->GetProjectionRaw((vr::EVREye)eye, &l, &r, &t, &b);
        tan[eye][0] = l, tan[eye][1] = r, tan[eye][2] = -t, tan[eye][3] = -b;
    }
    log("openvr d3d11: %ux%u per eye, heavy %d", w, h, args.heavy);

    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    long long frame = 0;
    bool quit = false;
    for (; !quit && (args.frames == 0 || frame < args.frames); ++frame) {
        vr::VREvent_t event;
        while (system->PollNextEvent(&event, sizeof(event)))
            if (event.eventType == vr::VREvent_Quit) {
                log("quit requested by the runtime");
                quit = true;
            }
        vr::EVRCompositorError e = vr::VRCompositor()->WaitGetPoses(poses, vr::k_unMaxTrackedDeviceCount, nullptr, 0);
        if (e != vr::VRCompositorError_None) log("WaitGetPoses: %d", (int)e);
        for (int eye = 0; eye < 2; ++eye) {
            scene.render(ctx.Get(), rtv[eye].Get(), eye, w, h, constants(tan[eye], frame, args.heavy));
            vr::Texture_t texture = {tex[eye].Get(), vr::TextureType_DirectX, vr::ColorSpace_Auto};
            e = vr::VRCompositor()->Submit((vr::EVREye)eye, &texture);
            if (e != vr::VRCompositorError_None) log("Submit eye %d: %d", eye, (int)e);
        }
        if (frame == 0) log("first frame submitted");
    }
    QueryPerformanceCounter(&t1);
    log("done: %lld frames, %.2f ms per frame", frame, frame ? (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart / frame : 0.0);
    vr::VR_Shutdown();
    return 0;
}
