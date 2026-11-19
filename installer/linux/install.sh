#!/bin/sh
# Per-user install, no root: the app and the Windows DLLs games load under Proton go to
# ~/.local/share/foveatedvr, with ~/.local/bin/foveatedvr, a menu entry and its icon. Run from the unpacked tarball.
set -eu
src=$(cd "$(dirname "$0")" && pwd)
data=${XDG_DATA_HOME:-$HOME/.local/share}
dest=$data/foveatedvr

pkill -x foveatedvr 2>/dev/null || true
mkdir -p "$dest" "$HOME/.local/bin" "$data/applications" "$data/icons/hicolor/256x256/apps"
cp -r "$src/foveatedvr" "$src/fov_openxr_layer.dll" "$src/fov_openxr_layer.json" "$src/openvr_proxy" \
    "$src/profiles" "$src/uninstall.sh" "$src/README.md" "$src/LICENSE" "$src/THIRD_PARTY_NOTICES.md" "$dest/"
ln -sf "$dest/foveatedvr" "$HOME/.local/bin/foveatedvr"
cp "$src/foveatedvr.png" "$data/icons/hicolor/256x256/apps/foveatedvr.png"
sed "s|@EXEC@|$dest/foveatedvr|" "$src/foveatedvr.desktop" > "$data/applications/foveatedvr.desktop"

nohup "$dest/foveatedvr" >/dev/null 2>&1 &
echo "FoveatedVR installed to $dest. Open its Games tab and enable each Proton game (close the game first)."
