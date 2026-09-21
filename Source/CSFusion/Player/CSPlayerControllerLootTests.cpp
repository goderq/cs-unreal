// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Stage 4 self-tests (death loot, disconnect loot, double-drop protection).
// Enabled by command-line flags only; inert in normal play. Every check reads
// authoritative state (director, Master-Client-owned inventories, pickups),
// never anything the local client decided.
//
//   -cstestgrab        pick up the nearest weapon and ammo, then stand still
//   -cstestkill        kill the other player, verify their loot drops, pick
//                      up the dropped weapon, verify they respawn clean
//   -cstestwatchleave  wait for the other player to leave, verify their loot
//                      dropped at their last position, pick it up
//   -cstestdoubledrop  authority: remove the same player twice, verify only
//                      one set of loot appears

#include "Player/CSPlayerController.h"

#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
#include "Core/CSAuthority.h"
#include "Core/CSLog.h"
#include "EngineUtils.h"
#include "Inventory/CSPlayerInventory.h"
#include "Items/CSItemDefinition.h"
#include "Items/CSItemSettings.h"
#include "Pickups/CSWorldPickup.h"
#include "TimerManager.h"
#include "Weapons/CSWeaponDefinition.h"

namespace
{
	int32 CountFilledSlots(const ACSPlayerInventory* Inventory)
	{
		int32 Filled = 0;
		if (Inventory)
		{
			for (const FCSInventorySlot& Slot : Inventory->GetSlots())
			{
				Filled += Slot.IsEmpty() ? 0 : 1;
			}
		}
		return Filled;
	}

	ACSWorldPickup* FindNearestPickup(UWorld* World, const FVector& From, TFunctionRef<bool(const ACSWorldPickup&)> Filter)
	{
		ACSWorldPickup* Best = nullptr;
		float BestDist = TNumericLimits<float>::Max();
		for (TActorIterator<ACSWorldPickup> It(World); It; ++It)
		{
			if (It->IsAvailable() && Filter(**It))
			{
				const float D = FVector::Dist(It->GetActorLocation(), From);
				if (D < BestDist)
				{
					BestDist = D;
					Best = *It;
				}
			}
		}
		return Best;
	}

	bool IsWeaponPickup(const ACSWorldPickup& P)
	{
		const UCSItemDefinition* Item = P.GetItemDefinition();
		return Item && Item->IsWeapon();
	}

	bool IsAmmoPickup(const ACSWorldPickup& P)
	{
		const UCSItemDefinition* Item = P.GetItemDefinition();
		return Item && Item->ItemType == ECSItemType::Ammo;
	}
}

void ACSPlayerController::TestWalkUpAndPress(ACSWorldPickup* Pickup)
{
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	if (!Self || !Pickup)
	{
		return;
	}
	const FVector Pos = Pickup->GetActorLocation();
	Self->SetActorLocation(FVector(Pos.X - 110.f, Pos.Y, Self->GetActorLocation().Z));
	FVector Eye, Unused;
	Self->GetAimRay(Eye, Unused);
	SetControlRotation((Pos - Eye).Rotation());

	// One frame for the focus query to see it, then E.
	GetWorldTimerManager().SetTimer(TestStepTimer, [this]() { PressKey(EKeys::E); }, 0.25f, false);
}

// ---------------------------------------------------------------------------
// -cstestgrab
// ---------------------------------------------------------------------------

void ACSPlayerController::CSTestGrab()
{
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	if (!Self)
	{
		return;
	}

	TestWalkUpAndPress(FindNearestPickup(GetWorld(), Self->GetActorLocation(), IsWeaponPickup));

	GetWorldTimerManager().SetTimer(TestLootTimer, [this]()
	{
		ACSCharacter* Me = Cast<ACSCharacter>(GetPawn());
		if (!Me)
		{
			return;
		}
		TestWalkUpAndPress(FindNearestPickup(GetWorld(), Me->GetActorLocation(), IsAmmoPickup));

		GetWorldTimerManager().SetTimer(TestLootTimer, [this]()
		{
			const ACSCharacter* Me2 = Cast<ACSCharacter>(GetPawn());
			const ACSPlayerInventory* Inv = Me2 ? ACSPlayerInventory::Find(this, Me2->GetOwningPlayerId()) : nullptr;
			UE_LOG(LogCS, Log, TEXT("GRAB TEST RESULT: player %d holds %d filled slots -> %s"),
				Me2 ? Me2->GetOwningPlayerId() : 0, CountFilledSlots(Inv),
				CountFilledSlots(Inv) >= 2 ? TEXT("GRAB OK") : TEXT("GRAB BROKEN"));
		}, 1.2f, false);
	}, 1.2f, false);
}

// ---------------------------------------------------------------------------
// -cstestkill
// ---------------------------------------------------------------------------

void ACSPlayerController::CSTestKill()
{
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	if (!Self)
	{
		return;
	}

	for (TActorIterator<ACSCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != Self)
		{
			TestVictim = *It;
			break;
		}
	}
	if (!TestVictim.IsValid())
	{
		UE_LOG(LogCS, Warning, TEXT("KILL TEST: no other player."));
		return;
	}

	TestVictimId = TestVictim->GetOwningPlayerId();
	TestVictimItemsBefore = CountFilledSlots(ACSPlayerInventory::Find(this, TestVictimId));
	TestPickupsBefore = ACSWorldPickup::CountAlive(this);
	TestShotsFired = 0;

	UE_LOG(LogCS, Log, TEXT("KILL TEST: victim %d carries %d items; world has %d pickups"),
		TestVictimId, TestVictimItemsBefore, TestPickupsBefore);

	GetWorldTimerManager().SetTimer(TestKillTimer, this, &ACSPlayerController::TestKillStep, 0.35f, true);
}

void ACSPlayerController::TestKillStep()
{
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());

	if (!Director || !Self || !TestVictim.IsValid())
	{
		GetWorldTimerManager().ClearTimer(TestKillTimer);
		return;
	}

	if (!Director->IsPlayerAlive(TestVictimId))
	{
		GetWorldTimerManager().ClearTimer(TestKillTimer);
		UE_LOG(LogCS, Log, TEXT("KILL TEST: victim %d died after %d shots"), TestVictimId, TestShotsFired);
		GetWorldTimerManager().SetTimer(TestLootTimer, this, &ACSPlayerController::TestKillVerifyLoot, 1.2f, false);
		return;
	}

	if (++TestShotsFired > 40)
	{
		GetWorldTimerManager().ClearTimer(TestKillTimer);
		UE_LOG(LogCS, Log, TEXT("DEATH LOOT RESULT: victim never died -> KILL BROKEN (hp %.0f)"),
			Director->GetHealth(TestVictimId));
		return;
	}

	FVector Eye, Unused;
	Self->GetAimRay(Eye, Unused);
	SetControlRotation((TestVictim->GetActorLocation() - Eye).Rotation());
	PressKey(EKeys::LeftMouseButton);
}

void ACSPlayerController::TestKillVerifyLoot()
{
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	const int32 ItemsLeft = CountFilledSlots(ACSPlayerInventory::Find(this, TestVictimId));
	const int32 PickupsNow = ACSWorldPickup::CountAlive(this);
	const bool bStarterKept = Director && Director->GetLoadout(TestVictimId).IsStarter();

	const bool bOk = ItemsLeft == 0 && PickupsNow == TestPickupsBefore + TestVictimItemsBefore && bStarterKept;
	UE_LOG(LogCS, Log, TEXT("DEATH LOOT RESULT: victim inventory %d -> %d, pickups %d -> %d, starter kept %s -> %s"),
		TestVictimItemsBefore, ItemsLeft, TestPickupsBefore, PickupsNow, bStarterKept ? TEXT("yes") : TEXT("no"),
		bOk ? TEXT("DEATH LOOT OK") : TEXT("DEATH LOOT BROKEN"));

	// Another player takes the dropped weapon (TZ test 6).
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	ACSWorldPickup* Dropped = Self ? FindNearestPickup(GetWorld(), Self->GetActorLocation(),
		[](const ACSWorldPickup& P) { return P.IsDropped() && IsWeaponPickup(P); }) : nullptr;

	if (!Dropped)
	{
		UE_LOG(LogCS, Log, TEXT("LOOT CLAIM RESULT: no dropped weapon found -> CLAIM BROKEN"));
		return;
	}

	TestLootItemIndex = Dropped->GetItemIndex();
	TestClaimAmmo = Dropped->GetAmmoInMag();
	TestWalkUpAndPress(Dropped);

	GetWorldTimerManager().SetTimer(TestLootTimer, [this]()
	{
		const ACSCharacter* Me = Cast<ACSCharacter>(GetPawn());
		const ACSPlayerInventory* Inv = Me ? ACSPlayerInventory::Find(this, Me->GetOwningPlayerId()) : nullptr;
		const int32 Have = Inv ? Inv->CountItem(TestLootItemIndex) : 0;
		UE_LOG(LogCS, Log, TEXT("LOOT CLAIM RESULT: took the victim's weapon (item %d, %d rounds kept) -> %s"),
			TestLootItemIndex, TestClaimAmmo, Have > 0 ? TEXT("CLAIM OK") : TEXT("CLAIM BROKEN"));
	}, 1.0f, false);

	// Respawn check after the respawn delay (TZ tests 7-8).
	GetWorldTimerManager().SetTimer(TestKillTimer, [this]()
	{
		const ACSMatchDirector* D = ACSMatchDirector::Get(this);
		FCSPlayerCombatRecord R;
		const bool bHave = D && D->GetRecord(TestVictimId, R);
		const FCSLoadoutView L = D ? D->GetLoadout(TestVictimId) : FCSLoadoutView();
		const int32 Items = CountFilledSlots(ACSPlayerInventory::Find(this, TestVictimId));
		const bool bOk = bHave && R.bAlive && R.Health >= 100.f && L.IsStarter() && L.RoundsInMag > 0 && Items == 0;
		UE_LOG(LogCS, Log, TEXT("RESPAWN RESULT: alive %s, hp %.0f, starter %s (%d rds), inventory %d -> %s"),
			(bHave && R.bAlive) ? TEXT("yes") : TEXT("no"), R.Health, L.IsStarter() ? TEXT("yes") : TEXT("no"),
			L.RoundsInMag, Items, bOk ? TEXT("RESPAWN OK") : TEXT("RESPAWN BROKEN"));
	}, 4.5f, false);
}

// ---------------------------------------------------------------------------
// -cstestwatchleave
// ---------------------------------------------------------------------------

void ACSPlayerController::CSTestWatchLeave()
{
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	for (TActorIterator<ACSCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != Self)
		{
			TestVictimId = It->GetOwningPlayerId();
			break;
		}
	}
	TestVictimItemsBefore = CountFilledSlots(ACSPlayerInventory::Find(this, TestVictimId));
	TestPickupsBefore = ACSWorldPickup::CountAlive(this);
	TestShotsFired = 0; // reused as a poll counter

	UE_LOG(LogCS, Log, TEXT("LEAVE TEST: watching player %d (carries %d items, %d pickups in world)"),
		TestVictimId, TestVictimItemsBefore, TestPickupsBefore);

	GetWorldTimerManager().SetTimer(TestKillTimer, [this]()
	{
		const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
		FCSPlayerCombatRecord Unused;
		const bool bGone = Director && !Director->GetRecord(TestVictimId, Unused);

		// Raw server view every 5 s, to measure how long Photon takes to notice.
		if (TestShotsFired % 5 == 0)
		{
			TArray<int32> Active, Inactive;
			UCSAuthority::GetRoomPlayers(this, Active, Inactive);
			FString A, I;
			for (int32 N : Active) { A += FString::Printf(TEXT("%d "), N); }
			for (int32 N : Inactive) { I += FString::Printf(TEXT("%d "), N); }
			const ACSCharacter* VictimPawn = ACSMatchDirector::FindPawnForPlayer(this, TestVictimId);
			UE_LOG(LogCS, Log, TEXT("LEAVE TEST: t+%ds room active=[%s] inactive=[%s] victimPawn=%s heartbeat=%d nettime=%.1f"),
				TestShotsFired, *A, *I, VictimPawn ? *VictimPawn->GetName() : TEXT("none"),
				VictimPawn ? VictimPawn->GetHeartbeat() : -1, UCSAuthority::GetNetworkTimeSeconds(this));
		}

		if (!bGone)
		{
			if (++TestShotsFired > 90)
			{
				GetWorldTimerManager().ClearTimer(TestKillTimer);
				UE_LOG(LogCS, Log, TEXT("LEAVE TEST RESULT: player %d never left -> LEAVE NOT DETECTED"), TestVictimId);
			}
			return;
		}

		GetWorldTimerManager().ClearTimer(TestKillTimer);
		GetWorldTimerManager().SetTimer(TestKillTimer, [this]()
		{
			const int32 PickupsNow = ACSWorldPickup::CountAlive(this);
			const bool bOk = PickupsNow == TestPickupsBefore + TestVictimItemsBefore;
			UE_LOG(LogCS, Log, TEXT("LEAVE TEST RESULT: player %d left after ~%ds; pickups %d -> %d (expected +%d) -> %s"),
				TestVictimId, TestShotsFired, TestPickupsBefore, PickupsNow, TestVictimItemsBefore,
				bOk ? TEXT("LEAVE LOOT OK") : TEXT("LEAVE LOOT BROKEN"));

			// Someone else picks up what the leaver was carrying (TZ test 11).
			ACSCharacter* Me = Cast<ACSCharacter>(GetPawn());
			ACSWorldPickup* Dropped = Me ? FindNearestPickup(GetWorld(), Me->GetActorLocation(),
				[](const ACSWorldPickup& P) { return P.IsDropped() && IsWeaponPickup(P); }) : nullptr;
			if (!Dropped)
			{
				UE_LOG(LogCS, Log, TEXT("LEAVE CLAIM RESULT: no dropped weapon -> CLAIM BROKEN"));
				return;
			}
			TestLootItemIndex = Dropped->GetItemIndex();
			TestWalkUpAndPress(Dropped);

			GetWorldTimerManager().SetTimer(TestLootTimer, [this]()
			{
				const ACSCharacter* Me2 = Cast<ACSCharacter>(GetPawn());
				const ACSPlayerInventory* Inv = Me2 ? ACSPlayerInventory::Find(this, Me2->GetOwningPlayerId()) : nullptr;
				UE_LOG(LogCS, Log, TEXT("LEAVE CLAIM RESULT: took the leaver's weapon -> %s"),
					(Inv && Inv->CountItem(TestLootItemIndex) > 0) ? TEXT("CLAIM OK") : TEXT("CLAIM BROKEN"));
			}, 1.0f, false);
		}, 1.0f, false);
	}, 1.0f, true);
}

// ---------------------------------------------------------------------------
// -cstestdoubledrop
// ---------------------------------------------------------------------------

void ACSPlayerController::CSTestDoubleDrop()
{
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	const ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	if (!Director || !Self || !UCSAuthority::IsGameAuthority(this))
	{
		UE_LOG(LogCS, Warning, TEXT("DOUBLE DROP TEST: needs the authority."));
		return;
	}

	// A synthetic player, so the test never touches a real one.
	constexpr int32 FakeId = 9001;
	Director->EnsurePlayer(FakeId);
	Director->NoteLocation(FakeId, Self->GetActorLocation() + Self->GetActorForwardVector() * 300.f);

	ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(this, FakeId);
	if (!Inventory)
	{
		UE_LOG(LogCS, Warning, TEXT("DOUBLE DROP TEST: fake player got no inventory."));
		return;
	}

	const UCSItemSettings* Items = UCSItemSettings::Get();
	Inventory->AddItem(Items->FindItemIndex(TEXT("m4")), 1, 30);
	Inventory->AddItem(Items->FindItemIndex(TEXT("ammo_rifle")), 60, 0);
	Inventory->AddItem(Items->FindItemIndex(TEXT("medkit")), 2, 0);
	const int32 Carried = CountFilledSlots(Inventory);

	const int32 Before = ACSWorldPickup::CountAlive(this);
	Director->RemovePlayer(FakeId, ECSDeathReason::Disconnected);  // leave notification
	Director->RemovePlayer(FakeId, ECSDeathReason::Disconnected);  // duplicate notification
	Director->RemovePlayer(FakeId, ECSDeathReason::Disconnected);  // sweep firing as well
	const int32 After = ACSWorldPickup::CountAlive(this);

	UE_LOG(LogCS, Log, TEXT("DOUBLE DROP TEST RESULT: carried %d, removed 3 times, pickups %d -> %d -> %s"),
		Carried, Before, After, (After - Before == Carried) ? TEXT("DOUBLE DROP OK") : TEXT("DOUBLE DROP BROKEN"));
}
