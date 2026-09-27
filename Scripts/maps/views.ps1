# v2.1 map rework: screenshots of a map from the views the build script wrote
# (Saved/CSTest/views_<Set>.txt, read by -cstestviews), then one contact sheet.
#
#   powershell -ExecutionPolicy Bypass -File Scripts\maps\views.ps1 -Map /Game/Maps/Lvl_Depot -Set depot
#
# Editor build by default; -Packaged uses Build\Development\Windows.
param(
    [Parameter(Mandatory = $true)][string]$Map,
    [Parameter(Mandatory = $true)][string]$Set,
    [string]$Resolution = "1600x900",
    [switch]$Packaged,
    [string]$Extra = "",
    [int]$TimeoutSec = 300
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ResX, $ResY = $Resolution -split 'x'
$log = "views_$Set.log"
if ($Packaged) {
    $exe = Join-Path $Root "Build\Development\Windows\CSFusion\Binaries\Win64\CSFusion.exe"
    $logPath = Join-Path $Root "Build\Development\Windows\CSFusion\Saved\Logs\$log"
    $argline = "$Map -windowed -ResX=$ResX -ResY=$ResY -noautodetect -noaccount -noautoconnect -nospawnprotection -cstestviews=$Set $Extra -LOG=$log"
} else {
    $exe = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
    $logPath = Join-Path $Root "Saved\Logs\$log"
    $argline = "`"$(Join-Path $Root 'CSFusion.uproject')`" $Map -game -windowed -ResX=$ResX -ResY=$ResY -noautodetect -noaccount -noautoconnect -nospawnprotection -cstestviews=$Set $Extra -LOG=$log"
}
if (Test-Path $logPath) { Remove-Item $logPath -Force }
Get-ChildItem (Join-Path $Root "Saved\CSTest") -Filter "views_$($Set)_*.png" -ErrorAction SilentlyContinue | Remove-Item -Force
$p = Start-Process -FilePath $exe -ArgumentList $argline -PassThru
$t = 0
while ($t -lt $TimeoutSec -and -not ((Test-Path $logPath) -and (Select-String $logPath -Pattern "VIEWS RESULT" -Quiet))) { Start-Sleep 2; $t += 2 }
Start-Sleep 2
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
if (Test-Path $logPath) { Select-String $logPath -Pattern "VIEWS RESULT|Error:|Ensure condition" | Select-Object -First 10 | ForEach-Object { $_.Line } }
$py = Join-Path $env:LOCALAPPDATA "Programs\Python\Python313\python.exe"
& $py -P -c @"
import glob, os
from PIL import Image
fs = sorted(glob.glob(os.path.join(r'$Root', 'Saved', 'CSTest', 'views_${Set}_*.png')))
if fs:
    w = 800; ims = [Image.open(f).convert('RGB') for f in fs]
    ims = [im.resize((w, int(im.height * w / im.width))) for im in ims]
    h = ims[0].height; cols = 2; rows = (len(ims) + cols - 1) // cols
    sheet = Image.new('RGB', (w * cols, h * rows))
    for i, im in enumerate(ims): sheet.paste(im, ((i % cols) * w, (i // cols) * h))
    out = os.path.join(r'$Root', 'Saved', 'CSTest', 'sheet_views_$Set.png'); sheet.save(out); print('sheet', out, len(fs))
"@
