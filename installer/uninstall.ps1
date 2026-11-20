# Copyright (C) 2026 EncidXVII
# SPDX-License-Identifier: GPL-3.0-or-later

# Removes everything install.ps1 and the app created: every game's original openvr_api.dll back, the OpenXR layer
# registration, Start menu entries, settings, logs and the program folder.
$target = Join-Path $env:LOCALAPPDATA "Programs\FoveaXR"
$menu = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"

Get-Process FoveaXR -ErrorAction SilentlyContinue | Stop-Process -Force
$app = Join-Path $target "FoveaXR.exe"
if (Test-Path $app) {
    $p = Start-Process $app -ArgumentList "--uninstall" -Wait -PassThru
    if ($p.ExitCode) {
        Write-Host "Could not restore every game's openvr_api.dll; nothing else was removed. Close running games and try again."
        exit 1
    }
}
$layer = Join-Path $target "fov_openxr_layer.json"
if (Test-Path $layer) { & (Join-Path $target "register-layer.ps1") -Manifest $layer -Remove }
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $menu "FoveaXR.lnk"), (Join-Path $menu "Uninstall FoveaXR.lnk")
foreach ($dir in (Join-Path $env:APPDATA "FoveaXR"), (Join-Path $env:LOCALAPPDATA "FoveaXR"), $target) {
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
}
Write-Host "FoveaXR removed."
