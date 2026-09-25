# Copyright (c) 2026 CS-Fusion. All Rights Reserved.
#
# Small patch to the Photon Fusion 3 Unreal SDK (Plugins/PhotonFusion, which is
# licensed and not in git). Run it once after installing or updating the SDK;
# running it again changes nothing. The project does not compile without it
# (Core/CSRpcGuard.h checks for FusionRpcOrigin.h).
#
#   powershell -ExecutionPolicy Bypass -File Scripts\patch_fusion_sdk.ps1
#   powershell -ExecutionPolicy Bypass -File Scripts\patch_fusion_sdk.ps1 -Revert
#
# What it changes (docs/AUDIT.md A1, K9):
#   The SDK hands an RPC to its handler without saying who sent it, although
#   the packet carries OriginPlayer. The patch records the sender (and the
#   room's Master Client) in FFusionRpcOrigin while the handler runs, so the
#   game can refuse an RPC sent by anyone but the owner of the object (a
#   request) or the Master Client (an event).
#   An RPC the sender dispatches to itself gets the local player as its origin.
#   Self-tests only (compiled out of Shipping): FFusionRpcOrigin::DebugForgeNext
#   makes the next RPC claim a different sender, to check whether the Photon
#   server overwrites a forged origin.
#
# And (AUDIT B5, K10): the wrapper connects with a random UserId and no
#   authentication type. FFusionCustomAuth lets the game send Custom
#   authentication - its profile id and login token - so Photon asks the
#   photon-auth function whether the player may connect at all.
#
# The original files are kept in Plugins/PhotonFusion/.cs-patch-backup.

param([switch]$Revert)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Plugin = Join-Path $Root "Plugins\PhotonFusion"
$Backup = Join-Path $Plugin ".cs-patch-backup"
$Marker = "CS-FUSION PATCH"
$Utf8Bom = New-Object System.Text.UTF8Encoding($true)

$Descriptor = Join-Path $Plugin "Source\PhotonFusion\Private\Types\FusionTypeDescriptor.cpp"
$OriginHeader = Join-Path $Plugin "Source\PhotonFusion\Public\FusionRpcOrigin.h"
$Connect = Join-Path $Plugin "Source\PhotonFusion\Private\Actions\FusionConnectToPhotonAsync.cpp"
$AuthHeader = Join-Path $Plugin "Source\PhotonFusion\Public\FusionCustomAuth.h"

if (-not (Test-Path $Descriptor)) { throw "Photon Fusion SDK not found at $Plugin" }

if ($Revert) {
    foreach ($f in @(@($Descriptor, "FusionTypeDescriptor.cpp"), @($Connect, "FusionConnectToPhotonAsync.cpp"))) {
        $saved = Join-Path $Backup $f[1]
        if (Test-Path $saved) { Copy-Item $saved $f[0] -Force; Write-Host "$($f[1]) restored." }
    }
    foreach ($h in @($OriginHeader, $AuthHeader)) {
        if (Test-Path $h) { Remove-Item $h -Force; Write-Host "$(Split-Path $h -Leaf) removed." }
    }
    exit 0
}

# --- FusionRpcOrigin.h (new public header) ------------------------------------
$header = @"
// $Marker (Scripts/patch_fusion_sdk.ps1) - not part of the Photon SDK.
//
// Who sent the Fusion RPC whose handler is running right now. Set by
// UFusionTypeDescriptor::OnReceiveRPC around the call to the handler.
// Game thread only.

#pragma once

#include "CoreMinimal.h"

struct PHOTONFUSION_API FFusionRpcOrigin
{
	/** True while a Fusion RPC handler runs. */
	static bool bInRpc;
	/** Photon player id carried by the RPC packet (0 when bInRpc is false). */
	static int32 Current;
	/** The room's Master Client when the RPC was delivered (0 when unknown). */
	static int32 MasterClient;
	/** Self-tests only, ignored in Shipping: the next RPC sent claims this origin. */
	static int32 DebugForgeNext;
};
"@
[System.IO.File]::WriteAllText($OriginHeader, ($header -replace "`r?`n", "`r`n"), $Utf8Bom)

function Replace-Once([string]$Source, [string]$Pattern, [string]$Replacement, [string]$What) {
    $found = [regex]::Matches($Source, $Pattern)
    if ($found.Count -ne 1) { throw "SDK changed: expected one match for $What, found $($found.Count). Patch not applied." }
    return [regex]::Replace($Source, $Pattern, $Replacement.Replace('$', '$$').Replace('$$0', '$0'))
}

$nl = "`r`n"
New-Item -ItemType Directory -Force $Backup | Out-Null

# --- FusionTypeDescriptor.cpp -------------------------------------------------
$text = [System.IO.File]::ReadAllText($Descriptor)
if ($text.Contains($Marker)) {
    Write-Host "FusionTypeDescriptor.cpp is already patched."
} else {
Copy-Item $Descriptor (Join-Path $Backup "FusionTypeDescriptor.cpp") -Force

$text = Replace-Once $text '#include "Types/FusionTypeDescriptor\.h"' (
    '$0' + $nl + '#include "FusionRpcOrigin.h" // ' + $Marker) "the descriptor include"

$text = Replace-Once $text 'TMap<FString, FFusionArrayHooks> UFusionTypeDescriptorLibrary::HooksMap;' (
    '$0' + $nl + $nl +
    "// ${Marker}: who sent the RPC being handled (FusionRpcOrigin.h)." + $nl +
    'bool FFusionRpcOrigin::bInRpc = false;' + $nl +
    'int32 FFusionRpcOrigin::Current = 0;' + $nl +
    'int32 FFusionRpcOrigin::MasterClient = 0;' + $nl +
    'int32 FFusionRpcOrigin::DebugForgeNext = 0;') "HooksMap"

$text = Replace-Once $text 'FusionCore::Rpc Rpc = FusionClient->Client->CreateUserRpc\([^;]*\);' (
    '$0' + $nl + $nl +
    "`t`t// ${Marker}: an RPC delivered to ourselves below carries us as its origin." + $nl +
    "`t`tif (Rpc.OriginPlayer == FusionCore::PlayerId{})" + $nl +
    "`t`t{" + $nl +
    "`t`t`tRpc.OriginPlayer = FusionClient->Client->LocalPlayerId();" + $nl +
    "`t`t}" + $nl +
    "#if !UE_BUILD_SHIPPING" + $nl +
    "`t`tif (FFusionRpcOrigin::DebugForgeNext != 0)" + $nl +
    "`t`t{" + $nl +
    "`t`t`tRpc.OriginPlayer = static_cast<FusionCore::PlayerId>(FFusionRpcOrigin::DebugForgeNext);" + $nl +
    "`t`t`tFFusionRpcOrigin::DebugForgeNext = 0;" + $nl +
    "`t`t}" + $nl +
    "#endif") "CreateUserRpc in OnSendRPC"

$text = Replace-Once $text '\t\tPair\.EngineObject->ProcessEvent\(FunctionDescriptor->Function, Params\);' (
    "`t`t// ${Marker}: tell the handler who sent this RPC." + $nl +
    "`t`tconst bool  bWasInRpc = FFusionRpcOrigin::bInRpc;" + $nl +
    "`t`tconst int32 PreviousOrigin = FFusionRpcOrigin::Current;" + $nl +
    "`t`tconst int32 PreviousMaster = FFusionRpcOrigin::MasterClient;" + $nl +
    "`t`tFFusionRpcOrigin::bInRpc = true;" + $nl +
    "`t`tFFusionRpcOrigin::Current = static_cast<int32>(Rpc.OriginPlayer);" + $nl +
    "`t`tFFusionRpcOrigin::MasterClient = 0;" + $nl +
    "`t`tif (FusionClient->Client && FusionClient->Client->HasRealtimeClient())" + $nl +
    "`t`t{" + $nl +
    "`t`t`tif (auto Room = FusionClient->Client->GetRealtimeClient().GetCurrentRoom())" + $nl +
    "`t`t`t{" + $nl +
    "`t`t`t`tFFusionRpcOrigin::MasterClient = Room->GetMasterClientId();" + $nl +
    "`t`t`t}" + $nl +
    "`t`t}" + $nl +
    "`t`tPair.EngineObject->ProcessEvent(FunctionDescriptor->Function, Params);" + $nl +
    "`t`tFFusionRpcOrigin::bInRpc = bWasInRpc;" + $nl +
    "`t`tFFusionRpcOrigin::Current = PreviousOrigin;" + $nl +
    "`t`tFFusionRpcOrigin::MasterClient = PreviousMaster;") "ProcessEvent in OnReceiveRPC"

[System.IO.File]::WriteAllText($Descriptor, $text, $Utf8Bom)
Write-Host "FusionTypeDescriptor.cpp patched (RPC origin)."
}

# --- Custom authentication (AUDIT B5, K10) --------------------------------------
# The wrapper connects with a random UserId and no authentication type. With
# the patch the game can hand over its login token: Photon then asks our
# photon-auth function whether the player may connect (banned players may not).
$authHeaderText = @"
// $Marker (Scripts/patch_fusion_sdk.ps1) - not part of the Photon SDK.
//
// Photon custom authentication values for the next connect. Set by the game
// before UFusionOnlineSubsystem::ConnectToPhoton; game thread only.

#pragma once

#include "CoreMinimal.h"

struct PHOTONFUSION_API FFusionCustomAuth
{
	/** Send Custom authentication with the values below. */
	static bool bEnabled;
	/** The player's id, as the auth server will confirm it. */
	static FString UserId;
	/** URL query form, e.g. "token=...". Sent to the auth server. */
	static FString Parameters;
};
"@
[System.IO.File]::WriteAllText($AuthHeader, ($authHeaderText -replace "`r?`n", "`r`n"), $Utf8Bom)

$connectText = [System.IO.File]::ReadAllText($Connect)
if ($connectText.Contains($Marker)) {
    Write-Host "FusionConnectToPhotonAsync.cpp is already patched."
} else {
    Copy-Item $Connect (Join-Path $Backup "FusionConnectToPhotonAsync.cpp") -Force
    $connectText = Replace-Once $connectText '#include "Actions/FusionConnectToPhotonAsync\.h"' (
        '$0' + $nl + '#include "FusionCustomAuth.h" // ' + $Marker + $nl + $nl +
        "// ${Marker}: custom authentication values (FusionCustomAuth.h)." + $nl +
        'bool FFusionCustomAuth::bEnabled = false;' + $nl +
        'FString FFusionCustomAuth::UserId;' + $nl +
        'FString FFusionCustomAuth::Parameters;') "the connect action include"
    $connectText = Replace-Once $connectText 'RealtimeConnectOptions\.Auth\.UserId = [^;]*;' (
        '$0' + $nl +
        "`t// ${Marker}: log in with the game's token when it has one." + $nl +
        "`tif (FFusionCustomAuth::bEnabled && !FFusionCustomAuth::UserId.IsEmpty())" + $nl +
        "`t{" + $nl +
        "`t`tRealtimeConnectOptions.Auth.Type = PhotonMatchmaking::CustomAuthenticationType::Custom;" + $nl +
        "`t`tRealtimeConnectOptions.Auth.UserId = reinterpret_cast<const PhotonCommon::CharType*>(StringCast<UTF8CHAR>(*FFusionCustomAuth::UserId).Get());" + $nl +
        "`t`tRealtimeConnectOptions.Auth.Parameters = reinterpret_cast<const PhotonCommon::CharType*>(StringCast<UTF8CHAR>(*FFusionCustomAuth::Parameters).Get());" + $nl +
        "`t}") "the connect UserId"
    [System.IO.File]::WriteAllText($Connect, $connectText, $Utf8Bom)
    Write-Host "FusionConnectToPhotonAsync.cpp patched (custom authentication)."
}
Write-Host "Photon Fusion SDK patch complete (backups in .cs-patch-backup)."
