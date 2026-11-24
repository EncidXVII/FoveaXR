# Copyright (C) 2026 EncidXVII
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.24)
project(FoveaXR VERSION 0.3.0 LANGUAGES C CXX ASM_MASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: games do not reliably ship the VC++ redistributable our DLLs would need inside them.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Everything that runs lands in build/<Config>: layer DLL + manifest, probe, tests, fixtures and their loaders.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>)

option(FOV_FIXTURES "Build the fixture apps" ON)

include(native/cmake/deps.cmake)
enable_testing()
add_subdirectory(native)
if(FOV_FIXTURES)
    add_subdirectory(fixtures)
endif()
