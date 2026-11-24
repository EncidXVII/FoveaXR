# Changelog

## Unreleased

- Linux: a game added by hand that Flatpak or Snap Steam runs (as a non-Steam game) gets the copy of FoveaXR's files
  in that sandbox's folder when enabled, as its Steam games do, so its proxy finds them. It counts as that Steam's
  when it is in the sandbox's folder, or when that Steam is the only one on the PC.
- Linux: user.reg, its backup and the layer records are written whole or not at all.

## 0.4.0 - 2026-11-24

- Linux: a game that ships both OpenVR and OpenXR gets our OpenXR layer in its Proton prefix too when enabled, so it
  is foveated whichever of the two it renders through.
- Linux: Flatpak and Snap Steam games are listed and can be enabled (Experimental). Library paths Flatpak Steam writes
  as its sandbox sees them are read as the host sees them. Enabling copies the OpenXR layer and the profiles into the
  sandbox's own folder, which its games can see; the copy is updated with the app and removed with the last game
  there, and by the uninstaller. Those games share settings and stats with the app through a file in that folder,
  since their /dev/shm is not the host's, and their logs are in Export logs. A running game is never edited, whether
  its prefix is named as the host or the sandbox sees it.
- Linux: the empty Games page no longer says "this Windows user", or that OpenXR games are foveated without being
  listed (on Linux they are enabled per game).
- Nix: the installed copy is kept safe from garbage collection.
- README: the status says 0.4; the OpenXR layer covers every game on Windows and goes per game on Linux; Monado and
  WiVRn setup (xrizer or OpenComposite).

## 0.3.0 - 2026-11-24

- **Add game** on the Games page: pick the folder of a VR game FoveaXR did not find, from another store or a copy
  outside Steam. It is listed like the others, and its row says it was added by hand and that FoveaXR is for
  single-player games only. **Remove from list** in its row takes it off again; the game itself is not touched.
- On Linux, Add game takes OpenVR games: an OpenXR game outside Steam has no Proton prefix FoveaXR can find.

## 0.2.1 - 2026-11-23

- About lists the maker's profiles as GitHub, X, Threads, with X at its new address (x.com/EncidXVII).
- Linux packaging: a Nix flake (`nix run github:EncidXVII/FoveaXR`), and an Arch Linux PKGBUILD in `packaging/aur`,
  both built from the release files.

## 0.2.0 - 2026-11-23

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
- Linux with Proton, experimental: Direct3D 12 games.
