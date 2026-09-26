# Copyright (c) 2026 CS-Fusion. All Rights Reserved.
#
# v2.0 phase 5: installs NVIDIA DLSS into Plugins/ from the plugin archive the
# owner downloaded from developer.nvidia.com (NVIDIA RTX SDK EULA: not for this
# public repository - Plugins/DLSS*/ and Plugins/Streamline*/ are git-ignored).
# Downloads nothing. Only DLSS Super Resolution and what it needs are taken:
#   Plugins/DLSS                 DLSS-SR, DLAA, Ray Reconstruction
#   Plugins/StreamlineNGXCommon  shared NGX loader DLSS depends on
# Frame Generation (Streamline DLSS-G) needs an RTX 40 or newer, Reflex is a
# separate plugin; neither is installed.
#
#   powershell -ExecutionPolicy Bypass -File Scripts\install_dlss.ps1
#   powershell -ExecutionPolicy Bypass -File Scripts\install_dlss.ps1 -Zip D:\UE5.8_DLSS4.5Plugin_v8.8.0.zip
#
# Without the plugin the game builds and runs on TSR/TAA and hides DLSS
# (docs/GRAPHICS.md). Rebuild after installing it.

param(
    [string]$Zip = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if (-not $Zip) {
    $Zip = Get-ChildItem (Join-Path $Root "SourceArt\_download") -Filter "UE5.8_DLSS*Plugin*.zip" -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Zip -or -not (Test-Path $Zip)) {
    throw "DLSS plugin archive not found. Download 'DLSS for Unreal Engine 5.8' from https://developer.nvidia.com/rtx/dlss/get-started and pass -Zip <path>."
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$Archive = [IO.Compression.ZipFile]::OpenRead($Zip)
try {
    $Count = 0
    foreach ($Entry in $Archive.Entries) {
        if ($Entry.FullName -match '^Plugins/(DLSS|StreamlineNGXCommon)/' -and $Entry.Name) {
            $Dest = Join-Path $Root ($Entry.FullName -replace '/', '\')
            New-Item -ItemType Directory -Force (Split-Path $Dest) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($Entry, $Dest, $true)
            $Count++
        }
    }
} finally {
    $Archive.Dispose()
}
$Descriptor = Get-Content (Join-Path $Root "Plugins\DLSS\DLSS.uplugin") -Raw | ConvertFrom-Json
Write-Host "Installed DLSS $($Descriptor.VersionName) for Unreal $($Descriptor.EngineVersion): $Count files in Plugins\DLSS and Plugins\StreamlineNGXCommon."
Write-Host "Rebuild the project; the settings screen then offers NVIDIA DLSS on RTX GPUs."
