# Copyright (C) 2026 EncidXVII
# SPDX-License-Identifier: GPL-3.0-or-later

# Registers the OpenXR API layer for the current user (no admin), or removes it with -Remove. The installer runs
# this on install and uninstall; developers run it against build\Release to try the layer with a real headset.
#   installer\register-layer.ps1 -Manifest build\Release\fov_openxr_layer.json [-Remove]
param([Parameter(Mandatory)][string]$Manifest, [switch]$Remove)
$key = "HKCU:\Software\Khronos\OpenXR\1\ApiLayers\Implicit"
$path = (Resolve-Path $Manifest).Path
if ($Remove) {
    if (Test-Path $key) { Remove-ItemProperty -Path $key -Name $path -ErrorAction SilentlyContinue }
    # Leave the registry as it was: the keys registering created go too once they are empty.
    for ($k = $key; $k -ne "HKCU:\Software"; $k = Split-Path $k) {
        if (-not (Test-Path $k)) { continue }
        if ((Get-Item $k).ValueCount -or (Get-ChildItem $k)) { break }
        Remove-Item $k
    }
    Write-Host "OpenXR layer unregistered: $path"
} else {
    New-Item -Path $key -Force | Out-Null
    # The loader's convention: value name = manifest path, DWORD 0 = enabled.
    New-ItemProperty -Path $key -Name $path -PropertyType DWord -Value 0 -Force | Out-Null
    Write-Host "OpenXR layer registered: $path"
}
