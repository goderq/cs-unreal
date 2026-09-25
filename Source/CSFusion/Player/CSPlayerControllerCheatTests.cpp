// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Stage 8 anti-cheat self-test (-cstestcheat). This client behaves like a
// modified one - it bypasses its own local checks and sends the authority
// requests a legitimate client never would - and verifies each is refused by
// reading the resulting AUTHORITATIVE state (director record, inventory).
// Works offline and as a non-master client in a room.

#include "Player/CSPlayerController.h"

#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
#include "Core/CSAuthority.h"
#include "Core/CSLog.h"
#include "EngineUtils.h"
#include "Inventory/CSPlayerInventory.h"
#include "Items/CSItemSettings.h"
#include "Pickups/CSWorldPickup.h"
#include "TimerManager.h"

namespace
{
	int32 Rounds(const UObject* Context, int32 PlayerId)
	{
		const ACSMatchDirector* Director = ACSMatchDirector::Get(Context);
		return Director ? Director->GetLoadout(PlayerId).RoundsInMag : -1;
	}

	void Report(const TCHAR* Name, bool bOk, const FString& Detail)
	{
		UE_LOG(LogCS, Log, TEXT("CHEAT TEST RESULT: %s -> %s (%s)"), Name, bOk ? TEXT("REJECTED OK") : TEXT("NOT STOPPED"), *Detail);
	}
}

void ACSPlayerController::CSTestCheat()
{
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	if (!Self || !Self->IsAliveAuthoritative())
	{
		UE_LOG(LogCS, Warning, TEXT("CHEAT TEST: no live pawn, retrying in 2 s."));
		GetWorldTimerManager().SetTimer(TestCheatTimer, this, &ACSPlayerController::CSTestCheat, 2.f, false);
		return;
	}
	const int32 Me = Self->GetOwningPlayerId();
	FVector Eye;
	FVector Dir;
	Self->GetAimRay(Eye, Dir);
	Dir = FVector(Dir.X, Dir.Y, -0.4f).GetSafeNormal(); // at the floor, so no one is hit

	// 1. Rate hack: ten shots in one frame, bypassing the weapon's local gate.
	CheatRoundsBefore = Rounds(this, Me);
	for (int32 i = 0; i < 10; ++i)
	{
		Self->RequestFire(Eye, Dir, false);
	}

	GetWorldTimerManager().SetTimer(TestCheatTimer, [this, Me]()
	{
		ACSCharacter* Self1 = Cast<ACSCharacter>(GetPawn());
		const int32 After = Rounds(this, Me);
		Report(TEXT("fire-rate hack (10 shots in one frame)"), CheatRoundsBefore - After <= 1,
			FString::Printf(TEXT("rounds %d -> %d"), CheatRoundsBefore, After));
		if (!Self1)
		{
			return;
		}

		// 2. Shooting from somewhere the pawn is not.
		FVector Eye1;
		FVector Dir1;
		Self1->GetAimRay(Eye1, Dir1);
		CheatRoundsBefore = After;
		Self1->RequestFire(Eye1 + FVector(2000.f, 0.f, 0.f), FVector(0.f, 0.f, -1.f), false);

		// 3. Picking up an item far away.
		CheatFarPickup = nullptr;
		for (TActorIterator<ACSWorldPickup> It(GetWorld()); It; ++It)
		{
			if (It->IsAvailable() && FVector::Dist(It->GetActorLocation(), Self1->GetActorLocation()) > 1500.f)
			{
				CheatFarPickup = *It;
				break;
			}
		}
		// v2.0: nothing lies on the maps any more; the (offline) authority puts
		// a gun down 20 m away itself.
		if (!CheatFarPickup.IsValid() && UCSAuthority::IsGameAuthority(this))
		{
			if (ACSMatchDirector* Director = ACSMatchDirector::Get(this))
			{
				FCSInventorySlot Gun;
				Gun.ItemIndex = UCSItemSettings::Get()->FindItemIndex(TEXT("m4"));
				Gun.Count = 1;
				Gun.AmmoInMag = 30;
				const FVector Far = Self1->GetActorLocation() + Self1->GetActorForwardVector() * 2000.f;
				CheatFarPickup = Director->SpawnDroppedItem(Gun, Far, Far);
			}
		}
		if (CheatFarPickup.IsValid())
		{
			if (UCSAuthority::IsSessionActive(this))
			{
				Self1->RpcRequestPickup(CheatFarPickup.Get());
			}
			else
			{
				Self1->RpcRequestPickup_Receive(CheatFarPickup.Get());
			}
		}

		GetWorldTimerManager().SetTimer(TestCheatTimer, [this, Me]()
		{
			ACSCharacter* Self2 = Cast<ACSCharacter>(GetPawn());
			const int32 After2 = Rounds(this, Me);
			Report(TEXT("fire from 20 m away"), After2 == CheatRoundsBefore,
				FString::Printf(TEXT("rounds %d -> %d"), CheatRoundsBefore, After2));
			const bool bFarTaken = CheatFarPickup.IsValid() ? !CheatFarPickup->IsAvailable() : false;
			Report(TEXT("pickup from 15+ m away"), CheatFarPickup.IsValid() && !bFarTaken,
				CheatFarPickup.IsValid() ? TEXT("pickup still on the floor") : TEXT("no far pickup found"));
			if (!Self2)
			{
				return;
			}

			// 4. Request flood: 60 slot switches in one frame.
			for (int32 i = 0; i < 60; ++i)
			{
				Self2->RequestSlot(INDEX_NONE);
			}

			// 5. Teleport: the owning client moves its own pawn, so this
			// "works" locally - the authority must notice and suspend.
			Self2->SetActorLocation(Self2->GetActorLocation() + FVector(0.f, 1500.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);

			GetWorldTimerManager().SetTimer(TestCheatTimer, [this, Me]()
			{
				ACSCharacter* Self3 = Cast<ACSCharacter>(GetPawn());
				if (!Self3)
				{
					return;
				}
				// A normal shot while suspended must be refused.
				FVector Eye3;
				FVector Dir3;
				Self3->GetAimRay(Eye3, Dir3);
				CheatRoundsBefore = Rounds(this, Me);
				Self3->RequestFire(Eye3, FVector(Dir3.X, Dir3.Y, -0.4f).GetSafeNormal(), false);

				GetWorldTimerManager().SetTimer(TestCheatTimer, [this, Me]()
				{
					const int32 After3 = Rounds(this, Me);
					Report(TEXT("teleport -> suspended, legit shot refused"), After3 == CheatRoundsBefore,
						FString::Printf(TEXT("rounds %d -> %d"), CheatRoundsBefore, After3));

					// After the suspension a normal shot works again.
					GetWorldTimerManager().SetTimer(TestCheatTimer, [this, Me]()
					{
						ACSCharacter* Self4 = Cast<ACSCharacter>(GetPawn());
						if (!Self4)
						{
							return;
						}
						FVector Eye4;
						FVector Dir4;
						Self4->GetAimRay(Eye4, Dir4);
						CheatRoundsBefore = Rounds(this, Me);
						Self4->RequestFire(Eye4, FVector(Dir4.X, Dir4.Y, -0.4f).GetSafeNormal(), false);

						GetWorldTimerManager().SetTimer(TestCheatTimer, [this, Me]()
						{
							const int32 After4 = Rounds(this, Me);
							UE_LOG(LogCS, Log, TEXT("CHEAT TEST RESULT: suspension lifted after 10 s -> %s (rounds %d -> %d)"),
								After4 == CheatRoundsBefore - 1 ? TEXT("RESTORED OK") : TEXT("STILL BLOCKED"),
								CheatRoundsBefore, After4);
						}, 1.f, false);
					}, 11.f, false);
				}, 1.f, false);
			}, 1.5f, false);
		}, 1.f, false);
	}, 1.f, false);
}
