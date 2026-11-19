# FoveatedVR (working name)

Free foveated rendering for your Steam VR library. Fixed foveation for every headset, eye-tracked for Steam Frame and
other eye-tracking headsets.

The edge of each eye's view is shaded more coarsely than the centre, where you look, so the GPU does less work per
frame. On the test scene (a pixel-heavy fixture) eye rendering took 1.37 ms without and 0.25 ms with the High preset.
Real games save less: how much depends on how much of their frame is pixel shading. Measure your own with Ctrl+F9.

## Install

1. Download `FoveatedVR-1.0.0.zip` from Releases, check it against `FoveatedVR-1.0.0.zip.sha256`, unzip.
2. Run `Install.cmd`. No admin rights: it installs for your Windows user only and starts the app (tray icon).
3. OpenXR games are foveated from their next start. OpenVR games: open the app's **Games** tab and press **Enable**.

Uninstall: Start menu → **Uninstall FoveatedVR**. It puts every game's original `openvr_api.dll` back and removes the
OpenXR layer, settings and logs.

## Use

- **On / Off**, **Mode** (Fixed, or Eye-tracked where the headset shares eye tracking), **Preset** (Low, Medium, High).
  Changes apply to running games at once.
- **Ctrl+F9** in VR (keyboard): foveation off / on, to compare. The app then shows the GPU time saved on eye
  rendering. **Ctrl+F10**: Off → Fixed → Eye-tracked.
- **Debug tint** colours the coarse rings (yellow: half rate, red: quarter rate) so you can see where they are.
- **Export logs** writes `FoveatedVR-logs.zip` to your desktop. Attach it to bug reports.

Eye-tracked mode needs the eye tracker to reach the game: OpenXR games through `XR_EXT_eye_gaze_interaction`, OpenVR
games through SteamVR's eye-tracked foveation centre (SteamVR 2.15+). Without eye data the game falls back to Fixed.

## Supported

| | NVIDIA (Turing / RTX 20 and newer) | AMD (RDNA2 / RX 6000 and newer) | Intel Arc |
|---|---|---|---|
| D3D11 | Yes | No | No |
| D3D12 | Yes | Yes | Yes |
| Vulkan, OpenGL | No | No | No |

VR APIs: OpenXR (API layer, all OpenXR games on any runtime) and OpenVR (per-game, after **Enable**).

## Honest limits

- Never runs in games with anti-cheat (EasyAntiCheat, BattlEye, listed games) or in games that foveate themselves.
  Online games: don't, even if not blocked.
- Detection is by render-target size: games that render the scene at an unusual size, or deferred renderers whose
  lighting pass binds no depth, may get no savings. Games using temporal anti-aliasing can show shimmer at the ring
  edges.
- D3D12 render targets created before the first OpenVR submit are not recognised: some OpenVR D3D12 games save
  nothing.
- A game update replaces our `openvr_api.dll`; the app then shows **Re-enable**.
- Windows may warn about the unsigned app, and antivirus may flag `openvr_api.dll` in game folders (it wraps the
  game's original DLL). Code signing comes later.

## Build

Windows 11 x64, Visual Studio 2022+ (C++ and MASM), CMake 3.24+, Rust stable, Python 3.13 (harness).

```bash
cmake -S . -B build
```

```bash
cmake --build build --config Release --parallel
```

```bash
cargo build --release -p fov-app
```

Release zip: `installer\package.ps1`. Tests: `ctest --test-dir build -C Release`, `cargo test --workspace`,
`python -m unittest discover -s harness/tests`, and with SteamVR installed (not running) the GPU check
`python harness/check.py`, which runs the fixtures under SteamVR's null driver and puts your SteamVR settings back.

| Folder | What |
|---|---|
| `native/` | C++ core, D3D11 (NVAPI) and D3D12 backends, OpenXR layer, OpenVR proxy, tests |
| `app/` | Rust + Slint tray app |
| `fixtures/` | Test apps that render like games |
| `harness/` | SteamVR null-driver runs and the foveation check |
| `profiles/` | Anti-cheat and native-foveation lists |
| `installer/` | Install, uninstall, packaging |

## License

GPL-3.0-or-later, see `LICENSE`. Third-party code and its licenses: `THIRD_PARTY_NOTICES.md`.
