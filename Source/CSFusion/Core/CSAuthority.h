// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// THE authority facade for the whole project.
//
// Photon Fusion 3 is a shared-authority (distributed authority) SDK: there is
// no Unreal instance acting as a server. Every replicated actor has an owning
// *client*, and only that client's writes propagate. One peer per room is
// elected Master Client; it has no extra simulation power, but Fusion gives it
// automatic ownership of the GameState and it is the only stable, single,
// room-wide arbiter available.
//
// This project therefore designates the Master Client as "the server". Every
// gameplay mutation that must not be forgeable - damage, ammo, inventory,
// pickup grants, loot spawning, death, respawn, bot simulation, match state -
// is validated and written ONLY where CSAuthority::IsGameAuthority() is true.
//
// Clients never write that state. They send a *request* (Fusion RPC targeted
// at the Master Client), the authority validates it, and the result comes back
// as replicated property state.
//
// Everything funnels through this one header so that swapping the backend to a
// real UE dedicated server later (ECSAuthorityBackend::DedicatedServer) is a
// change to CSAuthority.cpp, not to gameplay code.

#pragma once

#include "CoreMinimal.h"
#include "Core/CSCoreTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CSAuthority.generated.h"

class UFusionOnlineSubsystem;

UCLASS()
class CSFUSION_API UCSAuthority : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Which backend is currently arbitrating gameplay. */
	UFUNCTION(BlueprintPure, Category = "CS|Authority", meta = (WorldContext = "WorldContextObject"))
	static ECSAuthorityBackend GetBackend(const UObject* WorldContextObject);

	/**
	 * True on the single peer allowed to mutate authoritative gameplay state.
	 *
	 * - Offline          -> always true.
	 * - Fusion           -> true only on the elected Master Client.
	 * - DedicatedServer  -> true only where ROLE_Authority holds (Stage 8+).
	 *
	 * This is the gate for ALL server-side logic. If you are about to write
	 * health, ammo, inventory, score or spawn loot, guard it with this.
	 */
	UFUNCTION(BlueprintPure, Category = "CS|Authority", meta = (WorldContext = "WorldContextObject"))
	static bool IsGameAuthority(const UObject* WorldContextObject);

	/**
	 * True when this peer may write the replicated properties of Actor.
	 *
	 * Under Fusion this is per-actor ownership (equivalent to
	 * UFusionOnlineSubsystem::CanModify and to AActor::HasAuthority once the
	 * object handshake has completed). Use this for actor-local writes such as
	 * a player driving their own pawn; use IsGameAuthority for game rules.
	 */
	UFUNCTION(BlueprintPure, Category = "CS|Authority")
	static bool CanWrite(const AActor* Actor);

	/**
	 * Photon player id of the peer that OWNS this actor, as resolved by Fusion
	 * itself - not by anything the actor stores.
	 *
	 * This is the only trustworthy way for the authority to answer "who sent
	 * this?". A Fusion RPC is delivered on the same networked object it was
	 * sent from, so the receive handler can call this on `this` and learn the
	 * real sender. Never authorise anything using a player id carried in the
	 * RPC payload or stored on the PlayerState: both are client-written.
	 *
	 * Returns 0 offline or when the actor is not networked.
	 */
	UFUNCTION(BlueprintPure, Category = "CS|Authority")
	static int32 GetOwningPlayerId(const AActor* Actor);

	/** True while a Fusion session is live and this peer is inside a room. */
	UFUNCTION(BlueprintPure, Category = "CS|Authority", meta = (WorldContext = "WorldContextObject"))
	static bool IsSessionActive(const UObject* WorldContextObject);

	/**
	 * Stable per-session id of the local player, as assigned by Photon.
	 * Returns 0 offline. This - not a client-supplied value - is what the
	 * authority uses to attribute requests to a player.
	 */
	UFUNCTION(BlueprintPure, Category = "CS|Authority", meta = (WorldContext = "WorldContextObject"))
	static int32 GetLocalPlayerId(const UObject* WorldContextObject);

	/** Number of peers currently in the room (1 offline). */
	UFUNCTION(BlueprintPure, Category = "CS|Authority", meta = (WorldContext = "WorldContextObject"))
	static int32 GetRoomPlayerCount(const UObject* WorldContextObject);

	/** Round-trip time to the Photon relay in milliseconds (0 offline). */
	UFUNCTION(BlueprintPure, Category = "CS|Authority", meta = (WorldContext = "WorldContextObject"))
	static int32 GetRttMs(const UObject* WorldContextObject);

	/**
	 * Fusion's synchronised room time in seconds. Every authority-side timing
	 * check (fire rate, pickup cooldowns, respawn timers) must use this rather
	 * than local world time, so all peers agree on "when".
	 */
	UFUNCTION(BlueprintPure, Category = "CS|Authority", meta = (WorldContext = "WorldContextObject"))
	static double GetNetworkTimeSeconds(const UObject* WorldContextObject);

	/** Native accessor for the Fusion subsystem. Null offline. */
	static UFusionOnlineSubsystem* GetFusion(const UObject* WorldContextObject);
};

/**
 * Drop-in guard for authority-only blocks in native code.
 *
 *     void ACSGameMode::AwardKill(...)
 *     {
 *         CS_AUTHORITY_ONLY(this);
 *         ...
 *     }
 */
#define CS_AUTHORITY_ONLY(WorldContextObj)                                        \
	if (!UCSAuthority::IsGameAuthority(WorldContextObj))                          \
	{                                                                             \
		UE_LOG(LogCSAuth, Verbose,                                                \
			TEXT("%hs: skipped on non-authority peer."), __FUNCTION__);           \
		return;                                                                   \
	}

#define CS_AUTHORITY_ONLY_RET(WorldContextObj, ReturnValue)                       \
	if (!UCSAuthority::IsGameAuthority(WorldContextObj))                          \
	{                                                                             \
		UE_LOG(LogCSAuth, Verbose,                                                \
			TEXT("%hs: skipped on non-authority peer."), __FUNCTION__);           \
		return ReturnValue;                                                       \
	}
