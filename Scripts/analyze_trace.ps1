# Copyright (c) 2026 CS-Fusion. All Rights Reserved.
#
# v2.0 phase 7: timer statistics from an Unreal Insights trace, over the perf
# sample only (the CSPerfSample region the perf self-test marks).
#
#   powershell -ExecutionPolicy Bypass -File Scripts\run_tests.ps1 -Packaged -Only perf -Resolution 1920x1080 -Trace
#   powershell -ExecutionPolicy Bypass -File Scripts\analyze_trace.ps1 -Trace Saved\Traces\cstest_perf.utrace
#
# Writes %TEMP%\<trace>.<thread>.csv (UnrealInsights TimingInsights.ExportTimerStatistics,
# one row per timer) for GameThread, RenderThread and the GPU, and prints the
# most expensive timers per frame with Scripts\summarize_timers.py.

param(
    [Parameter(Mandatory = $true)][string]$Trace,
    [string]$EngineDir = "C:\Program Files\Epic Games\UE_5.8\Engine",
    [string]$Region = "CSPerfSample",
    [int]$Top = 25
)

$ErrorActionPreference = "Stop"
$Insights = Join-Path $EngineDir "Binaries\Win64\UnrealInsights.exe"
$TracePath = (Resolve-Path $Trace).Path
$Python = Join-Path $env:LOCALAPPDATA "Programs\Python\Python313\python.exe"
$script:Frames = 0

foreach ($thread in @("GameThread", "RenderThread", "GPU*")) {
    $safe = $thread -replace '[^A-Za-z]', ''
    $csv = Join-Path $env:TEMP ([IO.Path]::GetFileNameWithoutExtension($TracePath) + ".$safe.csv")
    if (Test-Path $csv) { Remove-Item $csv -Force }
    # No quotes inside the command: Insights does not unescape them (paths must not contain spaces).
    $cmd = "TimingInsights.ExportTimerStatistics $csv -threads=$thread -region=$Region"
    $p = Start-Process -FilePath $Insights -ArgumentList "-OpenTraceFile=`"$TracePath`" -NoUI -AutoQuit -ExecOnAnalysisCompleteCmd=`"$cmd`"" -PassThru -Wait -WindowStyle Hidden
    if (-not (Test-Path $csv)) { Write-Warning "No statistics for $thread (exit $($p.ExitCode))."; continue }
    Write-Host "`n== $thread" -ForegroundColor Cyan
    if ($thread -eq "GameThread") {
        $tick = Import-Csv $csv | Where-Object { $_.Name -eq "FEngineLoop::Tick" } | Select-Object -First 1
        $script:Frames = if ($tick) { [int]$tick.Count } else { 0 }
    }
    & $Python -P (Join-Path $PSScriptRoot "summarize_timers.py") $csv $Top $script:Frames
}
