// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

// Every export of the public openvr_api.dll (v2.15.6 and the older subsets games ship), in the order of the
// FOV_PROXY_TARGETS table: stubs.asm and openvr_api.def list the same names at the same indices.
#pragma once
#define FOV_OPENVR_EXPORTS(X)                                                                                       \
    X(LiquidVR) X(VRControlPanel) X(VRHeadsetView) X(VRPaths) X(VRVirtualDisplay) X(VR_GetGenericInterface)        \
    X(VR_GetInitToken) X(VR_GetRuntimePath) X(VR_GetStringForHmdError) X(VR_GetVRInitErrorAsEnglishDescription)   \
    X(VR_GetVRInitErrorAsSymbol) X(VR_InitInternal) X(VR_InitInternal2) X(VR_IsHmdPresent)                        \
    X(VR_IsInterfaceVersionValid) X(VR_IsRuntimeInstalled) X(VR_RuntimePath) X(VR_ShutdownInternal)
