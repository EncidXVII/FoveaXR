# Warnings are errors here (not in third-party code): CI fails on a new one.
add_library(fov_warnings INTERFACE)
target_compile_options(fov_warnings INTERFACE $<$<COMPILE_LANGUAGE:CXX>:/W4 /WX /permissive- /EHsc /utf-8>)
target_compile_definitions(fov_warnings INTERFACE WIN32_LEAN_AND_MEAN NOMINMAX)

add_library(fov_core STATIC core/log.cpp core/shm.cpp core/settings.cpp core/rings.cpp core/safety.cpp
    core/core.cpp core/hook.cpp)
target_include_directories(fov_core PUBLIC include)
target_compile_definitions(fov_core PRIVATE FOV_VERSION="${PROJECT_VERSION}")
target_link_libraries(fov_core PRIVATE fov_minhook fov_warnings)

add_library(gaze STATIC gaze/shm_source.cpp)
target_include_directories(gaze PUBLIC gaze)
target_link_libraries(gaze PUBLIC fov_core PRIVATE fov_warnings)

add_library(gfx_d3d11_nvapi STATIC gfx_d3d11_nvapi/caps.cpp gfx_d3d11_nvapi/backend.cpp gfx_d3d11_nvapi/vtable.c)
target_include_directories(gfx_d3d11_nvapi PUBLIC gfx_d3d11_nvapi)
target_link_libraries(gfx_d3d11_nvapi PUBLIC d3d11 fov_core PRIVATE fov_nvapi d3dcompiler fov_warnings)

add_library(gfx_d3d12 STATIC gfx_d3d12/caps.cpp gfx_d3d12/backend.cpp gfx_d3d12/vtable.c)
target_include_directories(gfx_d3d12 PUBLIC gfx_d3d12)
target_link_libraries(gfx_d3d12 PUBLIC d3d12 fov_core PRIVATE d3dcompiler fov_warnings)

add_library(gfx_vulkan STATIC gfx_vulkan/caps.cpp)
target_include_directories(gfx_vulkan PUBLIC gfx_vulkan)
target_link_libraries(gfx_vulkan PUBLIC fov_vulkan_headers PRIVATE fov_warnings)

# OpenXR API layer: fov_openxr_layer.dll + its manifest beside it (registered per user by the installer, or found
# through XR_API_LAYER_PATH by the harness).
add_library(layer_openxr SHARED layer_openxr/layer.cpp)
set_target_properties(layer_openxr PROPERTIES OUTPUT_NAME fov_openxr_layer)
target_link_libraries(layer_openxr PRIVATE fov_core gaze gfx_d3d11_nvapi gfx_d3d12 gfx_vulkan fov_openxr_headers
    fov_warnings)
set(FOV_VERSION_NUMBER ${PROJECT_VERSION_MAJOR}${PROJECT_VERSION_MINOR}${PROJECT_VERSION_PATCH})
configure_file(layer_openxr/manifest.json.in ${CMAKE_CURRENT_BINARY_DIR}/fov_openxr_layer.json @ONLY)
file(GENERATE OUTPUT $<TARGET_FILE_DIR:layer_openxr>/fov_openxr_layer.json
    INPUT ${CMAKE_CURRENT_BINARY_DIR}/fov_openxr_layer.json)
# Safety lists beside the layer, where the core looks for them (the installer does the same).
add_custom_command(TARGET layer_openxr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/profiles $<TARGET_FILE_DIR:layer_openxr>/profiles)

# OpenVR proxy: openvr_api.dll in its own folder, so it never sits beside the fixture's real openvr_api.dll.
add_library(hook_openvr SHARED hook_openvr/proxy.cpp hook_openvr/stubs.asm hook_openvr/openvr_api.def)
set_target_properties(hook_openvr PROPERTIES OUTPUT_NAME openvr_api
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/openvr_proxy)
target_include_directories(hook_openvr PRIVATE ${openvr_dir}/headers)
target_link_libraries(hook_openvr PRIVATE fov_core gaze gfx_d3d11_nvapi gfx_d3d12 fov_warnings)

# Test-only layer the harness puts next to the runtime: names a GPU where the null driver names none.
add_library(harness_layer SHARED tools/harness_layer.cpp)
set_target_properties(harness_layer PROPERTIES OUTPUT_NAME fov_harness_layer)
target_include_directories(harness_layer PRIVATE layer_openxr ${openxr_sdk_SOURCE_DIR}/include)
target_link_libraries(harness_layer PRIVATE fov_core fov_vulkan_headers dxgi fov_warnings)
file(GENERATE OUTPUT $<TARGET_FILE_DIR:harness_layer>/fov_harness_layer.json CONTENT [[{
    "file_format_version": "1.0.0",
    "api_layer": {
        "name": "XR_APILAYER_FOVEATEDVR_harness",
        "library_path": "./fov_harness_layer.dll",
        "api_version": "1.1",
        "implementation_version": "1",
        "description": "Test only: names a GPU under the SteamVR null driver"
    }
}
]])

add_executable(fov_probe tools/probe.cpp)
target_link_libraries(fov_probe PRIVATE gaze gfx_d3d11_nvapi gfx_d3d12 gfx_vulkan dxgi fov_warnings)

add_executable(fov_core_test test/core_test.cpp)
target_link_libraries(fov_core_test PRIVATE gaze gfx_d3d12 gfx_vulkan fov_warnings)
add_test(NAME core COMMAND fov_core_test)

# The raw VRS mechanism on this PC's GPU (not in CI: needs an NVIDIA GPU).
add_executable(fov_vrs_test test/vrs_test.cpp)
target_link_libraries(fov_vrs_test PRIVATE fov_nvapi d3d11 d3dcompiler)
