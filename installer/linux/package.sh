#!/bin/sh
# Builds dist/FoveatedVR-<version>-linux-x86_64.tar.gz and its .sha256 from the Linux app and the Windows DLLs that
# games load under Proton (built on Windows: the layer, its manifest, the OpenVR proxy).
#   installer/linux/package.sh <linux foveatedvr binary> <windows build/Release folder>
set -eu
bin=$1
win=$2
root=$(cd "$(dirname "$0")/../.." && pwd)
version=$(sed -n 's/^project(FoveatedVR VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
name=FoveatedVR-$version-linux-x86_64
out=$root/dist/$name
rm -rf "$out"
mkdir -p "$out/openvr_proxy" "$out/profiles"
install -m 755 "$bin" "$out/foveatedvr"
cp "$win/fov_openxr_layer.dll" "$win/fov_openxr_layer.json" "$out/"
cp "$win/openvr_proxy/openvr_api.dll" "$out/openvr_proxy/"
cp "$root"/profiles/*.toml "$out/profiles/"
cp "$root/packaging/brand/foveaxr-256.png" "$out/foveatedvr.png"
install -m 755 "$root/installer/linux/install.sh" "$root/installer/linux/uninstall.sh" "$out/"
cp "$root/installer/linux/foveatedvr.desktop" "$root/README.md" "$root/LICENSE" "$root/THIRD_PARTY_NOTICES.md" "$out/"
tar -C "$root/dist" -czf "$out.tar.gz" "$name"
(cd "$root/dist" && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256")
echo "$out.tar.gz"
