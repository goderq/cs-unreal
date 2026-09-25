// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Phase 2 RPC-sender self-test (-cstestspoof), run on the NON-master client B
// of a two-client room (Scripts/run_tests.ps1 -Network, scenario netspoof).
// B behaves like a modified client (docs/AUDIT.md A1):
//
//   1. sends a request on A's pawn ("switch to the knife") under its own
//      name - the Master Client must refuse it: B does not own that pawn;
//   2. sends the same request claiming to BE A (the packet's origin is
//      forged) - refused only if the Photon server stamps the real sender;
//      if this one gets through, the fallback is HMAC (AUDIT K9);
//   3. broadcasts a flashbang explosion as if it were the Master Client -
//      every peer must drop it (checked in A's log by the scenario).
//
// The result is read from the Master-Client-owned inventory of A, replicated
// to B: the equipped slot must not change.

#include "Player/CSPlayerController.h"

#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
#include "Core/CSAuthority.h"
#include "Core/CSLog.h"
#include "Core/CSRpcGuard.h"
#include "EngineUtils.h"
#include "Inventory/CSPlayerInventory.h"
#include "TimerManager.h"
#include "Weapons/CSGrenade.h"

#include <limits>

// B11 self-tests on the non-master client B (scenarios netfreeze, netremoval).
//   -cstestfreeze   the heartbeat stops for 15 s, as if the game froze: the
//                   Master Client must keep B in the match
//   -cstestremoval  B teleports three times, each time after the previous
//                   suspension ran out: the third suspension removes B from
//                   the match, and B's game leaves for the menu with the reason
void ACSPlayerController::CSTestFreeze()
{
	CS_SELF_TEST_ONLY();
#if !UE_BUILD_SHIPPING
	ACSCharacter* Mine = Cast<ACSCharacter>(GetPawn());
	if (!Mine)
	{
		GetWorldTimerManager().SetTimer(TestSpoofTimer, this, &ACSPlayerController::CSTestFreeze, 2.f, false);
		return;
	}
	Mine->DebugPauseHeartbeat(15.f);
	UE_LOG(LogCS, Log, TEXT("FREEZE TEST: heartbeat paused for 15 s."));
	GetWorldTimerManager().SetTimer(TestSpoofTimer, [this]()
	{
		const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
		FCSPlayerCombatRecord Record;
		const bool bKept = Director && Director->GetRecord(UCSAuthority::GetLocalPlayerId(this), Record);
		UE_LOG(LogCS, Log, TEXT("FREEZE TEST RESULT: after a 15 s freeze -> %s"),
			bKept ? TEXT("KEPT OK (still in the match)") : TEXT("DROPPED BROKEN (removed from the match)"));
	}, 22.f, false);
#endif
}

void ACSPlayerController::CSTestRemoval()
{
	CS_SELF_TEST_ONLY();
#if !UE_BUILD_SHIPPING
	if (ACSCharacter* Mine = Cast<ACSCharacter>(GetPawn()))
	{
		++RemovalTeleports;
		UE_LOG(LogCS, Log, TEXT("REMOVAL TEST: teleport %d."), RemovalTeleports);
		Mine->SetActorLocation(Mine->GetActorLocation() + FVector(0.f, RemovalTeleports % 2 ? 1500.f : -1500.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (RemovalTeleports < 3)
	{
		// Past the suspension (10 s), so the next teleport is a new one.
		GetWorldTimerManager().SetTimer(TestSpoofTimer, this, &ACSPlayerController::CSTestRemoval, 12.f, false);
	}
#endif
}

void ACSPlayerController::CSTestSpoof()
{
	CS_SELF_TEST_ONLY();
#if !UE_BUILD_SHIPPING
	const int32 Me = UCSAuthority::GetLocalPlayerId(this);
	ACSCharacter* Victim = nullptr;
	for (TActorIterator<ACSCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsBot() && It->GetOwningPlayerId() != Me && It->IsAliveAuthoritative())
		{
			Victim = *It;
			break;
		}
	}
	const ACSPlayerInventory* VictimInventory = Victim ? ACSPlayerInventory::Find(this, Victim->GetOwningPlayerId()) : nullptr;
	const bool bReady = VictimInventory && VictimInventory->HasItemInSlot(CSLoadout::Knife)
		&& VictimInventory->GetEquippedSlot() != CSLoadout::Knife;
	if (TestRetryUntil(bReady, TestSpoofTimer, &ACSPlayerController::CSTestSpoof, TEXT("the other player's pawn and inventory")))
	{
		return;
	}
	if (!bReady)
	{
		UE_LOG(LogCS, Log, TEXT("SPOOF TEST RESULT: no other player with a knife in the pocket -> MISSING"));
		return;
	}
	if (UCSAuthority::IsGameAuthority(this))
	{
		UE_LOG(LogCS, Log, TEXT("SPOOF TEST RESULT: this peer is the Master Client, the test needs the other one -> MISSING"));
		return;
	}

	SpoofVictimId = Victim->GetOwningPlayerId();
	SpoofVictimSlot = VictimInventory->GetEquippedSlot();
	UE_LOG(LogCS, Log, TEXT("SPOOF TEST: I am player %d; player %d holds slot %d. Asking their pawn to switch to the knife."),
		Me, SpoofVictimId, SpoofVictimSlot);

	// 1. A request on somebody else's pawn, under my own name.
	Victim->RpcRequestSlot(CSLoadout::Knife);

	GetWorldTimerManager().SetTimer(TestSpoofTimer, [this]()
	{
		const ACSPlayerInventory* Inv = ACSPlayerInventory::Find(this, SpoofVictimId);
		const int32 Now = Inv ? Inv->GetEquippedSlot() : -1;
		UE_LOG(LogCS, Log, TEXT("SPOOF TEST RESULT: request on another player's pawn -> %s (their slot %d -> %d)"),
			Now == SpoofVictimSlot ? TEXT("REJECTED OK") : TEXT("NOT STOPPED"), SpoofVictimSlot, Now);

		// 2. The same request, claiming to be the pawn's owner.
		ACSCharacter* Victim2 = nullptr;
		for (TActorIterator<ACSCharacter> It(GetWorld()); It; ++It)
		{
			if (!It->IsBot() && It->GetOwningPlayerId() == SpoofVictimId)
			{
				Victim2 = *It;
				break;
			}
		}
		if (!Victim2)
		{
			UE_LOG(LogCS, Log, TEXT("SPOOF TEST RESULT: the other pawn disappeared -> MISSING"));
			return;
		}
		SpoofVictimSlot = Now;
		CSRpcGuard::DebugForgeNextSender(SpoofVictimId);
		Victim2->RpcRequestSlot(CSLoadout::Knife);

		GetWorldTimerManager().SetTimer(TestSpoofTimer, [this]()
		{
			const ACSPlayerInventory* Inv2 = ACSPlayerInventory::Find(this, SpoofVictimId);
			const int32 Now2 = Inv2 ? Inv2->GetEquippedSlot() : -1;
			UE_LOG(LogCS, Log, TEXT("SPOOF TEST RESULT: request with a forged sender (claims to be player %d) -> %s (their slot %d -> %d)"),
				SpoofVictimId, Now2 == SpoofVictimSlot ? TEXT("REJECTED OK") : TEXT("NOT STOPPED"), SpoofVictimSlot, Now2);

			// 3. A flashbang "from the Master Client" at the other player's feet.
			if (ACSMatchDirector* Director = ACSMatchDirector::Get(this))
			{
				FVector Where = FVector::ZeroVector;
				for (TActorIterator<ACSCharacter> It(GetWorld()); It; ++It)
				{
					if (It->GetOwningPlayerId() == SpoofVictimId)
					{
						Where = It->GetActorLocation();
					}
				}
				Director->RpcGrenadeExploded(424242, static_cast<int32>(ECSGrenadeType::Flash), Where);
			}

			// 4. Numbers no real client sends (B9), on my own pawn: the authority
			// must refuse them before any range check (checked in A's log).
			if (ACSCharacter* Mine = Cast<ACSCharacter>(GetPawn()))
			{
				const double NaN = std::numeric_limits<double>::quiet_NaN();
				const double Inf = std::numeric_limits<double>::infinity();
				FVector Eye;
				FVector Dir;
				Mine->GetAimRay(Eye, Dir);
				Mine->RpcRequestFire(FVector(NaN, Eye.Y, Eye.Z), Dir, false);
				Mine->RpcRequestThrow(Eye, FVector(Inf, 0.0, 0.0));
				Mine->RpcRequestMelee(FVector(1.0e9, 0.0, 0.0), Dir, false);
			}
			UE_LOG(LogCS, Log, TEXT("SPOOF TEST: forged flashbang and NaN requests sent. SPOOF TEST: done"));
		}, 2.f, false);
	}, 2.f, false);
#endif
}
