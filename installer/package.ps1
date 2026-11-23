# Copyright (C) 2026 EncidXVII
# SPDX-License-Identifier: GPL-3.0-or-later

# Builds the release zip: dist\FoveaXR-<version>.zip and its .sha256.
#   installer\package.ps1
# Compress-Archive in Windows PowerShell 5.1 names entries "dir\file", which Linux tools and some extractors read
# as one file with a backslash in its name. This writes "dir/file", as the zip format specifies. Like
# Compress-Archive, a folder goes in under its own name and a file at the top.
function New-Zip([string]$Zip, [string[]]$Paths) {
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    if (Test-Path $Zip) { Remove-Item $Zip }
    $archive = [IO.Compression.ZipFile]::Open($Zip, 'Create')
    try {
        foreach ($path in $Paths) {
            $item = Get-Item $path
            $base = Split-Path $item.FullName -Parent
            $files = if ($item.PSIsContainer) { Get-ChildItem $item.FullName -Recurse -File } else { @($item) }
            foreach ($file in $files) {
                $name = $file.FullName.Substring($base.Length + 1).Replace('\', '/')
                [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $name, 'Optimal')
            }
            # Empty folders too (a mod loader may expect one to exist), as "dir/" entries.
            if ($item.PSIsContainer) {
                foreach ($dir in Get-ChildItem $item.FullName -Recurse -Directory | Where-Object { -not (Get-ChildItem $_.FullName -Force) }) {
                    [void]$archive.CreateEntry($dir.FullName.Substring($base.Length + 1).Replace('\', '/') + '/')
                }
            }
        }
    } finally { $archive.Dispose() }
}
$root = Split-Path $PSScriptRoot
$version = (Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern 'project\(FoveaXR VERSION ([0-9.]+)').Matches[0].Groups[1].Value
cmake -S $root -B (Join-Path $root "build") | Out-Null
cmake --build (Join-Path $root "build") --config Release --target layer_openxr hook_openvr --parallel
if ($LASTEXITCODE) { throw "native build failed" }
cargo build --release -p fov-app --manifest-path (Join-Path $root "Cargo.toml")
if ($LASTEXITCODE) { throw "app build failed" }
# After the builds: Windows PowerShell turns any stderr line of a native tool (cargo's progress) into an error.
$ErrorActionPreference = "Stop"

$name = "FoveaXR-$version"
$dist = Join-Path $root "dist"
# Staged under build\package, so a copy someone runs from dist (its exe locked) never blocks a rebuild.
$out = Join-Path $root "build\package\$name"
New-Item -ItemType Directory -Force $dist | Out-Null
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force "$out\openvr_proxy", "$out\profiles" | Out-Null
$bin = Join-Path $root "build\Release"
Copy-Item (Join-Path $root "target\release\FoveaXR.exe") $out
Copy-Item "$bin\fov_openxr_layer.dll", "$bin\fov_openxr_layer.json" $out
Copy-Item "$bin\openvr_proxy\openvr_api.dll" "$out\openvr_proxy"
Copy-Item (Join-Path $root "profiles\*.toml") "$out\profiles"
Copy-Item (Join-Path $PSScriptRoot "install.ps1"), (Join-Path $PSScriptRoot "uninstall.ps1"), (Join-Path $PSScriptRoot "register-layer.ps1") $out
Copy-Item (Join-Path $root "README.md"), (Join-Path $root "LICENSE"), (Join-Path $root "NOTICE"), (Join-Path $root "THIRD_PARTY_NOTICES.md") $out
Set-Content -Encoding ascii "$out\Install.cmd" "@powershell -NoProfile -ExecutionPolicy Bypass -File `"%~dp0install.ps1`"`r`n@pause"

$zip = Join-Path $dist "$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
New-Zip $zip @($out)
$hash = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
# LF, not CRLF: `sha256sum -c` on Linux reads a CR as part of the file name.
[IO.File]::WriteAllText("$zip.sha256", "$hash  $name.zip`n")
Write-Host "$zip`n$hash"
