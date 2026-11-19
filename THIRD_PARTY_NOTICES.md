# Third-party notices

Fetched at build time, pinned by SHA-256 (`native/cmake/deps.cmake`, `fixtures/CMakeLists.txt`). Nothing below is
committed to this repository except the fonts.

## Shipped with or linked into the product

- **NVAPI** (NVIDIA/nvapi, commit 51e5aae): headers and `nvapi64.lib`, linked into the D3D11 path. MIT License,
  Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. Text: `License.txt` in the fetched source.
- **OpenXR SDK** (KhronosGroup/OpenXR-SDK-Source release 1.1.65): headers for the API layer. Apache License 2.0,
  Copyright (c) The Khronos Group Inc.
- **Vulkan-Headers** (KhronosGroup/Vulkan-Headers vulkan-sdk-1.4.363.0): type declarations the layer's OpenXR headers
  need. Apache License 2.0 or MIT, Copyright (c) The Khronos Group Inc.
- **MinHook** v1.3.4 (TsudaKageyu/minhook): compiled into the layer and the OpenVR proxy. BSD 2-Clause License,
  Copyright (C) 2009-2017 Tsuda Kageyu; Hacker Disassembler Engine Copyright (c) 2008-2009 Vyacheslav Patkov. Full
  text at the end of this file.
- **OpenVR SDK** header (ValveSoftware/openvr v2.15.6): its interface declarations are compiled into the OpenVR proxy.
  BSD 3-Clause License, Copyright (c) 2015 Valve Corporation.
- **Barlow** font (`app/crates/ui/fonts/Barlow-Regular.ttf`, `Barlow-SemiBold.ttf`), Copyright 2017 The Barlow Project
  Authors. SIL Open Font License 1.1, text in `app/crates/ui/fonts/Barlow-OFL.txt`.
- **Rust crates** compiled into the tray app: licenses checked by `cargo deny` against the allow list in `deny.toml`;
  the full table is generated for each release.

## Test fixtures and tools only (never shipped)

- **OpenXR loader and hello_xr** (OpenXR SDK release 1.1.65, prebuilt): the fixtures' loader and the extra
  `hello_xr` fixture. Apache License 2.0.
- **OpenVR SDK** (ValveSoftware/openvr v2.15.6): `openvr_api.lib`, `openvr_api.dll` for the OpenVR fixture.
- **volk** (zeux/volk vulkan-sdk-1.4.363.0): Vulkan loader for the Vulkan fixture. MIT License, Copyright (c)
  2018-2025 Arseny Kapoulkine.
- **DirectX Shader Compiler** (microsoft/DirectXShaderCompiler v1.9.2609): compiles the fixture shader to SPIR-V at
  build time. University of Illinois/NCSA Open Source License and Apache License 2.0 with LLVM exceptions.

## MinHook license

```
MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

================================================================================
Portions of this software are Copyright (c) 2008-2009, Vyacheslav Patkov.
================================================================================
Hacker Disassembler Engine 32 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-------------------------------------------------------------------------------
Hacker Disassembler Engine 64 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
