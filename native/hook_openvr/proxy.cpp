// openvr_api.dll proxy (SPEC 6.2). This phase forwards every export to the game's original, renamed
// openvr_api.fov-orig.dll beside it; interception of the compositor comes with the D3D11 path.
#include <windows.h>
#include <cstdint>
#include <mutex>
#include <string>
#include "exports.h"
#include "fov_core.h"

namespace {
#define FOV_NAME(n) #n,
constexpr const char* kNames[] = {FOV_OPENVR_EXPORTS(FOV_NAME)};
#undef FOV_NAME
constexpr size_t kCount = sizeof(kNames) / sizeof(kNames[0]);

// What a stub lands on when the original lacks that export (an older openvr_api) or is missing: 0 / null / false
// for every return type the API uses.
uintptr_t missing_export() { return 0; }

std::wstring own_dir() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&own_dir), &self);
    wchar_t path[MAX_PATH];
    std::wstring s(path, GetModuleFileNameW(self, path, MAX_PATH));
    return s.substr(0, s.find_last_of(L"\\/") + 1);
}
}  // namespace

extern "C" void* fov_proxy_targets[kCount] = {};

extern "C" void* fov_proxy_resolve(uint64_t index) {
    static std::once_flag once;
    std::call_once(once, [] {
        // Full path: the search order must never find this proxy again, or a stray copy elsewhere.
        std::wstring path = own_dir() + L"openvr_api.fov-orig.dll";
        HMODULE orig = LoadLibraryW(path.c_str());
        if (!orig) fov_log("openvr proxy: cannot load %ls (%lu); every call returns 0", path.c_str(), GetLastError());
        int missing = 0;
        for (size_t i = 0; i < kCount; ++i) {
            FARPROC f = orig ? GetProcAddress(orig, kNames[i]) : nullptr;
            missing += !f;
            fov_proxy_targets[i] = f ? reinterpret_cast<void*>(f) : reinterpret_cast<void*>(&missing_export);
        }
        if (orig) fov_log("openvr proxy %s: forwarding to %ls (%d exports absent)", fov_version(), path.c_str(), missing);
    });
    return index < kCount ? fov_proxy_targets[index] : reinterpret_cast<void*>(&missing_export);
}
