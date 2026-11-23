# Releasing FoveaXR

## The repository page (once)

What search engines and assistants read first is the repository's own page, so it says what the README says:

1. **Name**: `FoveaXR` (GitHub redirects the old address after a rename).
2. **About**:
   - Description: `Free, open-source foveated rendering for any PC VR game: fixed for every headset, eye-tracked (experimental) for Steam Frame and other eye-tracking headsets. OpenXR layer and OpenVR, Direct3D 11 (NVIDIA) and Direct3D 12 (NVIDIA, AMD, Intel).`
   - Topics (20 at most): `vr`, `virtual-reality`, `foveated-rendering`, `fixed-foveated-rendering`, `eye-tracking`,
     `variable-rate-shading`, `vrs`, `openxr`, `openxr-api-layer`, `openvr`, `steamvr`, `vr-performance`,
     `steam-frame`, `valve-index`, `meta-quest`, `directx-12`, `directx-11`, `nvidia`, `rust`, `cpp`.
3. **Settings > General > Social preview**: upload `docs/img/og.png` (the card shown when the link is shared).
4. **Security**: turn on Private vulnerability reporting (`SECURITY.md` sends reports there) and Dependabot alerts.
5. Features: Issues on, Discussions on (Q&A, Show and tell), Wiki off.

## Each release

1. Everything committed, the changelog's top section dated, the version the same in every manifest.
2. The checks in [CONTRIBUTING.md](CONTRIBUTING.md) pass, and the build has been used in a headset.
3. Build the packages with the packaging scripts; each carries `LICENSE`, `NOTICE` and `THIRD_PARTY_NOTICES.md`.
4. Tag `vX.Y.Z` (annotated: `git tag -a vX.Y.Z -m "FoveaXR X.Y.Z"`), publish the release with the changelog section as its notes, the packages and their SHA-256 files.
5. Check the README's download links and the app's update check against the new release.
6. In `flake.nix`, set `version` and each Linux file's `hash` (its `.sha256` in SRI form: `nix hash convert --hash-algo
   sha256 <hex>`), then push: the Nix workflow builds it.
