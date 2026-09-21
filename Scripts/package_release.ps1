# Copyright (c) 2026 CS-Fusion. All Rights Reserved.
#
# Stage 8 packaging. Builds, cooks and stages a Windows build, then zips it.
#
#   powershell -ExecutionPolicy Bypass -File Scripts\package_release.ps1 -Version 0.8.0
#   powershell -ExecutionPolicy Bypass -File Scripts\package_release.ps1 -Version 0.8.0 -Config Development
#
# Output:
#   Build\<Config>\Windows\CSFusion.exe            staged build (run this)
#   Build\dist\CSFusion-v<Version>-Win64[-Development].zip
#
# Shipping      the player build: fastest, smallest, no console, no logs, no
#               automation tests (the installed engine cannot enable logging in
#               Shipping; that needs a source-built engine).
# Development   the tester build: logs in CSFusion\Saved\Logs, the `cs.*`
#               console commands and every -cstest* self-test
#               (Scripts\run_tests.ps1 -Packaged).
#
# The Version is written to ProjectVersion/AppVersion in Config\DefaultGame.ini
# before building. AppVersion becomes the Photon AppVersion, so builds of
# different versions never end up in the same room.

param(
    [Parameter(Mandatory = $true)][string]$Version,
    [ValidateSet("Shipping", "Development")][string]$Config = "Shipping",
    [string]$EngineDir = "C:\Program Files\Epic Games\UE_5.8\Engine",
    [switch]$SkipZip
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Root "CSFusion.uproject"
$RunUAT = Join-Path $EngineDir "Build\BatchFiles\RunUAT.bat"
$Archive = Join-Path $Root "Build\$Config"
$Dist = Join-Path $Root "Build\dist"

if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Version must look like 1.2.3, got '$Version'" }

# --- Version stamp -------------------------------------------------------------
$ini = Join-Path $Root "Config\DefaultGame.ini"
$text = [System.IO.File]::ReadAllText($ini)
$text = $text -replace '(?m)^ProjectVersion=.*$', "ProjectVersion=$Version"
$text = $text -replace '(?m)^AppVersion=.*$', "AppVersion=$Version"
[System.IO.File]::WriteAllText($ini, $text, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "Version stamped: $Version" -ForegroundColor Cyan

# --- Build, cook, stage, archive -------------------------------------------------
if (Test-Path $Archive) { Remove-Item $Archive -Recurse -Force }
$uatArgs = @(
    "BuildCookRun", "-project=$Project", "-noP4", "-platform=Win64", "-clientconfig=$Config",
    "-build", "-cook", "-stage", "-pak", "-iostore", "-compressed", "-prereqs", "-archive",
    "-archivedirectory=$Archive",
    "-map=/Game/Maps/Lvl_MainMenu+/Game/Maps/Lvl_Warehouse+/Game/Maps/Lvl_Depot+/Game/Maps/Lvl_OldTown",
    "-unattended", "-utf8output"
)
if ($Config -eq "Shipping") { $uatArgs += @("-nodebuginfo", "-distribution") }

Write-Host "RunUAT $($uatArgs -join ' ')"
& $RunUAT @uatArgs
if ($LASTEXITCODE -ne 0) { throw "BuildCookRun failed with exit code $LASTEXITCODE" }

$staged = Join-Path $Archive "Windows"
if (-not (Test-Path (Join-Path $staged "CSFusion.exe"))) { throw "Staged build missing: $staged\CSFusion.exe" }

# Anything the smoke tests wrote into the staged folder must not ship.
$saved = Join-Path $staged "CSFusion\Saved"
if (Test-Path $saved) { Remove-Item $saved -Recurse -Force }

# --- Zip --------------------------------------------------------------------------
if (-not $SkipZip) {
    New-Item -ItemType Directory -Force $Dist | Out-Null
    $suffix = if ($Config -eq "Shipping") { "" } else { "-$Config" }
    $zip = Join-Path $Dist "CSFusion-v$Version-Win64$suffix.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    # tar.exe ships with Windows 10+ and handles >2 GB, unlike Compress-Archive on PS 5.1.
    Push-Location $staged
    try { & tar.exe -a -c -f $zip . } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { throw "tar failed with exit code $LASTEXITCODE" }
    $mb = [math]::Round((Get-Item $zip).Length / 1MB)
    Write-Host "Packaged: $zip ($mb MB)" -ForegroundColor Green
}

$exeMb = [math]::Round((Get-ChildItem (Join-Path $staged "CSFusion\Binaries\Win64") -Filter "CSFusion*.exe" | Measure-Object Length -Sum).Sum / 1MB)
$paksMb = [math]::Round((Get-ChildItem (Join-Path $staged "CSFusion\Content\Paks") | Measure-Object Length -Sum).Sum / 1MB)
Write-Host "Staged build: $staged (game exe $exeMb MB, content $paksMb MB)" -ForegroundColor Green
