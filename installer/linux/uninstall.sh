#!/bin/sh
# Removes everything install.sh and the app created: every game's original openvr_api.dll back, our layer out of
# every Proton prefix's registry, then the program, menu entry, settings, logs and the shared memory file.
set -eu
data=${XDG_DATA_HOME:-$HOME/.local/share}
dest=$data/foveatedvr

pkill -x foveatedvr 2>/dev/null || true
if [ -x "$dest/foveatedvr" ] && ! "$dest/foveatedvr" --uninstall; then
    echo "Could not undo every game's changes; nothing else was removed. Close running games and try again."
    exit 1
fi
rm -rf "$dest" "${XDG_CONFIG_HOME:-$HOME/.config}/foveatedvr" "${XDG_STATE_HOME:-$HOME/.local/state}/foveatedvr"
rm -f "$HOME/.local/bin/foveatedvr" "$data/applications/foveatedvr.desktop" \
    "$data/icons/hicolor/256x256/apps/foveatedvr.png" /dev/shm/FoveatedVR.v1
echo "FoveatedVR removed."
