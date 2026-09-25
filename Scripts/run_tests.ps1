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
# "-Only a,b" arrives as one string when the script is started with -File.
$Only = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$Root = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Root "CSFusion.uproject"
$Editor = Join-Path $EngineDir "Binaries\Win64\UnrealEditor.exe"
$EditorCmd = Join-Path $EngineDir "Binaries\Win64\UnrealEditor-Cmd.exe"
# The top-level CSFusion.exe is only a launcher that starts this binary and
# exits, so stopping the launcher would leave the game running. Start it directly.
$GameExe = Join-Path $Root "Build\Development\Windows\CSFusion\Binaries\Win64\CSFusion.exe"
$LogDir = if ($Packaged) { Join-Path $Root "Build\Development\Windows\CSFusion\Saved\Logs" } else { Join-Path $Root "Saved\Logs" }
$Map = "/Game/Maps/Lvl_Depot"
$FailPattern = "BROKEN|NOT STOPPED|STILL BLOCKED|MISSING|NOT DETECTED|no frames"

# Name, extra flags, regex of the LAST result line, timeout seconds.
$Suites = @(
    @{ Name = "input";      Flags = "-cstestinput";            Done = "FIRE TEST RESULT";              Timeout = 60 },
    @{ Name = "loadout";    Flags = "-cstestloot";             Done = "LOADOUT TEST: done";            Timeout = 90 },
    @{ Name = "doubledrop"; Flags = "-cstestdoubledrop";       Done = "DOUBLE DROP TEST RESULT";       Timeout = 60 },
    @{ Name = "ui";         Flags = "-cstestui";               Done = "UI TEST RESULT: combat HUD";    Timeout = 80 },
    @{ Name = "bots";       Flags = "-bots=4 -cstestbots";     Done = "BOT TEST RESULT";               Timeout = 150 },
    @{ Name = "weapons";    Flags = "-cstestweapons";          Done = "WEAPON TEST: done";             Timeout = 150 },
    @{ Name = "round";     Flags = "-roundtime=20 -bots=2 -cstestround"; Done = "SCORES RESET|ROUND FLOW BROKEN"; Timeout = 120 },
    @{ Name = "cheat";     Flags = "-cstestcheat";            Done = "suspension lifted";             Timeout = 90 },
    @{ Name = "modes";     Flags = "-mode=dm -cstestmodes";   Done = "MODES TEST: done";              Timeout = 120; Map = "/Game/Maps/Lvl_Depot" },
    @{ Name = "comp";      Flags = "-mode=5v5 -roundtime=40 -cstestcomp"; Done = "ROUNDS OK|ROUNDS BROKEN"; Timeout = 240; Map = "/Game/Maps/Lvl_OldTown" },
    @{ Name = "poses";     Flags = "-mode=dm -bots=1 -cstestposes"; Done = "RESPAWN POSE"; Timeout = 90; Map = "/Game/Maps/Lvl_Depot" },
    @{ Name = "tourdepot"; Flags = "-cstesttour";             Done = "TOUR RESULT";                   Timeout = 150; Map = "/Game/Maps/Lvl_Depot" },
    @{ Name = "touroldtown"; Flags = "-cstesttour";           Done = "TOUR RESULT";                   Timeout = 150; Map = "/Game/Maps/Lvl_OldTown" },
    @{ Name = "perf";       Flags = "-bots=8 -cstestperf";     Done = "PERF TEST RESULT";              Timeout = 120 }
)

$Results = New-Object System.Collections.Generic.List[object]

function Add-Result([string]$Suite, [bool]$Ok, [string]$Detail) {
    $Results.Add([pscustomobject]@{ Suite = $Suite; Result = $(if ($Ok) { "PASS" } else { "FAIL" }); Detail = $Detail })
    $color = if ($Ok) { "Green" } else { "Red" }
    Write-Host ("  {0,-12} {1}  {2}" -f $Suite, $(if ($Ok) { "PASS" } else { "FAIL" }), $Detail) -ForegroundColor $color
}

# $StartMap = "" starts on the project's default map (the main menu).
function Start-Client([string]$ClientArgs, [string]$LogName, [string]$StartMap = $Map) {
    $logArg = "-LOG=$LogName"
    # Legacy suites test pickup and shooting as in v1.0: weapons on the floor,
    # no spawn protection. The v1.1 mode suites (-cstestmodes*) run the real rules.
    # Self-tests cannot sign in to Epic, so they always run without an account.
    $ClientArgs = "-noaccount $ClientArgs"
    if ($ClientArgs -notmatch "cstestmodes|cstestposes|cstestcomp") { $ClientArgs = "-nospawnprotection $ClientArgs" }
    if ($Packaged) {
        return Start-Process -FilePath $GameExe -ArgumentList "$StartMap -windowed -ResX=960 -ResY=540 $ClientArgs $logArg" -PassThru
    }
    return Start-Process -FilePath $Editor -ArgumentList "`"$Project`" $StartMap -game -windowed -ResX=960 -ResY=540 $ClientArgs $logArg" -PassThru
}

function Stop-Client($Proc) {
    if ($Proc -and -not $Proc.HasExited) { Stop-Process -Id $Proc.Id -Force -ErrorAction SilentlyContinue }
}

function Wait-ForLine([string]$LogPath, [string]$Pattern, [int]$TimeoutSec) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 2
        if ((Test-Path $LogPath) -and (Select-String -Path $LogPath -Pattern $Pattern -CaseSensitive -Quiet)) {
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

    $suiteMap = $(if ($s.Map) { $s.Map } else { $Map })
    $proc = Start-Client "-noautoconnect $($s.Flags)" $logName $suiteMap
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

# --- 3. Two-client scenarios (Photon room) -------------------------------------
# A starts first and becomes the Master Client; B joins after DelayB seconds.
#   Done      : "A:regex" / "B:regex" - the line that ends the scenario
#   KillB     : "B:regex" - when it appears, B's process is killed (a crash,
#               not a clean leave), and the scenario continues on A
#   Expect    : extra "A:regex|label" / "B:regex|label" lines that must exist
#   Menu      : both clients start on the main menu instead of the map
# {ROOM} in the flags is replaced with a fresh room name.
$NetScenarios = @(
    @{ Name = "netshoot"; A = "-room={ROOM} -cstestinput -cstestshoot -LogCmds=`"LogCSCombat Verbose`""; B = "-room={ROOM} -cstestinput -cstestshoot";
       DelayB = 5; Done = "B:SHOOT TEST RESULT"; Timeout = 90;
       Expect = @("A:SHOOT TEST RESULT|master: shot at the other player", "B:NOW the authority|__absent__",
                  # Phase 2: honest RPCs are never refused by the sender check.
                  "A:RPC Rpc\w+ on .* refused|__absent__", "B:RPC Rpc\w+ on .* refused|__absent__") },
    @{ Name = "netcontest"; A = "-room={ROOM} -cstestcontest"; B = "-room={ROOM} -cstestcontest";
       DelayB = 3; Done = "A:CONTEST TEST RESULT"; Timeout = 90;
       Expect = @("A:CONTEST TEST RESULT: player \d+ WON|B:CONTEST TEST RESULT: player \d+ WON|exactly one player won the contested pickup") },
    @{ Name = "netdeath"; A = "-room={ROOM} -testprimary=m4 -cstestkill -LogCmds=`"LogCSCombat Verbose`""; B = "-room={ROOM} -cstestgrab";
       DelayB = 3; Done = "A:RESPAWN RESULT"; Timeout = 120; Expect = @() },
    @{ Name = "netleave"; A = "-room={ROOM} -testprimary=m4 -cstestwatchleave"; B = "-room={ROOM} -cstestgrab";
       DelayB = 3; KillB = "B:GRAB TEST RESULT"; Done = "A:LEAVE CLAIM RESULT|LEAVE TEST RESULT: player \d+ never left"; Timeout = 150; Expect = @() },
    @{ Name = "nethost"; A = "-room={ROOM} -bots=2 -cstestleave=45"; B = "-room={ROOM} -cstestkill -cstestkillbot -cstestbots";
       DelayB = 3; Done = "B:BOT TEST RESULT"; Timeout = 150;
       Expect = @("B:now controlled by this peer|B took over the bots after the host left", "B:NOW the authority|B became the Master Client") },
    @{ Name = "netmenu"; A = "-cstestmenu=create:{ROOM} -cstestui"; B = "-cstestmenu=browsejoin:{ROOM}"; Menu = $true;
       DelayB = 15; Done = "B:-> ECSSessionState::InRoom|BROWSER BROKEN"; Timeout = 120;
       Expect = @("B:-> ECSSessionState::InRoom|B joined A's room from the browser") },
    # Phase 2 (A1): B sends RPCs as a modified client would; A (Master Client) must refuse them.
    @{ Name = "netspoof"; A = "-room={ROOM}"; B = "-room={ROOM} -cstestspoof";
       DelayB = 3; Done = "B:SPOOF TEST: done|SPOOF TEST RESULT: .*MISSING"; Timeout = 120;
       Expect = @("A:RPC RpcRequestSlot on .* refused: sent by player|master refused the slot request on its pawn",
                  "A:RPC RpcGrenadeExploded on .* refused: sent by player|master refused the forged flashbang",
                  "B:RPC RpcGrenadeExploded on .* refused|B refused its own forged flashbang",
                  "A:RpcRequestFire from player \d+ refused: origin|master refused a shot from NaN (B9)",
                  "A:RpcRequestThrow from player \d+ refused: origin|master refused an infinite throw (B9)",
                  "A:RpcRequestMelee from player \d+ refused: origin|master refused a knife from 10 000 km (B9)") },
    # Phase 2 (A2): B flags its own pawn as a bot; A shoots it - the damage must still reach player B.
    @{ Name = "netbotflag"; A = "-room={ROOM} -cstestinput -cstestshoot -cstestexpectbotflag"; B = "-room={ROOM} -cstestbotflag";
       DelayB = 3; Done = "A:SHOOT TEST RESULT"; Timeout = 120;
       Expect = @("B:BOTFLAG TEST: my pawn now claims to be bot 4242|B flagged its pawn as a bot",
                  "A:SHOOT TEST RESULT: victim [1-9] hp 100 -> \d+ -> DAMAGE OK|damage reached the real player, not bot 4242") },
    # Phase 2 (B11): a frozen game is not a departure; repeated cheating removes the player.
    @{ Name = "netfreeze"; A = "-room={ROOM}"; B = "-room={ROOM} -cstestfreeze";
       DelayB = 3; Done = "B:FREEZE TEST RESULT"; Timeout = 150;
       Expect = @("A:no heartbeat for .* marked inactive \(kept in the match\)|master marked the frozen B inactive",
                  "A:heartbeat is back - active again|master saw B come back",
                  "A:Player \d+ left \(|__absent__") },
    # Phase 2 (B10): stepping through walls at a legal average speed is still caught.
    @{ Name = "netwallhop"; A = "-room={ROOM} -LogCmds=`"LogCSSecurity Verbose`""; B = "-room={ROOM} -cstestwallhop";
       DelayB = 3; Done = "B:WALLHOP TEST: done"; Timeout = 150;
       Expect = @("B:WALLHOP TEST: done \([1-9]|B found a wall and stepped through it",
                  "A:strike \(through a wall\)|master caught the step through the wall") },
    @{ Name = "netremoval"; A = "-room={ROOM}"; B = "-room={ROOM} -cstestremoval";
       DelayB = 3; Done = "B:Leaving for the menu: You were removed"; Timeout = 150;
       Expect = @("A:REMOVED from the match after 3 suspensions|master removed B after three suspensions",
                  "B:Leaving for the menu: You were removed|B's game left for the menu with the reason") },
    @{ Name = "netcheat"; A = "-room={ROOM}"; B = "-room={ROOM} -cstestcheat";
       DelayB = 20; Done = "B:suspension lifted"; Timeout = 120;
       Expect = @("A:Cheat guard: player \d+ SUSPENDED|master suspended the cheating client") }
)

function Resolve-Side([string]$Spec, [string]$LogA, [string]$LogB) {
    $side, $rest = $Spec -split ':', 2
    return @{ Log = $(if ($side -eq "A") { $LogA } else { $LogB }); Pattern = $rest }
}

if ($Network) {
    foreach ($n in $NetScenarios) {
        if (-not (Test-Selected $n.Name) -and -not (Test-Selected "network")) { continue }
        Write-Host "`n[$($n.Name)] A: $($n.A)   B: $($n.B)"
        $room = "cstest$(Get-Random -Maximum 999999)"
        $logA = Join-Path $LogDir "cstest_$($n.Name)_A.log"
        $logB = Join-Path $LogDir "cstest_$($n.Name)_B.log"
        foreach ($p in @($logA, $logB)) { if (Test-Path $p) { Remove-Item $p -Force } }
        $startMap = if ($n.Menu) { "" } else { $Map }

        $a = Start-Client ($n.A -replace '\{ROOM\}', $room) (Split-Path $logA -Leaf) $startMap
        # B must not start before A is in the room: otherwise B can create it
        # first and become the Master Client, which inverts every scenario.
        if (-not (Wait-ForLine $logA "-> ECSSessionState::InRoom" 90)) {
            Add-Result $n.Name $false "A never reached the room"
        }
        Start-Sleep -Seconds $n.DelayB
        $b = Start-Client ("-WinX=980 " + ($n.B -replace '\{ROOM\}', $room)) (Split-Path $logB -Leaf) $startMap

        $deadline = (Get-Date).AddSeconds($n.Timeout)
        if ($n.KillB) {
            $k = Resolve-Side $n.KillB $logA $logB
            $killed = Wait-ForLine $k.Log $k.Pattern $n.Timeout
            Stop-Client $b
            if (-not $killed) { Add-Result $n.Name $false "never reached the point to kill B ('$($k.Pattern)')" }
        }
        $d = Resolve-Side $n.Done $logA $logB
        $remaining = [int][math]::Max(5, ($deadline - (Get-Date)).TotalSeconds)
        $done = Wait-ForLine $d.Log $d.Pattern $remaining
        Stop-Client $b
        Stop-Client $a
        Start-Sleep -Seconds 2

        foreach ($side in @(@{ Tag = "A"; Log = $logA }, @{ Tag = "B"; Log = $logB })) {
            foreach ($line in (Get-ResultLines $side.Log)) { Add-Result $n.Name ($line -notmatch $FailPattern) "$($side.Tag): $line" }
        }
        foreach ($e in $n.Expect) {
            $parts = $e -split '\|'
            $label = $parts[-1]
            if ($label -eq "__absent__") {
                # "X:regex|__absent__": the line must NOT be there.
                $r = Resolve-Side $parts[0] $logA $logB
                $present = (Test-Path $r.Log) -and (Select-String -Path $r.Log -Pattern $r.Pattern -CaseSensitive -Quiet)
                Add-Result $n.Name (-not $present) "not in $(Split-Path $r.Log -Leaf): '$($r.Pattern)'"
                continue
            }
            if ($parts.Count -eq 3) {
                # "A:x|B:x|label": exactly one of the two lines exists.
                $r1 = Resolve-Side $parts[0] $logA $logB
                $r2 = Resolve-Side $parts[1] $logA $logB
                $hits = @($r1, $r2 | Where-Object { (Test-Path $_.Log) -and (Select-String -Path $_.Log -Pattern $_.Pattern -CaseSensitive -Quiet) }).Count
                Add-Result $n.Name ($hits -eq 1) "$label ($hits of 2)"
                continue
            }
            $r = Resolve-Side $parts[0] $logA $logB
            $found = (Test-Path $r.Log) -and (Select-String -Path $r.Log -Pattern $r.Pattern -CaseSensitive -Quiet)
            Add-Result $n.Name $found $label
        }
        Test-CleanLog $n.Name $logA
        Test-CleanLog $n.Name $logB
        if (-not $done) { Add-Result $n.Name $false "timed out waiting for '$($d.Pattern)'" }
    }
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
