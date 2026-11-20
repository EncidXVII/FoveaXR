// OpenXR API layer (SPEC 6.1): learns the eye swapchains, layout and FOV, reads eye gaze (XR_EXT_eye_gaze_interaction
// through our own action set, else the shared-memory test source), and hands each frame to the core; the graphics
// backend found at xrCreateSession foveates the eye passes. Inactive processes get pure pass-through.
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "fov_core.h"
#include "fov_d3d11.h"
#include "fov_d3d12.h"
#include "fov_frame.h"
#include "fov_gaze.h"
#include "negotiate.h"

namespace {
constexpr const char* kEyeGazeExt = "XR_EXT_eye_gaze_interaction";
constexpr const char* kEyeProfile = "/interaction_profiles/ext/eye_gaze_interaction";
constexpr int kAttachAloneAfter = 120;  // frames without the app attaching action sets

#define FOV_XR_NEXT(X)                                                                                           \
    X(xrCreateSession) X(xrDestroySession) X(xrCreateSwapchain) X(xrEnumerateSwapchainImages)                   \
    X(xrAcquireSwapchainImage) X(xrReleaseSwapchainImage) X(xrWaitFrame) X(xrBeginFrame) X(xrEndFrame)          \
    X(xrSuggestInteractionProfileBindings) X(xrAttachSessionActionSets) X(xrSyncActions) X(xrLocateViews)       \
    X(xrGetSystemProperties) X(xrStringToPath) X(xrCreateActionSet) X(xrCreateAction) X(xrCreateActionSpace)    \
    X(xrCreateReferenceSpace) X(xrLocateSpace) X(xrGetActionStatePose)
struct Next {
#define FOV_DECLARE(name) PFN_##name name = nullptr;
    FOV_XR_NEXT(FOV_DECLARE)
#undef FOV_DECLARE
} g;
PFN_xrGetInstanceProcAddr g_next_gipa = nullptr;

struct Swapchain {
    int64_t format = 0;
    uint32_t w = 0, h = 0, array = 1;
    std::vector<void*> images;  // ID3D11Texture2D* or ID3D12Resource*
    uint32_t acquired = 0;
    struct Eye {
        int eye;
        RECT rect;
        uint32_t slice;
    };
    std::vector<Eye> eyes;  // from the last xrEndFrame, for the debug tint
};

struct State {
    std::mutex m;
    XrInstance instance = XR_NULL_HANDLE;
    XrSession session = XR_NULL_HANDLE;
    FovGfx gfx = FOV_GFX_NONE;
    bool gaze_ext = false, gaze_supported = false, attached = false;
    bool app_sets = false;  // the app made action sets of its own: it attaches and syncs them, ours rides along
    bool alone = false;     // ours was attached alone: nobody else syncs it
    XrActionSet set = XR_NULL_HANDLE;
    XrAction gaze = XR_NULL_HANDLE;
    XrPath profile = XR_NULL_PATH, binding = XR_NULL_PATH;
    std::vector<XrActionSuggestedBinding> app_eye_bindings;
    XrSpace view = XR_NULL_HANDLE, gaze_space = XR_NULL_HANDLE;
    XrTime display = 0;
    uint64_t frames = 0;
    std::map<XrSwapchain, Swapchain> swapchains;
} s;

bool active() { return fov::Core::get().active(); }

// --- eye gaze -------------------------------------------------------------------------------------------------

void suggest_eye_bindings() {
    std::vector<XrActionSuggestedBinding> b = s.app_eye_bindings;
    b.push_back({s.gaze, s.binding});
    XrInteractionProfileSuggestedBinding sb = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    sb.interactionProfile = s.profile;
    sb.countSuggestedBindings = (uint32_t)b.size();
    sb.suggestedBindings = b.data();
    XrResult r = g.xrSuggestInteractionProfileBindings(s.instance, &sb);
    if (XR_FAILED(r)) fov_log("eye gaze: suggest bindings failed (%d)", (int)r);
}

void setup_gaze(XrSystemId system) {
    XrSystemEyeGazeInteractionPropertiesEXT eye = {XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT};
    XrSystemProperties props = {XR_TYPE_SYSTEM_PROPERTIES, &eye};
    if (!s.gaze_ext || XR_FAILED(g.xrGetSystemProperties(s.instance, system, &props))) return;
    s.gaze_supported = eye.supportsEyeGazeInteraction;
    fov_log("eye gaze: %s", s.gaze_supported ? "supported" : "not supported by this headset");
    if (!s.gaze_supported) return;
    // Action sets belong to the instance: a later session attaches the one made for the first (making it again
    // fails on its name).
    if (s.set) {
        s.gaze_supported = s.binding != XR_NULL_PATH;
        return;
    }
    XrActionSetCreateInfo asci = {XR_TYPE_ACTION_SET_CREATE_INFO};
    snprintf(asci.actionSetName, sizeof(asci.actionSetName), "fov_eye_gaze");
    snprintf(asci.localizedActionSetName, sizeof(asci.localizedActionSetName), "Foveated rendering eye gaze");
    XrActionCreateInfo aci = {XR_TYPE_ACTION_CREATE_INFO};
    aci.actionType = XR_ACTION_TYPE_POSE_INPUT;
    snprintf(aci.actionName, sizeof(aci.actionName), "gaze");
    snprintf(aci.localizedActionName, sizeof(aci.localizedActionName), "Eye gaze");
    if (XR_FAILED(g.xrCreateActionSet(s.instance, &asci, &s.set)) ||
        XR_FAILED(g.xrCreateAction(s.set, &aci, &s.gaze)) ||
        XR_FAILED(g.xrStringToPath(s.instance, kEyeProfile, &s.profile)) ||
        XR_FAILED(g.xrStringToPath(s.instance, "/user/eyes_ext/input/gaze_ext/pose", &s.binding))) {
        fov_log("eye gaze: action setup failed");
        s.gaze_supported = false;
        return;
    }
    suggest_eye_bindings();
}

void after_attach() {
    s.attached = true;
    XrActionSpaceCreateInfo ci = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
    ci.action = s.gaze;
    ci.poseInActionSpace.orientation.w = 1;
    g.xrCreateActionSpace(s.session, &ci, &s.gaze_space);
}

// Rotates v by unit quaternion q.
XrVector3f rotate(const XrQuaternionf& q, XrVector3f v) {
    XrVector3f u = {q.x, q.y, q.z};
    auto cross = [](XrVector3f a, XrVector3f b) {
        return XrVector3f{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    };
    XrVector3f t = cross(u, v);
    t = {2 * t.x, 2 * t.y, 2 * t.z};
    XrVector3f c = cross(u, t);
    return {v.x + q.w * t.x + c.x, v.y + q.w * t.y + c.y, v.z + q.w * t.z + c.z};
}

// A point 2 m along the gaze, projected into each eye's image. False if the tracker has nothing this frame.
bool gaze_uv(const XrView views[2], float out[2][2]) {
    if (!s.gaze_space) return false;
    XrActionStateGetInfo gi = {XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = s.gaze;
    XrActionStatePose st = {XR_TYPE_ACTION_STATE_POSE};
    XrSpaceLocation loc = {XR_TYPE_SPACE_LOCATION};
    const XrSpaceLocationFlags need = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    if (XR_FAILED(g.xrGetActionStatePose(s.session, &gi, &st)) || !st.isActive ||
        XR_FAILED(g.xrLocateSpace(s.gaze_space, s.view, s.display, &loc)) || (loc.locationFlags & need) != need)
        return false;
    XrVector3f d = rotate(loc.pose.orientation, {0, 0, -2});
    XrVector3f p = {loc.pose.position.x + d.x, loc.pose.position.y + d.y, loc.pose.position.z + d.z};
    for (int e = 0; e < 2; ++e) {
        const XrPosef& eye = views[e].pose;
        XrQuaternionf inv = {-eye.orientation.x, -eye.orientation.y, -eye.orientation.z, eye.orientation.w};
        XrVector3f l = rotate(inv, {p.x - eye.position.x, p.y - eye.position.y, p.z - eye.position.z});
        if (l.z > -0.01f) return false;
        const XrFovf& f = views[e].fov;
        float tl = std::tan(f.angleLeft), tr = std::tan(f.angleRight), tu = std::tan(f.angleUp), td = std::tan(f.angleDown);
        float tx = l.x / -l.z, ty = l.y / -l.z;
        out[e][0] = (tx - tl) / (tr - tl);
        out[e][1] = (tu - ty) / (tu - td);
        if (out[e][0] < 0 || out[e][0] > 1 || out[e][1] < 0 || out[e][1] > 1) return false;
    }
    return true;
}

// --- frame ----------------------------------------------------------------------------------------------------

void begin_frame() {
    if (!s.view || !s.display) return;
    // An app without action sets (no controllers) never attaches or syncs any: ours goes alone. Never when it has
    // some: a session takes one attach, and ours going first would make the app's own fail, and a sync of ours
    // alone would switch the app's sets off.
    if (s.gaze_supported && !s.attached && !s.app_sets && s.frames > kAttachAloneAfter) {
        XrSessionActionSetsAttachInfo ai = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        ai.countActionSets = 1;
        ai.actionSets = &s.set;
        if (XR_SUCCEEDED(g.xrAttachSessionActionSets(s.session, &ai))) {
            after_attach();
            s.alone = true;
        } else {
            s.gaze_supported = false;
        }
    }
    if (s.alone) {
        XrActiveActionSet as = {s.set, XR_NULL_PATH};
        XrActionsSyncInfo si = {XR_TYPE_ACTIONS_SYNC_INFO};
        si.countActiveActionSets = 1;
        si.activeActionSets = &as;
        g.xrSyncActions(s.session, &si);
    }
    XrViewLocateInfo li = {XR_TYPE_VIEW_LOCATE_INFO};
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = s.display;
    li.space = s.view;
    XrViewState vs = {XR_TYPE_VIEW_STATE};
    XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    uint32_t n = 0;
    if (XR_FAILED(g.xrLocateViews(s.session, &li, &vs, 2, &n, views)) || n != 2) return;
    fov::Fov fov[2];
    for (int e = 0; e < 2; ++e)
        fov[e] = {std::tan(views[e].fov.angleLeft), std::tan(views[e].fov.angleRight), std::tan(views[e].fov.angleUp),
                  std::tan(views[e].fov.angleDown)};
    float uv[2][2];
    FovGazeSource source = FOV_GAZE_OPENXR;
    bool have = gaze_uv(views, uv);
    if (!have) {
        fov::gaze::Sample sample;
        if (fov::gaze::from_shm(sample, fov_qpc_now()) == fov::gaze::Status::Valid && sample.kind == FOV_GAZE_UV) {
            for (int e = 0; e < 2; ++e) uv[e][0] = sample.eye[e][0], uv[e][1] = sample.eye[e][1];
            source = sample.source;
            have = true;
        }
    }
    fov::Core::get().begin_frame(fov, have ? uv : nullptr, source);
}

// Which swapchain images are which eye, the layout, the eye size: from what the app submits.
void learn(const XrFrameEndInfo* info) {
    for (uint32_t i = 0; i < info->layerCount; ++i) {
        if (info->layers[i]->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION) continue;
        auto* p = reinterpret_cast<const XrCompositionLayerProjection*>(info->layers[i]);
        if (p->viewCount != 2) continue;
        const XrSwapchainSubImage* sub[2] = {&p->views[0].subImage, &p->views[1].subImage};
        auto& core = fov::Core::get();
        FovLayout layout = sub[0]->swapchain != sub[1]->swapchain     ? FOV_LAYOUT_SEPARATE
                           : sub[0]->imageArrayIndex != sub[1]->imageArrayIndex ? FOV_LAYOUT_ARRAY
                                                                                 : FOV_LAYOUT_WIDE;
        core.set_layout(layout);
        core.set_eye_size(sub[0]->imageRect.extent.width, sub[0]->imageRect.extent.height);
        std::lock_guard lock(s.m);
        for (auto& [handle, sc] : s.swapchains) sc.eyes.clear();
        for (int e = 0; e < 2; ++e) {
            auto it = s.swapchains.find(sub[e]->swapchain);
            if (it == s.swapchains.end()) continue;
            const XrRect2Di& r = sub[e]->imageRect;
            it->second.eyes.push_back({e, {r.offset.x, r.offset.y, r.offset.x + r.extent.width, r.offset.y + r.extent.height},
                                       sub[e]->imageArrayIndex});
            if (layout != FOV_LAYOUT_WIDE)
                for (void* img : it->second.images) core.register_eye_image(img, layout == FOV_LAYOUT_ARRAY ? 2 : e);
        }
        return;  // the first projection layer is the scene
    }
}

// --- hooks ----------------------------------------------------------------------------------------------------

XrResult XRAPI_CALL create_session(XrInstance instance, const XrSessionCreateInfo* info, XrSession* session) {
    XrResult r = g.xrCreateSession(instance, info, session);
    if (XR_FAILED(r)) return r;
    s.session = *session;
    auto& core = fov::Core::get();
    for (auto* b = static_cast<const XrBaseInStructure*>(info->next); b; b = b->next) {
        if (b->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
            s.gfx = FOV_GFX_D3D11;
            fov::d3d11::install(reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(b)->device);
        } else if (b->type == XR_TYPE_GRAPHICS_BINDING_D3D12_KHR) {
            auto* d = reinterpret_cast<const XrGraphicsBindingD3D12KHR*>(b);
            s.gfx = FOV_GFX_D3D12;
            fov::d3d12::install(d->device, d->queue);
        }
    }
    if (s.gfx == FOV_GFX_NONE) core.disable(FOV_REASON_UNSUPPORTED_GPU, "graphics API not supported (D3D11 and D3D12 only)");
    XrReferenceSpaceCreateInfo rs = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    rs.poseInReferenceSpace.orientation.w = 1;
    g.xrCreateReferenceSpace(*session, &rs, &s.view);
    setup_gaze(info->systemId);
    return r;
}

XrResult XRAPI_CALL destroy_session(XrSession session) {
    {
        std::lock_guard lock(s.m);
        s.swapchains.clear();
    }
    s.session = XR_NULL_HANDLE, s.view = s.gaze_space = XR_NULL_HANDLE, s.attached = s.alone = false;
    return g.xrDestroySession(session);  // destroys our spaces with it
}

XrResult XRAPI_CALL create_swapchain(XrSession session, const XrSwapchainCreateInfo* info, XrSwapchain* sc) {
    XrResult r = g.xrCreateSwapchain(session, info, sc);
    if (XR_SUCCEEDED(r) && (info->usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT)) {
        std::lock_guard lock(s.m);
        s.swapchains[*sc] = {info->format, info->width, info->height, info->arraySize};
    }
    return r;
}

XrResult XRAPI_CALL enumerate_images(XrSwapchain sc, uint32_t capacity, uint32_t* count,
                                     XrSwapchainImageBaseHeader* images) {
    XrResult r = g.xrEnumerateSwapchainImages(sc, capacity, count, images);
    if (XR_FAILED(r) || !images) return r;
    std::lock_guard lock(s.m);
    auto it = s.swapchains.find(sc);
    if (it == s.swapchains.end()) return r;
    it->second.images.clear();
    for (uint32_t i = 0; i < *count; ++i) {
        if (s.gfx == FOV_GFX_D3D11) it->second.images.push_back(reinterpret_cast<XrSwapchainImageD3D11KHR*>(images)[i].texture);
        if (s.gfx == FOV_GFX_D3D12) it->second.images.push_back(reinterpret_cast<XrSwapchainImageD3D12KHR*>(images)[i].texture);
    }
    return r;
}

XrResult XRAPI_CALL acquire_image(XrSwapchain sc, const XrSwapchainImageAcquireInfo* info, uint32_t* index) {
    XrResult r = g.xrAcquireSwapchainImage(sc, info, index);
    if (XR_SUCCEEDED(r)) {
        std::lock_guard lock(s.m);
        auto it = s.swapchains.find(sc);
        if (it != s.swapchains.end()) it->second.acquired = *index;
    }
    return r;
}

// The debug tint goes in while the app still owns the image, after its rendering was submitted.
XrResult XRAPI_CALL release_image(XrSwapchain sc, const XrSwapchainImageReleaseInfo* info) {
    if (fov::Core::get().tint()) {
        std::lock_guard lock(s.m);
        auto it = s.swapchains.find(sc);
        if (it != s.swapchains.end() && it->second.acquired < it->second.images.size()) {
            Swapchain& c = it->second;
            void* img = c.images[c.acquired];
            for (auto& e : c.eyes) {
                if (s.gfx == FOV_GFX_D3D11)
                    fov::d3d11::tint(static_cast<ID3D11Texture2D*>(img), (DXGI_FORMAT)c.format, e.eye, e.rect, e.slice);
                if (s.gfx == FOV_GFX_D3D12)
                    fov::d3d12::tint(static_cast<ID3D12Resource*>(img), (DXGI_FORMAT)c.format, e.eye, e.rect, e.slice);
            }
        }
    }
    return g.xrReleaseSwapchainImage(sc, info);
}

XrResult XRAPI_CALL wait_frame(XrSession session, const XrFrameWaitInfo* info, XrFrameState* state) {
    XrResult r = g.xrWaitFrame(session, info, state);
    if (XR_SUCCEEDED(r)) s.display = state->predictedDisplayTime;
    return r;
}

XrResult XRAPI_CALL begin_frame_hook(XrSession session, const XrFrameBeginInfo* info) {
    XrResult r = g.xrBeginFrame(session, info);
    if (XR_SUCCEEDED(r)) begin_frame();
    return r;
}

XrResult XRAPI_CALL end_frame(XrSession session, const XrFrameEndInfo* info) {
    learn(info);
    XrResult r = g.xrEndFrame(session, info);
    s.frames++;
    fov::Core::get().end_frame();
    return r;
}

XrResult XRAPI_CALL suggest(XrInstance instance, const XrInteractionProfileSuggestedBinding* b) {
    if (!s.gaze || b->interactionProfile != s.profile) return g.xrSuggestInteractionProfileBindings(instance, b);
    // Suggestions replace earlier ones per profile: keep the app's and ours together.
    s.app_eye_bindings.assign(b->suggestedBindings, b->suggestedBindings + b->countSuggestedBindings);
    suggest_eye_bindings();
    return XR_SUCCESS;
}

XrResult XRAPI_CALL attach(XrSession session, const XrSessionActionSetsAttachInfo* info) {
    if (!s.gaze_supported || s.attached) return g.xrAttachSessionActionSets(session, info);
    std::vector<XrActionSet> sets(info->actionSets, info->actionSets + info->countActionSets);
    sets.push_back(s.set);
    XrSessionActionSetsAttachInfo mine = *info;
    mine.countActionSets = (uint32_t)sets.size();
    mine.actionSets = sets.data();
    XrResult r = g.xrAttachSessionActionSets(session, &mine);
    if (XR_SUCCEEDED(r)) after_attach();
    return r;
}

XrResult XRAPI_CALL sync(XrSession session, const XrActionsSyncInfo* info) {
    if (!s.attached) return g.xrSyncActions(session, info);
    std::vector<XrActiveActionSet> sets(info->activeActionSets, info->activeActionSets + info->countActiveActionSets);
    sets.push_back({s.set, XR_NULL_PATH});
    XrActionsSyncInfo mine = *info;
    mine.countActiveActionSets = (uint32_t)sets.size();
    mine.activeActionSets = sets.data();
    return g.xrSyncActions(session, &mine);
}

XrResult XRAPI_CALL create_action_set(XrInstance instance, const XrActionSetCreateInfo* info, XrActionSet* set) {
    s.app_sets = true;
    return g.xrCreateActionSet(instance, info, set);
}

XrResult XRAPI_CALL get_proc(XrInstance instance, const char* name, PFN_xrVoidFunction* fn);

struct Hook {
    const char* name;
    PFN_xrVoidFunction fn;
};
const Hook kHooks[] = {
    {"xrCreateSession", (PFN_xrVoidFunction)create_session},
    {"xrDestroySession", (PFN_xrVoidFunction)destroy_session},
    {"xrCreateSwapchain", (PFN_xrVoidFunction)create_swapchain},
    {"xrEnumerateSwapchainImages", (PFN_xrVoidFunction)enumerate_images},
    {"xrAcquireSwapchainImage", (PFN_xrVoidFunction)acquire_image},
    {"xrReleaseSwapchainImage", (PFN_xrVoidFunction)release_image},
    {"xrWaitFrame", (PFN_xrVoidFunction)wait_frame},
    {"xrBeginFrame", (PFN_xrVoidFunction)begin_frame_hook},
    {"xrEndFrame", (PFN_xrVoidFunction)end_frame},
    {"xrSuggestInteractionProfileBindings", (PFN_xrVoidFunction)suggest},
    {"xrAttachSessionActionSets", (PFN_xrVoidFunction)attach},
    {"xrSyncActions", (PFN_xrVoidFunction)sync},
    {"xrCreateActionSet", (PFN_xrVoidFunction)create_action_set},
};

XrResult XRAPI_CALL get_proc(XrInstance instance, const char* name, PFN_xrVoidFunction* fn) {
    if (!std::strcmp(name, "xrGetInstanceProcAddr")) {
        *fn = reinterpret_cast<PFN_xrVoidFunction>(get_proc);
        return XR_SUCCESS;
    }
    if (!g_next_gipa) return XR_ERROR_HANDLE_INVALID;
    if (active() && s.instance) {
        for (const Hook& h : kHooks) {
            if (std::strcmp(name, h.name)) continue;
            // Only where the runtime has the function too.
            XrResult r = g_next_gipa(instance, name, fn);
            if (XR_SUCCEEDED(r)) *fn = h.fn;
            return r;
        }
    }
    return g_next_gipa(instance, name, fn);
}

bool runtime_has(PFN_xrGetInstanceProcAddr gipa, const char* ext) {
    PFN_xrEnumerateInstanceExtensionProperties enumerate = nullptr;
    if (XR_FAILED(gipa(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties", (PFN_xrVoidFunction*)&enumerate)))
        return false;
    uint32_t n = 0;
    enumerate(nullptr, 0, &n, nullptr);
    std::vector<XrExtensionProperties> props(n, {XR_TYPE_EXTENSION_PROPERTIES});
    enumerate(nullptr, n, &n, props.data());
    for (auto& p : props)
        if (!std::strcmp(p.extensionName, ext)) return true;
    return false;
}

XrResult XRAPI_CALL create_instance(const XrInstanceCreateInfo* info, const XrApiLayerCreateInfo* layer,
                                    XrInstance* instance) {
    if (!fov::Core::get().start(FOV_API_OPENXR) || !layer || !layer->nextInfo)
        return fov::xr::create_next(info, layer, instance, g_next_gipa);
    // Ask for eye gaze on the app's behalf when the runtime has it.
    std::vector<const char*> exts(info->enabledExtensionNames, info->enabledExtensionNames + info->enabledExtensionCount);
    bool asked = false;
    for (auto e : exts) asked |= !std::strcmp(e, kEyeGazeExt);
    XrInstanceCreateInfo mine = *info;
    if (!asked && runtime_has(layer->nextInfo->nextGetInstanceProcAddr, kEyeGazeExt)) {
        exts.push_back(kEyeGazeExt);
        mine.enabledExtensionCount = (uint32_t)exts.size();
        mine.enabledExtensionNames = exts.data();
        asked = true;
    }
    XrResult r = fov::xr::create_next(&mine, layer, instance, g_next_gipa);
    if (XR_FAILED(r)) return r;
    s.instance = *instance;
    s.gaze_ext = asked;
    // A new instance: the action set and paths of an earlier one died with it.
    s.set = XR_NULL_HANDLE, s.gaze = XR_NULL_HANDLE, s.profile = s.binding = XR_NULL_PATH;
    s.app_eye_bindings.clear();
    s.app_sets = false;
#define FOV_LOAD(name) g_next_gipa(*instance, #name, reinterpret_cast<PFN_xrVoidFunction*>(&g.name));
    FOV_XR_NEXT(FOV_LOAD)
#undef FOV_LOAD
    fov_log("layer: instance for \"%s\" (engine \"%s\"), eye gaze extension %s", info->applicationInfo.applicationName,
            info->applicationInfo.engineName, asked ? "on" : "off");
    return r;
}
}  // namespace

extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loader, const char* /*layerName*/, XrNegotiateApiLayerRequest* request) {
    return fov::xr::negotiate(loader, request, get_proc, create_instance);
}
