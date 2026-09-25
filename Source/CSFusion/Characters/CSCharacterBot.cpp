// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// ACSCharacter as a bot (Stage 7): identity and the direct authority entry
// points the bot controller uses instead of RPCs.

#include "Characters/CSCharacter.h"

#include "Combat/CSCheatGuard.h"
#include "Combat/CSMatchDirector.h"
#include "Core/CSAuthority.h"
#include "Core/CSLog.h"
#include "Pickups/CSAmmoMachine.h"
#include "Pickups/CSWorldPickup.h"

void ACSCharacter::InitAsBot(int32 InBotId)
{
	// Called between SpawnActorDeferred and FinishSpawning, so the values are
	// in place before Fusion registers the object and replicates it.
	bIsBot = true;
	BotId = InBotId;
	if (FusionActor)
	{
		// Owned by the Master Client, not attached to any player: the bot
		// survives master migration and is never destroyed by a player leaving.
		FusionActor->Ownership = EFusionObjectOwnerFlags::MasterClient;
	}
}

void ACSCharacter::BotFire(const FVector& Origin, const FVector& Direction)
{
	if (!bIsBot || !UCSAuthority::IsGameAuthority(this))
	{
		return;
	}
	// Bots never aim down sights: they get the hip-fire spread cone.
	TGuardValue<bool> Scope(bBotAuthorityCall, true);
	RpcRequestFire_Receive(Origin, Direction, /*bAiming*/ false);
}

void ACSCharacter::BotReload()
{
	if (!bIsBot || !UCSAuthority::IsGameAuthority(this))
	{
		return;
	}
	TGuardValue<bool> Scope(bBotAuthorityCall, true);
	RpcRequestReload_Receive();
}

void ACSCharacter::BotPickup(ACSWorldPickup* Pickup)
{
	if (!bIsBot || !Pickup || !UCSAuthority::IsGameAuthority(this))
	{
		return;
	}
	TGuardValue<bool> Scope(bBotAuthorityCall, true);
	RpcRequestPickup_Receive(Pickup);
}

void ACSCharacter::BotSelectSlot(int32 Slot)
{
	if (!bIsBot || !UCSAuthority::IsGameAuthority(this))
	{
		return;
	}
	TGuardValue<bool> Scope(bBotAuthorityCall, true);
	RpcRequestSlot_Receive(Slot);
}

bool ACSCharacter::PassesCheatGuard(ECSRequestKind Kind) const
{
	// Bots run on the authority itself; only remote requests are policed.
	if (bIsBot)
	{
		return true;
	}
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	return !Director || Director->GuardRequest(GetOwningPlayerId(), Kind);
}

void ACSCharacter::BotMelee(const FVector& Origin, const FVector& Direction, bool bHeavy)
{
	if (!bIsBot || !UCSAuthority::IsGameAuthority(this))
	{
		return;
	}
	TGuardValue<bool> Scope(bBotAuthorityCall, true);
	ResolveMeleeOnAuthority(Origin, Direction, bHeavy);
}

void ACSCharacter::BotBuyAmmo(ACSAmmoMachine* Machine)
{
	if (!bIsBot || !Machine || !UCSAuthority::IsGameAuthority(this))
	{
		return;
	}
	TGuardValue<bool> Scope(bBotAuthorityCall, true);
	RpcRequestAmmo_Receive(Machine->GetSortedIndex());
}
