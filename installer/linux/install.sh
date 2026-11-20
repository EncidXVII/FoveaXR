#!/bin/sh
# Copyright (C) 2026 EncidXVII
# SPDX-License-Identifier: GPL-3.0-or-later

# Per-user install, no root: the app and the Windows DLLs games load under Proton go to
# ~/.local/share/foveaxr, with ~/.local/bin/foveaxr, a menu entry and its icon. Run from the unpacked tarball.
set -eu
src=$(cd "$(dirname "$0")" && pwd)
data=${XDG_DATA_HOME:-$HOME/.local/share}
dest=$data/foveaxr

pkill -x foveaxr 2>/dev/null || true
mkdir -p "$dest" "$HOME/.local/bin" "$data/applications" "$data/icons/hicolor/256x256/apps"
cp -r "$src/foveaxr" "$src/fov_openxr_layer.dll" "$src/fov_openxr_layer.json" "$src/openvr_proxy" \
    "$src/profiles" "$src/uninstall.sh" "$src/README.md" "$src/LICENSE" "$src/NOTICE" "$src/THIRD_PARTY_NOTICES.md" "$dest/"
ln -sf "$dest/foveaxr" "$HOME/.local/bin/foveaxr"
cp "$src/foveaxr.png" "$data/icons/hicolor/256x256/apps/foveaxr.png"
sed "s|@EXEC@|$dest/foveaxr|" "$src/foveaxr.desktop" > "$data/applications/foveaxr.desktop"

nohup "$dest/foveaxr" >/dev/null 2>&1 &
echo "FoveaXR installed to $dest. Open its Games tab and enable each Proton game (close the game first)."
