# Third-party code, pinned by hash (licenses in THIRD_PARTY_NOTICES.md). SOURCE_SUBDIR none: headers and prebuilt
# libraries only, their own CMake projects are not added.
include(FetchContent)

FetchContent_Declare(openxr_sdk
    URL https://github.com/KhronosGroup/OpenXR-SDK-Source/releases/download/release-1.1.65/openxr_loader_windows-1.1.65.zip
    URL_HASH SHA256=859d7de35b7a968112db770b09ed8bddeed3cb16fd8ae070643a857676bea6fd
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR none)
FetchContent_Declare(vulkan_headers
    URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-1.4.363.0.zip
    URL_HASH SHA256=f4be95220ff0ee0b1c620301ff4fe9ae48592d71f4c30f2d5c571351a153ae81
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR none)
FetchContent_Declare(nvapi
    URL https://github.com/NVIDIA/nvapi/archive/51e5aae4bc40c3ccd3fb7d4dfd94256e7bfc496e.zip
    URL_HASH SHA256=7f3b786c35734df2572ef92f1346a5459c180446b32850026e0208a5eefa6cd5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR none)
FetchContent_Declare(minhook
    URL https://github.com/TsudaKageyu/minhook/archive/refs/tags/v1.3.4.zip
    URL_HASH SHA256=172708123daa0c98d20d3a980b16a50be14af243dc95dee6f79c24193ad010e4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR none)
FetchContent_MakeAvailable(openxr_sdk vulkan_headers nvapi minhook)

# MinHook (BSD-2-Clause): inline hooks on the graphics APIs' render-target calls.
add_library(fov_minhook STATIC ${minhook_SOURCE_DIR}/src/buffer.c ${minhook_SOURCE_DIR}/src/hook.c
    ${minhook_SOURCE_DIR}/src/trampoline.c ${minhook_SOURCE_DIR}/src/hde/hde64.c)
target_include_directories(fov_minhook PUBLIC ${minhook_SOURCE_DIR}/include)

# OpenVR SDK files: the header (proxy and fixture), the import library and the DLL games ship (fixture).
set(openvr_dir ${CMAKE_BINARY_DIR}/_deps/openvr)
foreach(item
        "headers/openvr.h=1e6ed57199896cc1f7c5484e50fa18955e97be15be690beb28d998c877ead7fd"
        "lib/win64/openvr_api.lib=a0bf57c5920f569e8d21ab3e5bc95bac4b73e2016217f8b5b93495a2a7197bbb"
        "bin/win64/openvr_api.dll=bab8ac6ef64e68a9ca53315b0014d131088584b2efdfa6db511d67ec03cfcb4a"
        "LICENSE=f56ff606104d4ef18e617921a75c73ad73b5a1a1d70c69590c29de16919e04ad")
    string(REPLACE "=" ";" item ${item})
    list(GET item 0 path)
    list(GET item 1 hash)
    file(DOWNLOAD https://raw.githubusercontent.com/ValveSoftware/openvr/v2.15.6/${path} ${openvr_dir}/${path}
        EXPECTED_HASH SHA256=${hash})
endforeach()

add_library(fov_openxr_headers INTERFACE)
target_include_directories(fov_openxr_headers INTERFACE ${openxr_sdk_SOURCE_DIR}/include)
target_compile_definitions(fov_openxr_headers INTERFACE XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_D3D11
    XR_USE_GRAPHICS_API_D3D12 XR_USE_GRAPHICS_API_VULKAN)

add_library(fov_vulkan_headers INTERFACE)
target_include_directories(fov_vulkan_headers INTERFACE ${vulkan_headers_SOURCE_DIR}/include)

add_library(fov_nvapi INTERFACE)
target_include_directories(fov_nvapi INTERFACE ${nvapi_SOURCE_DIR})
target_link_libraries(fov_nvapi INTERFACE ${nvapi_SOURCE_DIR}/amd64/nvapi64.lib)
