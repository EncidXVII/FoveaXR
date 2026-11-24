# Copyright (C) 2026 EncidXVII
# SPDX-License-Identifier: GPL-3.0-or-later

# FoveaXR for Nix and NixOS, from the published Linux release (the same files as the tarball on GitHub Releases).
#   nix run github:EncidXVII/FoveaXR
# Each enabled Proton game is pointed at the Windows DLLs that sit next to the app, and a Nix store path changes with
# every update. So the `foveaxr` command keeps a per-user copy in ~/.local/share/foveaxr (the folder install.sh uses),
# refreshes it when the package changes, and runs that copy.
{
  description = "FoveaXR: foveated rendering for Steam VR games under Proton";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";

  outputs =
    { self, nixpkgs }:
    let
      version = "0.3.0";
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      meta = {
        description = "Foveated rendering for Steam VR games under Proton";
        homepage = "https://everyvr.net/foveaxr";
        license = pkgs.lib.licenses.gpl3Plus;
        sourceProvenance = [ pkgs.lib.sourceTypes.binaryNativeCode ];
        platforms = [ system ];
        mainProgram = "foveaxr";
      };
      # The release as published, with the app's libraries pointed at the Nix store. The window system libraries
      # are loaded at run time, so they go on the library path too. The Windows DLLs are for games under Proton
      # and stay as they are.
      app = pkgs.stdenv.mkDerivation {
        pname = "foveaxr-app";
        inherit version meta;
        src = pkgs.fetchurl {
          url = "https://github.com/EncidXVII/FoveaXR/releases/download/v${version}/FoveaXR-${version}-linux-x86_64.tar.gz";
          hash = "sha256-rynIZSBkLNEfsw+yU+iXGZ9obSF0os6mmErEeBenvTc=";
        };
        nativeBuildInputs = [ pkgs.autoPatchelfHook ];
        buildInputs = [
          pkgs.stdenv.cc.cc.lib
          pkgs.fontconfig
        ];
        runtimeDependencies = [
          pkgs.libxkbcommon
          pkgs.wayland
          pkgs.libGL
          (pkgs.libx11 or pkgs.xorg.libX11)
          (pkgs.libxcursor or pkgs.xorg.libXcursor)
          (pkgs.libxi or pkgs.xorg.libXi)
        ];
        dontConfigure = true;
        dontBuild = true;
        dontStrip = true;
        installPhase = ''
          runHook preInstall
          mkdir -p $out/share/foveaxr
          cp -r foveaxr fov_openxr_layer.dll fov_openxr_layer.json openvr_proxy profiles uninstall.sh \
            README.md LICENSE NOTICE THIRD_PARTY_NOTICES.md $out/share/foveaxr/
          install -Dm644 foveaxr.png $out/share/icons/hicolor/256x256/apps/foveaxr.png
          sed 's|@EXEC@|foveaxr|' foveaxr.desktop > foveaxr.desktop.out
          install -Dm644 foveaxr.desktop.out $out/share/applications/foveaxr.desktop
          runHook postInstall
        '';
      };
      launcher = pkgs.writeShellApplication {
        name = "foveaxr";
        runtimeInputs = [ pkgs.coreutils ];
        inherit meta;
        text = ''
          dest="''${XDG_DATA_HOME:-$HOME/.local/share}/foveaxr"
          if [ "$(cat "$dest/.nix-package" 2>/dev/null || true)" != "${app}" ]; then
            mkdir -p "$dest"
            cp -R --remove-destination "${app}/share/foveaxr/." "$dest/"
            chmod -R u+w "$dest"
            echo "${app}" > "$dest/.nix-package"
          fi
          # The copy's libraries are in the Nix store: a GC root keeps them while the copy is there, or a
          # garbage collection after `nix run` would break the copy.
          if command -v nix-store >/dev/null; then
            nix-store --add-root "$dest/.nix-gcroot" --realise "${app}" >/dev/null 2>&1 || true
          fi
          exec "$dest/foveaxr" "$@"
        '';
      };
      # The command plus the menu entry and icon.
      foveaxr = pkgs.runCommand "foveaxr-${version}" { inherit meta; } ''
        mkdir -p $out/share
        ln -s ${launcher}/bin $out/bin
        ln -s ${app}/share/applications ${app}/share/icons $out/share/
      '';
    in
    {
      packages.${system} = {
        inherit app foveaxr;
        default = foveaxr;
      };
    };
}
