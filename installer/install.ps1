# Copyright (C) 2026 EncidXVII
# SPDX-License-Identifier: GPL-3.0-or-later

# Per-user install, no admin: copies the app to %LOCALAPPDATA%\Programs\FoveaXR, registers the OpenXR layer for
# this user, adds Start menu entries and starts the app. Run from the unzipped release folder (or Install.cmd).
$ErrorActionPreference = "Stop"
$source = $PSScriptRoot
$target = Join-Path $env:LOCALAPPDATA "Programs\FoveaXR"
$menu = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"

Get-Process FoveaXR -ErrorAction SilentlyContinue | Stop-Process -Force
New-Item -ItemType Directory -Force $target | Out-Null
Copy-Item -Recurse -Force (Join-Path $source "*") $target
& (Join-Path $target "register-layer.ps1") -Manifest (Join-Path $target "fov_openxr_layer.json")

$shell = New-Object -ComObject WScript.Shell
$app = $shell.CreateShortcut((Join-Path $menu "FoveaXR.lnk"))
$app.TargetPath = Join-Path $target "FoveaXR.exe"
$app.WorkingDirectory = $target
$app.Save()
$remove = $shell.CreateShortcut((Join-Path $menu "Uninstall FoveaXR.lnk"))
$remove.TargetPath = "powershell.exe"
$remove.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $target 'uninstall.ps1')`""
$remove.Save()

Start-Process (Join-Path $target "FoveaXR.exe")
Write-Host "FoveaXR installed to $target. OpenXR games are foveated from their next start; enable OpenVR games in the app's Games tab."
