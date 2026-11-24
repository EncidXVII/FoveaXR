#!/bin/sh
# Copyright (C) 2026 EncidXVII
# SPDX-License-Identifier: GPL-3.0-or-later

# Removes everything install.sh and the app created: every game's original openvr_api.dll back, our layer out of
# every Proton prefix's registry, then the program, menu entry, settings, logs and the shared memory file. Flatpak
# and Snap Steam's games keep theirs in the sandbox's own home: the copy of the layer, logs and shared memory file.
set -eu
data=${XDG_DATA_HOME:-$HOME/.local/share}
dest=$data/foveaxr

pkill -x foveaxr 2>/dev/null || true
if [ -x "$dest/foveaxr" ] && ! "$dest/foveaxr" --uninstall; then
    echo "Could not undo every game's changes; nothing else was removed. Close running games and try again."
    exit 1
fi
rm -rf "$dest" "${XDG_CONFIG_HOME:-$HOME/.config}/foveaxr" "${XDG_STATE_HOME:-$HOME/.local/state}/foveaxr"
rm -f "$HOME/.local/bin/foveaxr" "$data/applications/foveaxr.desktop" \
    "$data/icons/hicolor/256x256/apps/foveaxr.png" /dev/shm/FoveaXR.v1
for sandbox in "$HOME/.var/app/com.valvesoftware.Steam" "$HOME/snap/steam/common"; do
    rm -rf "$sandbox/.local/share/foveaxr" "$sandbox/.local/state/foveaxr"
done
echo "FoveaXR removed."
