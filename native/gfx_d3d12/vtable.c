// Copyright (C) 2026 EncidXVII
// SPDX-License-Identifier: GPL-3.0-or-later

/* Vtable slots from the SDK's C declarations, so no index is typed by hand. */
#pragma warning(disable : 4201) /* the SDK headers in C mode use nameless unions */
#define CINTERFACE
#include <stddef.h>
#include <d3d12.h>

#define SLOT(iface, method) (unsigned)(offsetof(iface##Vtbl, method) / sizeof(void*))
const unsigned fov_d3d12_slot_create_rtv = SLOT(ID3D12Device, CreateRenderTargetView);
const unsigned fov_d3d12_slot_create_dsv = SLOT(ID3D12Device, CreateDepthStencilView);
const unsigned fov_d3d12_slot_close = SLOT(ID3D12GraphicsCommandList4, Close);
const unsigned fov_d3d12_slot_reset = SLOT(ID3D12GraphicsCommandList4, Reset);
const unsigned fov_d3d12_slot_om_set = SLOT(ID3D12GraphicsCommandList4, OMSetRenderTargets);
const unsigned fov_d3d12_slot_begin_pass = SLOT(ID3D12GraphicsCommandList4, BeginRenderPass);
const unsigned fov_d3d12_slot_end_pass = SLOT(ID3D12GraphicsCommandList4, EndRenderPass);
