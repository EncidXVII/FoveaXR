#include <MinHook.h>
#include <mutex>
#include "fov_frame.h"

namespace fov {

bool hook(void* target, void* detour, void** original) {
    static std::once_flag once;
    std::call_once(once, [] { MH_Initialize(); });
    MH_STATUS s = MH_CreateHook(target, detour, original);
    if (s == MH_OK) s = MH_EnableHook(target);
    if (s != MH_OK) fov_log("hook %p failed: %s", target, MH_StatusToString(s));
    return s == MH_OK;
}

}  // namespace fov
