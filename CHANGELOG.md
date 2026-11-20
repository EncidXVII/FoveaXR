# Changelog

## 1.0.0 (unreleased)

First release.

- Foveated rendering for OpenXR games (an API layer, no per-game setup) and OpenVR games (one Enable click per game).
- Direct3D 12 on NVIDIA RTX 20 and newer, AMD RX 6000 and newer and Intel Arc; Direct3D 11 on NVIDIA RTX 20 and
  newer.
- Fixed mode for every headset; eye-tracked mode where the headset shares eye tracking with PC games.
- Presets Low, Medium and High; Ctrl+F9 switches foveation off and on in VR to compare, Ctrl+F10 cycles the mode.
- Tray app: on / off, mode, preset, the GPU time saved in the running game, your Steam VR games with their state,
  eight accent themes, a debug tint that shows the rings, Export logs.
- Never active in games with anti-cheat, in VRChat or in games with their own foveated rendering.
- Per-user install and uninstall without admin rights; uninstall puts every game's `openvr_api.dll` back.
- Linux with Proton, experimental: Direct3D 12 games, not yet tested in a headset.
