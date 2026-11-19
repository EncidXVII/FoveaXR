# Per-user install, no admin: copies the app to %LOCALAPPDATA%\Programs\FoveatedVR, registers the OpenXR layer for
# this user, adds Start menu entries and starts the app. Run from the unzipped release folder (or Install.cmd).
$ErrorActionPreference = "Stop"
$source = $PSScriptRoot
$target = Join-Path $env:LOCALAPPDATA "Programs\FoveatedVR"
$menu = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"

Get-Process FoveatedVR -ErrorAction SilentlyContinue | Stop-Process -Force
New-Item -ItemType Directory -Force $target | Out-Null
Copy-Item -Recurse -Force (Join-Path $source "*") $target
& (Join-Path $target "register-layer.ps1") -Manifest (Join-Path $target "fov_openxr_layer.json")

$shell = New-Object -ComObject WScript.Shell
$app = $shell.CreateShortcut((Join-Path $menu "FoveatedVR.lnk"))
$app.TargetPath = Join-Path $target "FoveatedVR.exe"
$app.WorkingDirectory = $target
$app.Save()
$remove = $shell.CreateShortcut((Join-Path $menu "Uninstall FoveatedVR.lnk"))
$remove.TargetPath = "powershell.exe"
$remove.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $target 'uninstall.ps1')`""
$remove.Save()

Start-Process (Join-Path $target "FoveatedVR.exe")
Write-Host "FoveatedVR installed to $target. OpenXR games are foveated from their next start; enable OpenVR games in the app's Games tab."
