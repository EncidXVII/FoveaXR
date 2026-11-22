# Contributing to FoveaXR

Thanks for helping. The most useful contributions right now, in order:

1. **Game reports.** Play a game for a few minutes with FoveaXR on, press Ctrl+F9 twice to measure, and open an
   issue with the game, its graphics API, your GPU and headset, the saving the app shows, and the logs zip
   (Settings > Export logs). Reports of games where nothing is saved are as useful as the good ones.
2. **Headsets with eye tracking** and **Linux with Proton**: both are experimental and require more testing.
3. **Profiles**: games with anti-cheat or their own foveated rendering that the lists in `profiles/` miss.
4. **Code**: start with an issue, so the approach is agreed before you write it.

## Build and test

Windows 11 x64, Visual Studio 2022 or newer (C++ and MASM), CMake 3.24+, Rust stable, Python 3.13.

```bash
cmake -S . -B build
```

```bash
cmake --build build --config Release --parallel
```

```bash
cargo build --release -p fov-app
```

Before a pull request, run what CI runs: `ctest --test-dir build -C Release`, `cargo fmt --all --check`,
`cargo clippy --workspace --all-targets -- -D warnings`, `cargo test --workspace`,
`python -m unittest discover -s harness/tests`. With SteamVR installed and not running, `python harness/check.py`
runs the fixtures under SteamVR's null driver and puts your SteamVR settings back.

The README's pictures come from the real UI: `cargo run -p fov-ui --example qa -- docs/img`.

## Rules for the code that runs inside games

- Any failure leaves the game untouched: no hook, no rate image, plain pass-through.
- Never active in anti-cheat games, VRChat or SteamVR's own processes (`native/core/safety.cpp`).
- Nothing per frame that allocates, logs or blocks. The log gets one line per state change.
- This repository is written from public API documentation only. Do not copy code from other foveation tools, and do
  not name them in code, comments or documents.

## Rights in what you contribute

FoveaXR is the work of [EncidXVII](https://github.com/EncidXVII), published under GPL-3.0-or-later with the terms in [NOTICE](NOTICE). So
that it stays one work with one owner who can answer for all of it:

- You keep the copyright in what you write.
- By opening a pull request you license your contribution to the project under GPL-3.0-or-later, and you give
  EncidXVII a perpetual, worldwide, royalty-free right to use, change and relicense it as part of FoveaXR (for
  example to move to a later license version or to publish in a store that needs other terms).
- You confirm that you wrote it or have the right to submit it, as in the
  [Developer Certificate of Origin 1.1](https://developercertificate.org/): add `Signed-off-by: Your Name <email>`
  to your commits (`git commit -s`).
- Contributors are named in the changelog and the release notes.
