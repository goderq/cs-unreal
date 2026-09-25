// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Who sent the Fusion RPC being handled (docs/AUDIT.md A1).
//
// Photon Fusion delivers an RPC on the object it was sent to, and any peer in
// the room may send an RPC to any object: "this pawn belongs to player 3" does
// not mean "player 3 sent this". The patched SDK (Scripts/patch_fusion_sdk.ps1)
// records the sender carried by the packet in FFusionRpcOrigin while the
// handler runs, and every handler checks it:
//
//   request  (client -> Master Client, on the client's own pawn)
//            -> CSRpcGuard::FromOwner(this, ...)
//   event    (Master Client -> everyone)
//            -> CSRpcGuard::FromMasterClient(this, ...)
//
// A handler called directly - offline, by a bot, by the authority itself - is
// not an RPC and passes. A refused RPC is logged (LogCSSecurity) and counts as
// a strike against the sender.

#pragma once

#include "CoreMinimal.h"

#if !__has_include("FusionRpcOrigin.h")
	#error "The Photon Fusion SDK is not patched: run Scripts/patch_fusion_sdk.ps1 once (docs/AUDIT.md A1)."
#endif

class AActor;

namespace CSRpcGuard
{
	/** Photon id of the sender of the RPC being handled; 0 when the handler was called directly. */
	CSFUSION_API int32 Sender();

	/** True while a Fusion RPC handler runs (false for a direct call). */
	CSFUSION_API bool InRpc();

	/** A request on Actor: only the peer that owns Actor may send it. */
	CSFUSION_API bool FromOwner(const AActor* Actor, const TCHAR* RpcName);

	/** An event: only the room's current Master Client may send it. */
	CSFUSION_API bool FromMasterClient(const UObject* Context, const TCHAR* RpcName);

#if !UE_BUILD_SHIPPING
	/** Self-tests only: the next RPC this peer sends claims to come from PlayerId. */
	CSFUSION_API void DebugForgeNextSender(int32 PlayerId);
#endif
}
