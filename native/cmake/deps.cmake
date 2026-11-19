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
FetchContent_MakeAvailable(openxr_sdk vulkan_headers nvapi)

add_library(fov_openxr_headers INTERFACE)
target_include_directories(fov_openxr_headers INTERFACE ${openxr_sdk_SOURCE_DIR}/include)
target_compile_definitions(fov_openxr_headers INTERFACE XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_D3D11
    XR_USE_GRAPHICS_API_D3D12 XR_USE_GRAPHICS_API_VULKAN)

add_library(fov_vulkan_headers INTERFACE)
target_include_directories(fov_vulkan_headers INTERFACE ${vulkan_headers_SOURCE_DIR}/include)

add_library(fov_nvapi INTERFACE)
target_include_directories(fov_nvapi INTERFACE ${nvapi_SOURCE_DIR})
target_link_libraries(fov_nvapi INTERFACE ${nvapi_SOURCE_DIR}/amd64/nvapi64.lib)
