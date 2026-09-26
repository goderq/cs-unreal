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
#
# v2.0 phase 5: NVIDIA DLSS ships inside every build, like in any game (the
# NVIDIA RTX SDK license allows the plugin's runtime in a released game; only
# its files stay out of the public repository). If Plugins\DLSS is missing it
# is installed from the downloaded archive (Scripts\install_dlss.ps1); with no
# archive either, packaging stops. -NoDLSS builds without it on purpose. After
# staging, the build is checked for NVIDIA's runtime (nvngx_dlss.dll).

param(
    [Parameter(Mandatory = $true)][string]$Version,
    [ValidateSet("Shipping", "Development")][string]$Config = "Shipping",
    [string]$EngineDir = "C:\Program Files\Epic Games\UE_5.8\Engine",
    [switch]$SkipZip,
    [switch]$NoKeys,
    [switch]$NoDLSS
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

# --- NVIDIA DLSS -------------------------------------------------------------------
if (-not $NoDLSS -and -not (Test-Path (Join-Path $Root "Plugins\DLSS\DLSS.uplugin"))) {
    Write-Host "Plugins\DLSS missing: installing it from the downloaded archive." -ForegroundColor Cyan
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "install_dlss.ps1")
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path (Join-Path $Root "Plugins\DLSS\DLSS.uplugin"))) {
        throw "NVIDIA DLSS is not installed and could not be installed (see docs/GRAPHICS.md). Use -NoDLSS only for a build meant to go without it."
    }
}

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

if (-not $NoDLSS) {
    $dlss = Get-ChildItem $staged -Recurse -Filter "nvngx_dlss.dll" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $dlss) { throw "NVIDIA DLSS runtime (nvngx_dlss.dll) missing from the staged build: $staged" }
    Write-Host "NVIDIA DLSS in the build: $($dlss.FullName.Substring($staged.Length + 1))" -ForegroundColor Green
}

# Anything the smoke tests wrote into the staged folder must not ship.
$saved = Join-Path $staged "CSFusion\Saved"
if (Test-Path $saved) { Remove-Item $saved -Recurse -Force }

# --- Account keys -----------------------------------------------------------------
# Staging only picks up Default*.ini, so Config\Backend.ini is copied in by hand.
# Without it the packaged game cannot sign anybody in - and signing in is what
# lets you play. The keys therefore travel inside the build, which is how every
# EOS client works; the point of keeping them out of the repository is that they
# cannot be scraped automatically. Use -NoKeys for a build meant to stay keyless.
$backend = Join-Path $Root "Config\Backend.ini"
if ($NoKeys) {
    Write-Host "Account keys left out (-NoKeys): the build cannot sign in." -ForegroundColor Yellow
} elseif (Test-Path $backend) {
    # Config\ is cooked into the pak, so the staged folder has no such directory
    # yet; a loose file there is still read at runtime (the pak file system
    # falls through to the real one).
    $stagedConfig = Join-Path $staged "CSFusion\Config"
    New-Item -ItemType Directory -Force $stagedConfig | Out-Null
    # The [Photon] section (TestAppId) is for the self-tests on this machine
    # only: a packaged build must never send players without an account to
    # the test app, where anonymous clients are allowed (AUDIT B5).
    $kept = New-Object System.Collections.Generic.List[string]
    $inPhoton = $false
    foreach ($line in [System.IO.File]::ReadAllLines($backend)) {
        if ($line -match '^\s*\[(.+)\]\s*$') { $inPhoton = ($Matches[1] -eq 'Photon') }
        if (-not $inPhoton) { $kept.Add($line) }
    }
    [System.IO.File]::WriteAllLines((Join-Path $stagedConfig "Backend.ini"), $kept)
    Write-Host "Account keys copied into the build (Config\Backend.ini)." -ForegroundColor Yellow
} else {
    Write-Host "No Config\Backend.ini: the build cannot sign in (see docs/ACCOUNTS.md)." -ForegroundColor Yellow
}

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
