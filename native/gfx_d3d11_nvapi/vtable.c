/* Vtable slots from the SDK's C declarations, so no index is typed by hand. */
#pragma warning(disable : 4201) /* the SDK headers in C mode use nameless unions */
#define CINTERFACE
#include <stddef.h>
#include <d3d11.h>

const unsigned fov_d3d11_slot_om_set = offsetof(ID3D11DeviceContextVtbl, OMSetRenderTargets) / sizeof(void*);
const unsigned fov_d3d11_slot_om_set_uav =
    offsetof(ID3D11DeviceContextVtbl, OMSetRenderTargetsAndUnorderedAccessViews) / sizeof(void*);
