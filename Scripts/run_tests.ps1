# Copyright (c) 2026 CS-Fusion. All Rights Reserved.
#
# Stage 8 test runner. Runs the headless unit tests and the in-game
# self-tests, then prints a PASS/FAIL summary. Exit code 0 = everything passed.
#
#   powershell -ExecutionPolicy Bypass -File Scripts\run_tests.ps1                # editor build
#   powershell -ExecutionPolicy Bypass -File Scripts\run_tests.ps1 -Packaged      # Build\Development\Windows
#   powershell -ExecutionPolicy Bypass -File Scripts\run_tests.ps1 -Network       # + two-client anti-cheat
#   powershell -ExecutionPolicy Bypass -File Scripts\run_tests.ps1 -Only cheat,perf
#
# Every self-test writes lines like "CHEAT TEST RESULT: ... -> REJECTED OK".
# A line is a failure when its verdict contains BROKEN / NOT STOPPED /
# STILL BLOCKED / MISSING / NOT DETECTED; a suite fails when a line fails, or
# when its final line never appears before the timeout.

param(
    [switch]$Packaged,
    [switch]$Network,
    [string[]]$Only = @(),
    [string]$EngineDir = "C:\Program Files\Epic Games\UE_5.8\Engine",
    [double]$PerfBudgetMs = 33.3
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Root "CSFusion.uproject"
$Editor = Join-Path $EngineDir "Binaries\Win64\UnrealEditor.exe"
$EditorCmd = Join-Path $EngineDir "Binaries\Win64\UnrealEditor-Cmd.exe"
# The top-level CSFusion.exe is only a launcher that starts this binary and
# exits, so stopping the launcher would leave the game running. Start it directly.
$GameExe = Join-Path $Root "Build\Development\Windows\CSFusion\Binaries\Win64\CSFusion.exe"
$LogDir = if ($Packaged) { Join-Path $Root "Build\Development\Windows\CSFusion\Saved\Logs" } else { Join-Path $Root "Saved\Logs" }
$Map = "/Game/Maps/Lvl_Warehouse"
$FailPattern = "BROKEN|NOT STOPPED|STILL BLOCKED|MISSING|NOT DETECTED|no frames"

# Name, extra flags, regex of the LAST result line, timeout seconds.
$Suites = @(
    @{ Name = "input";      Flags = "-cstestinput";            Done = "FIRE TEST RESULT";              Timeout = 60 },
    @{ Name = "loot";       Flags = "-cstestloot";             Done = "LOOT TEST RESULT: drop";        Timeout = 70 },
    @{ Name = "doubledrop"; Flags = "-cstestdoubledrop";       Done = "DOUBLE DROP TEST RESULT";       Timeout = 60 },
    @{ Name = "ui";         Flags = "-cstestui";               Done = "UI TEST RESULT: combat HUD";    Timeout = 80 },
    @{ Name = "bots";       Flags = "-bots=4 -cstestbots";     Done = "BOT TEST RESULT";               Timeout = 150 },
    @{ Name = "cheat";      Flags = "-cstestcheat";            Done = "suspension lifted";             Timeout = 90 },
    @{ Name = "perf";       Flags = "-bots=8 -cstestperf";     Done = "PERF TEST RESULT";              Timeout = 120 }
)

$Results = New-Object System.Collections.Generic.List[object]

function Add-Result([string]$Suite, [bool]$Ok, [string]$Detail) {
    $Results.Add([pscustomobject]@{ Suite = $Suite; Result = $(if ($Ok) { "PASS" } else { "FAIL" }); Detail = $Detail })
    $color = if ($Ok) { "Green" } else { "Red" }
    Write-Host ("  {0,-12} {1}  {2}" -f $Suite, $(if ($Ok) { "PASS" } else { "FAIL" }), $Detail) -ForegroundColor $color
}

function Start-Client([string]$ClientArgs, [string]$LogName) {
    $logArg = "-LOG=$LogName"
    if ($Packaged) {
        return Start-Process -FilePath $GameExe -ArgumentList "$Map -windowed -ResX=960 -ResY=540 $ClientArgs $logArg" -PassThru
    }
    return Start-Process -FilePath $Editor -ArgumentList "`"$Project`" $Map -game -windowed -ResX=960 -ResY=540 $ClientArgs $logArg" -PassThru
}

function Stop-Client($Proc) {
    if ($Proc -and -not $Proc.HasExited) { Stop-Process -Id $Proc.Id -Force -ErrorAction SilentlyContinue }
}

function Wait-ForLine([string]$LogPath, [string]$Pattern, [int]$TimeoutSec) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 2
        if ((Test-Path $LogPath) -and (Select-String -Path $LogPath -Pattern $Pattern -Quiet)) {
            Start-Sleep -Seconds 1   # let the rest of the frame's lines flush
            return $true
        }
    }
    return $false
}

function Get-ResultLines([string]$LogPath) {
    if (-not (Test-Path $LogPath)) { return @() }
    return @(Select-String -Path $LogPath -Pattern "RESULT:" | ForEach-Object { ($_.Line -replace '^.*?LogCS: (Warning: )?', '') })
}

# An ensure or a crash is a failure even when every RESULT line says OK.
function Test-CleanLog([string]$Suite, [string]$LogPath) {
    if (-not (Test-Path $LogPath)) { return }
    $bad = @(Select-String -Path $LogPath -Pattern "Ensure condition failed|Assertion failed|Fatal error|Unhandled Exception")
    if ($bad.Count -gt 0) {
        Add-Result $Suite $false ("{0}: {1}" -f (Split-Path $LogPath -Leaf), ($bad[0].Line -replace '^.*?Error: ', ''))
    }
}

function Test-Selected([string]$Name) { return ($Only.Count -eq 0) -or ($Only -contains $Name) }

Write-Host "CS-Fusion test run ($(if ($Packaged) { 'packaged build' } else { 'editor build' }))" -ForegroundColor Cyan

# --- 1. Unit tests (headless, editor binaries) --------------------------------
if (Test-Selected "unit") {
    Write-Host "`n[unit] UE Automation: CSFusion.Unit.*"
    if (Test-Path $EditorCmd) {
        $unitLog = Join-Path $Root "Saved\Logs\cstest_unit.log"
        & $EditorCmd $Project "-ExecCmds=Automation RunTests CSFusion.Unit; Quit" -unattended -nullrhi -nosplash -nosound "-LOG=cstest_unit.log" | Out-Null
        $lines = @(Select-String -Path $unitLog -Pattern "Test Completed\. Result=\{(\w+)\}.*Path=\{([^}]+)\}")
        if ($lines.Count -eq 0) { Add-Result "unit" $false "no automation results (see $unitLog)" }
        foreach ($l in $lines) {
            $ok = $l.Matches[0].Groups[1].Value -eq "Success"
            Add-Result "unit" $ok $l.Matches[0].Groups[2].Value
        }
    } else {
        Add-Result "unit" $false "UnrealEditor-Cmd.exe not found; unit tests need the editor"
    }
}

# --- 2. In-game self-tests (one offline process per suite) --------------------
if ($Packaged -and -not (Test-Path $GameExe)) { throw "Packaged build not found: $GameExe" }

foreach ($s in $Suites) {
    if (-not (Test-Selected $s.Name)) { continue }
    Write-Host "`n[$($s.Name)] $($s.Flags)"
    $logName = "cstest_$($s.Name).log"
    $logPath = Join-Path $LogDir $logName
    if (Test-Path $logPath) { Remove-Item $logPath -Force }

    $proc = Start-Client "-noautoconnect $($s.Flags)" $logName
    $done = Wait-ForLine $logPath $s.Done $s.Timeout
    Stop-Client $proc

    $lines = Get-ResultLines $logPath
    foreach ($line in $lines) {
        $ok = $line -notmatch $FailPattern
        if ($s.Name -eq "perf" -and $line -match "p95 ([\d\.]+) ms") {
            $p95 = [double]$Matches[1]
            $ok = $ok -and ($p95 -le $PerfBudgetMs)
            $line = "$line  [budget p95 <= $PerfBudgetMs ms]"
        }
        Add-Result $s.Name $ok $line
    }
    if (-not $done) { Add-Result $s.Name $false "timed out after $($s.Timeout) s waiting for '$($s.Done)'" }
    Test-CleanLog $s.Name $logPath
}

# --- 3. Two-client anti-cheat (Photon room) -----------------------------------
if ($Network -and (Test-Selected "netcheat")) {
    Write-Host "`n[netcheat] master + cheating client in a Photon room"
    $room = "cstest$(Get-Random -Maximum 999999)"
    $logA = Join-Path $LogDir "cstest_netcheat_A.log"
    $logB = Join-Path $LogDir "cstest_netcheat_B.log"
    foreach ($p in @($logA, $logB)) { if (Test-Path $p) { Remove-Item $p -Force } }

    $a = Start-Client "-room=$room" "cstest_netcheat_A.log"
    Start-Sleep -Seconds 20
    $b = Start-Client "-room=$room -WinX=980 -cstestcheat" "cstest_netcheat_B.log"
    $done = Wait-ForLine $logB "suspension lifted" 120
    Stop-Client $b
    Stop-Client $a

    foreach ($line in (Get-ResultLines $logB)) { Add-Result "netcheat" ($line -notmatch $FailPattern) $line }
    $suspended = (Test-Path $logA) -and (Select-String -Path $logA -Pattern "Cheat guard: player \d+ SUSPENDED" -Quiet)
    Add-Result "netcheat" $suspended "master log shows the suspension"
    Test-CleanLog "netcheat" $logA
    Test-CleanLog "netcheat" $logB
    if (-not $done) { Add-Result "netcheat" $false "timed out waiting for the client's results" }
}

# --- Summary -------------------------------------------------------------------
$failed = @($Results | Where-Object { $_.Result -eq "FAIL" })
Write-Host ""
Write-Host ("SUMMARY: {0} checks, {1} passed, {2} failed" -f $Results.Count, ($Results.Count - $failed.Count), $failed.Count) -ForegroundColor $(if ($failed.Count -eq 0) { "Green" } else { "Red" })
if ($failed.Count -gt 0) {
    $failed | ForEach-Object { Write-Host ("  FAIL {0}: {1}" -f $_.Suite, $_.Detail) -ForegroundColor Red }
    exit 1
}
exit 0
