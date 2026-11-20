# Copyright (C) 2026 EncidXVII
# SPDX-License-Identifier: GPL-3.0-or-later

# Fixture apps for the harness: they render like a game (eye pass with depth, then a UI pass without) so the gates
# can check what foveation does to each pass. Third-party pieces pinned by hash, see THIRD_PARTY_NOTICES.md.
FetchContent_Declare(volk
    URL https://github.com/zeux/volk/archive/refs/tags/vulkan-sdk-1.4.363.0.zip
    URL_HASH SHA256=889b90fdcbe9cee7b118339d640c6895b7be5c920d309c44b744692063275b3d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR none)
FetchContent_Declare(dxc
    URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip
    URL_HASH SHA256=ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR none)
FetchContent_MakeAvailable(volk dxc)

# scene.hlsl: embedded as source for the D3D fixtures (compiled at run time) and as SPIR-V for Vulkan.
set(gen ${CMAKE_CURRENT_BINARY_DIR}/gen)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS common/scene.hlsl)
file(READ common/scene.hlsl hlsl)
file(CONFIGURE OUTPUT ${gen}/scene_hlsl.h
    CONTENT "#pragma once\nconstexpr char kSceneHlsl[] = R\"hlsl(${hlsl})hlsl\";\n" @ONLY)
set(spirv)
foreach(shader "vs_main;vs_6_0;kVsMain" "ps_scene;ps_6_0;kPsScene" "ps_ui;ps_6_0;kPsUi")
    list(GET shader 0 entry)
    list(GET shader 1 profile)
    list(GET shader 2 name)
    # D3D's clip space is y-up, Vulkan's y-down: flipped in the vertex shader so both draw the same image.
    set(flip)
    if(profile MATCHES "^vs")
        set(flip -fvk-invert-y)
    endif()
    add_custom_command(OUTPUT ${gen}/${entry}.spv.h
        COMMAND ${dxc_SOURCE_DIR}/bin/x64/dxc.exe -spirv ${flip} -fspv-target-env=vulkan1.1 -T ${profile}
            -E ${entry} -Vn ${name} -Fh ${gen}/${entry}.spv.h ${CMAKE_CURRENT_SOURCE_DIR}/common/scene.hlsl
        DEPENDS common/scene.hlsl VERBATIM)
    list(APPEND spirv ${gen}/${entry}.spv.h)
endforeach()

add_library(fixture_d3d11 STATIC common/d3d11_scene.cpp)
target_include_directories(fixture_d3d11 PUBLIC common ${gen})
target_link_libraries(fixture_d3d11 PUBLIC d3d11 dxgi d3dcompiler PRIVATE fov_warnings)

add_executable(fov_fixture_openvr_d3d11 openvr_d3d11/main.cpp)
target_include_directories(fov_fixture_openvr_d3d11 PRIVATE ${openvr_dir}/headers)
target_link_libraries(fov_fixture_openvr_d3d11 PRIVATE fixture_d3d11 ${openvr_dir}/lib/win64/openvr_api.lib fov_warnings)
add_custom_command(TARGET fov_fixture_openvr_d3d11 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${openvr_dir}/bin/win64/openvr_api.dll
        $<TARGET_FILE_DIR:fov_fixture_openvr_d3d11>)

add_library(fixture_volk STATIC ${volk_SOURCE_DIR}/volk.c)
target_include_directories(fixture_volk PUBLIC ${volk_SOURCE_DIR})
target_link_libraries(fixture_volk PUBLIC fov_vulkan_headers)

add_executable(fov_fixture_xr openxr/main.cpp openxr/xr_d3d11.cpp openxr/xr_d3d12.cpp openxr/xr_vulkan.cpp ${spirv})
target_include_directories(fov_fixture_xr PRIVATE ${openxr_sdk_SOURCE_DIR}/include)
target_link_libraries(fov_fixture_xr PRIVATE fixture_d3d11 fixture_volk d3d12
    ${openxr_sdk_SOURCE_DIR}/x64/lib/openxr_loader.lib fov_warnings)
add_custom_command(TARGET fov_fixture_xr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${openxr_sdk_SOURCE_DIR}/x64/bin/openxr_loader.dll
        $<TARGET_FILE_DIR:fov_fixture_xr>)
