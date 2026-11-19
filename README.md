# FoveatedVR (working name)

Free foveated rendering for your Steam VR library. Fixed foveation for every headset, eye-tracked for Steam Frame and
other eye-tracking headsets.

**Status: pre-alpha, foundation only.** Nothing changes how games render yet. This is the build, test and harness
setup the foveation work is built on.

## What it will do

- Lower the GPU cost of PC VR games by shading the edge of each eye's view more coarsely than the centre, where you
  look. Variable rate shading on D3D11 (NVIDIA), D3D12 (VRS Tier 2) and Vulkan (fragment shading rate).
- OpenXR games through an API layer, OpenVR games through a per-game `openvr_api.dll`.
- Never in anti-cheat games, never in games that already foveate themselves.

## Layout

| Folder | What |
|---|---|
| `native/` | C++20 core (`fov_core`, `gaze`), graphics backends (`gfx_d3d11_nvapi`, `gfx_d3d12`, `gfx_vulkan`), the OpenXR layer (`layer_openxr`), the OpenVR proxy (`hook_openvr`), tools and tests |
| `app/` | Rust + Slint tray app (`fov-app`), its views (`fov-ui`) and the shared-memory mirror (`fov-shm`) |
| `fixtures/` | Test apps that render like games: OpenVR D3D11, OpenXR D3D11 / D3D12 / Vulkan |
| `harness/` | Python harness: SteamVR null-driver sessions, the test gaze source, fixture runs |
| `profiles/` | Per-game data: anti-cheat block list, native-foveation list |
| `installer/` | Per-user install pieces (OpenXR layer registration) |

## Build

Windows 11 x64, Visual Studio 2022 or newer (C++ and MASM), CMake 3.24+, Rust stable, Python 3.13.

```bash
cmake -S . -B build
```

```bash
cmake --build build --config Release --parallel
```

```bash
ctest --test-dir build -C Release --output-on-failure
```

```bash
cargo test --workspace
```

```bash
python -m unittest discover -s harness/tests
```

## Run a fixture in a headless SteamVR session

Needs SteamVR installed and not running. The harness swaps in the null driver and puts your SteamVR settings back
afterwards.

```bash
python harness/run.py xr-d3d11 --layer --secondary
```

Fixtures: `openvr-d3d11`, `xr-d3d11`, `xr-d3d12`, `xr-vulkan`, `hello_xr`. `--layer` loads the OpenXR layer from
the build folder, `--proxy` runs an OpenVR fixture through the `openvr_api.dll` proxy, `--adapter 1` picks the second
GPU, `--gaze 0.5,0.5` feeds the test gaze source.

## License

GPL-3.0-or-later, see `LICENSE`. Third-party code and its licenses: `THIRD_PARTY_NOTICES.md`.
