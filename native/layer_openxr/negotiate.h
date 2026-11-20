// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
// OpenXR loader negotiation and instance-creation chaining, shared by the product layer and the harness layer.
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

namespace fov::xr {

inline XrResult negotiate(const XrNegotiateLoaderInfo* loader, XrNegotiateApiLayerRequest* request,
                          PFN_xrGetInstanceProcAddr gipa, PFN_xrCreateApiLayerInstance create) {
    if (!loader || !request || loader->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        loader->structVersion != XR_LOADER_INFO_STRUCT_VERSION || loader->structSize != sizeof(XrNegotiateLoaderInfo) ||
        request->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
        request->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION ||
        request->structSize != sizeof(XrNegotiateApiLayerRequest) ||
        loader->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loader->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION)
        return XR_ERROR_INITIALIZATION_FAILED;
    request->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    request->layerApiVersion = XR_CURRENT_API_VERSION;
    request->getInstanceProcAddr = gipa;
    request->createApiLayerInstance = create;
    return XR_SUCCESS;
}

// Calls the next layer's instance creation; `next_gipa` receives the next xrGetInstanceProcAddr.
inline XrResult create_next(const XrInstanceCreateInfo* info, const XrApiLayerCreateInfo* layer, XrInstance* instance,
                            PFN_xrGetInstanceProcAddr& next_gipa) {
    if (!layer || layer->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO || !layer->nextInfo)
        return XR_ERROR_INITIALIZATION_FAILED;
    next_gipa = layer->nextInfo->nextGetInstanceProcAddr;
    XrApiLayerCreateInfo next = *layer;
    next.nextInfo = layer->nextInfo->next;
    return layer->nextInfo->nextCreateApiLayerInstance(info, &next, instance);
}

}  // namespace fov::xr
