# Third-party notices

Fetched at build time, pinned by SHA-256 (`native/cmake/deps.cmake`, `fixtures/CMakeLists.txt`). Nothing below is
committed to this repository except the fonts.

## Shipped with or linked into the product

- **NVAPI** (NVIDIA/nvapi, commit 51e5aae): headers and `nvapi64.lib`, linked into the D3D11 path. MIT License,
  Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. Text: `License.txt` in the fetched source.
- **OpenXR SDK** (KhronosGroup/OpenXR-SDK-Source release 1.1.65): headers for the API layer. Apache License 2.0,
  Copyright (c) The Khronos Group Inc.
- **Vulkan-Headers** (KhronosGroup/Vulkan-Headers vulkan-sdk-1.4.363.0): headers for the Vulkan path. Apache License
  2.0 or MIT, Copyright (c) The Khronos Group Inc.
- **Barlow** font (`app/crates/ui/fonts/Barlow-Regular.ttf`, `Barlow-SemiBold.ttf`), Copyright 2017 The Barlow Project
  Authors. SIL Open Font License 1.1, text in `app/crates/ui/fonts/Barlow-OFL.txt`.
- **Rust crates** compiled into the tray app: licenses checked by `cargo deny` against the allow list in `deny.toml`;
  the full table is generated for each release.

## Test fixtures and tools only (never shipped)

- **OpenXR loader and hello_xr** (OpenXR SDK release 1.1.65, prebuilt): the fixtures' loader and the extra
  `hello_xr` fixture. Apache License 2.0.
- **OpenVR SDK** (ValveSoftware/openvr v2.15.6): `openvr.h`, `openvr_api.lib`, `openvr_api.dll` for the OpenVR fixture.
  BSD 3-Clause License, Copyright (c) 2015 Valve Corporation.
- **volk** (zeux/volk vulkan-sdk-1.4.363.0): Vulkan loader for the Vulkan fixture. MIT License, Copyright (c)
  2018-2025 Arseny Kapoulkine.
- **DirectX Shader Compiler** (microsoft/DirectXShaderCompiler v1.9.2609): compiles the fixture shader to SPIR-V at
  build time. University of Illinois/NCSA Open Source License and Apache License 2.0 with LLVM exceptions.
