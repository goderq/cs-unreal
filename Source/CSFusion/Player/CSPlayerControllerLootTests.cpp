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
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Pickups/CSWorldPickup.h"
#include "TimerManager.h"
#include "Weapons/CSWeaponDefinition.h"

namespace
{
	/** Holds or releases the aim button, so kill-test shots are not at the mercy of hip spread. */
	void SetTestAim(ACSPlayerController* PC, bool bAim)
	{
		const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
		FViewport* Viewport = (PC->GetLocalPlayer() && PC->GetLocalPlayer()->ViewportClient)
			? PC->GetLocalPlayer()->ViewportClient->Viewport : nullptr;
		PC->InputKey(FInputKeyEventArgs(Viewport, Device, EKeys::RightMouseButton,
			bAim ? IE_Pressed : IE_Released, bAim ? 1.f : 0.f, false, FPlatformTime::Cycles64()));
	}

	/** v2.0: the guns a player carries (primary, pistol) - exactly what drops on death. */
	int32 CountFilledSlots(const ACSPlayerInventory* Inventory)
	{
		int32 Guns = 0;
		if (Inventory)
		{
			Guns += Inventory->HasItemInSlot(CSLoadout::Primary) ? 1 : 0;
			Guns += Inventory->HasItemInSlot(CSLoadout::Pistol) ? 1 : 0;
		}
		return Guns;
	}

	bool HasItem(const ACSPlayerInventory* Inventory, int32 ItemIndex)
	{
		if (Inventory)
		{
			for (const FCSInventorySlot& Slot : Inventory->GetSlots())
			{
				if (!Slot.IsEmpty() && Slot.ItemIndex == ItemIndex)
				{
					return true;
				}
			}
		}
		return false;
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

}

void ACSPlayerController::TestWalkUpAndPress(ACSWorldPickup* Pickup, TFunction<void()> AfterPress)
{
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	if (!Self || !Pickup)
	{
		return;
	}
	const FVector Pos = Pickup->GetActorLocation();
	TWeakObjectPtr<ACSWorldPickup> WeakPickup(Pickup);
	TestMoveTo(FVector(Pos.X - 110.f, Pos.Y, 0.f), [this, Pos, AfterPress, WeakPickup]()
	{
		// Dead (a bot shot us on the way) or nothing in focus yet: try again
		// shortly, a few times, instead of pressing E at nothing.
		const ACSCharacter* Check = Cast<ACSCharacter>(GetPawn());
		if ((!Check || !Check->IsAliveAuthoritative() || !Check->GetFocusedPickup())
			&& WeakPickup.IsValid() && WeakPickup->IsAvailable() && ++WalkPressRetries <= 4)
		{
			UE_LOG(LogCS, Log, TEXT("TEST WALK: not ready to press E (attempt %d), retrying."), WalkPressRetries);
			GetWorldTimerManager().SetTimer(TestStepTimer, [this, WeakPickup, AfterPress]()
			{
				TestWalkUpAndPress(WeakPickup.Get(), AfterPress);
			}, 2.f, false);
			return;
		}
		WalkPressRetries = 0;
		ACSCharacter* Walker = Cast<ACSCharacter>(GetPawn());
		if (!Walker)
		{
			return;
		}
		FVector Eye, Unused;
		Walker->GetAimRay(Eye, Unused);
		SetControlRotation((Pos - Eye).Rotation());

		// One frame for the focus query to see it, then E.
		GetWorldTimerManager().SetTimer(TestStepTimer, [this, AfterPress, Pos]()
		{
			const ACSCharacter* Presser = Cast<ACSCharacter>(GetPawn());
			const ACSWorldPickup* Focused = Presser ? Presser->GetFocusedPickup() : nullptr;
			FString Blocker = TEXT("none");
			float Dot = 0.f;
			if (Presser && !Focused)
			{
				FVector Eye2, Fwd;
				Presser->GetAimRay(Eye2, Fwd);
				Dot = FVector::DotProduct(Fwd, (Pos - Eye2).GetSafeNormal());
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(CSTestSight), false, Presser);
				if (GetWorld()->LineTraceSingleByChannel(Hit, Eye2, Pos, ECC_Visibility, Params))
				{
					Blocker = FString::Printf(TEXT("%s.%s at %s"), *GetNameSafe(Hit.GetActor()),
						*GetNameSafe(Hit.GetComponent()), *Hit.ImpactPoint.ToCompactString());
				}
			}
			UE_LOG(LogCS, Log, TEXT("TEST WALK: at %s, pickup at %s, focused %s (view dot %.2f, sight blocked by %s)"),
				Presser ? *Presser->GetActorLocation().ToCompactString() : TEXT("-"), *Pos.ToCompactString(),
				Focused ? *Focused->GetName() : TEXT("nothing"), Dot, *Blocker);
			PressKey(EKeys::E);
			if (AfterPress)
			{
				GetWorldTimerManager().SetTimer(TestStepTimer, [AfterPress]() { AfterPress(); }, 1.0f, false);
			}
		}, 0.25f, false);
	});
}

// ---------------------------------------------------------------------------
// -cstestgrab
// ---------------------------------------------------------------------------

void ACSPlayerController::CSTestGrab()
{
	CS_SELF_TEST_ONLY();
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	if (!Self)
	{
		return;
	}

	// v2.0: nothing lies on the floor to grab. The authority hands every spawn
	// a primary with -testprimary=; this checks both guns arrived.
	const ACSPlayerInventory* Inv = ACSPlayerInventory::Find(this, Self->GetOwningPlayerId());
	UE_LOG(LogCS, Log, TEXT("GRAB TEST RESULT: player %d carries %d gun(s) -> %s"),
		Self->GetOwningPlayerId(), CountFilledSlots(Inv),
		CountFilledSlots(Inv) >= 2 ? TEXT("GRAB OK") : TEXT("GRAB BROKEN"));
}

// ---------------------------------------------------------------------------
// -cstestkill
// ---------------------------------------------------------------------------

void ACSPlayerController::CSTestKill()
{
	CS_SELF_TEST_ONLY();
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	if (!Self)
	{
		return;
	}

	// First alive other character; with -cstestkillbot, a bot if there is one.
	const bool bPreferBot = FParse::Param(FCommandLine::Get(), TEXT("cstestkillbot"));
	TestVictim.Reset();
	for (TActorIterator<ACSCharacter> It(GetWorld()); It; ++It)
	{
		if (*It == Self || !It->IsAliveAuthoritative())
		{
			continue;
		}
		if (!TestVictim.IsValid() || (bPreferBot && It->IsBot() && !TestVictim->IsBot()))
		{
			TestVictim = *It;
		}
	}
	// A human victim must have picked something up first (-cstestgrab), or
	// there is no loot to check.
	const bool bVictimReady = TestVictim.IsValid() && (TestVictim->IsBot()
		|| CountFilledSlots(ACSPlayerInventory::Find(this, TestVictim->GetOwningPlayerId())) > 0);
	if (TestRetryUntil(bVictimReady, Stage4ArmTimers[3], &ACSPlayerController::CSTestKill, TEXT("a victim carrying loot")))
	{
		return;
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

	// v1.1: Deathmatch spawns players far apart, often out of sight - walk up first.
	const FVector ToVictim = TestVictim->GetActorLocation() - Self->GetActorLocation();
	const FVector Dest = TestVictim->GetActorLocation() - ToVictim.GetSafeNormal2D() * 450.f;
	TestMoveTo(Dest, [this]()
	{
		SetTestAim(this, true);
		GetWorldTimerManager().SetTimer(TestKillTimer, this, &ACSPlayerController::TestKillStep, 0.35f, true);
	});
}

void ACSPlayerController::TestKillStep()
{
	CS_SELF_TEST_ONLY();
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());

	if (!Director || !Self || !TestVictim.IsValid())
	{
		GetWorldTimerManager().ClearTimer(TestKillTimer);
		SetTestAim(this, false);
		return;
	}

	if (!Director->IsPlayerAlive(TestVictimId))
	{
		GetWorldTimerManager().ClearTimer(TestKillTimer);
		SetTestAim(this, false);
		UE_LOG(LogCS, Log, TEXT("KILL TEST: victim %d died after %d shots"), TestVictimId, TestShotsFired);
		FCSPlayerCombatRecord AtDeath;
		TestVictimRespawnsAtDeath = Director->GetRecord(TestVictimId, AtDeath) ? AtDeath.RespawnCounter : 0;
		// Stage 6: the victim should be playing a death animation now.
		FTimerHandle DeathShot;
		GetWorldTimerManager().SetTimer(DeathShot, [this]() { TestScreenshot(TEXT("tp_death")); }, 0.8f, false);
		GetWorldTimerManager().SetTimer(TestLootTimer, this, &ACSPlayerController::TestKillVerifyLoot, 1.2f, false);
		return;
	}

	// 80 shots (~28 s): a bot target strafes, shoots back and heals, and the
	// shooter may die and respawn in between.
	if (++TestShotsFired > 80)
	{
		GetWorldTimerManager().ClearTimer(TestKillTimer);
		SetTestAim(this, false);
		UE_LOG(LogCS, Log, TEXT("DEATH LOOT RESULT: victim never died -> KILL BROKEN (hp %.0f)"),
			Director->GetHealth(TestVictimId));
		return;
	}

	FVector Eye, Unused;
	Self->GetAimRay(Eye, Unused);

	// The victim may have moved after the walk started (a fresh spawn is
	// placed by its own client a moment later): with a wall in between, walk
	// up again instead of emptying the magazine into it.
	FHitResult Block;
	FCollisionQueryParams Sight(SCENE_QUERY_STAT(CSTestKillSight), false, Self);
	Sight.AddIgnoredActor(TestVictim.Get());
	if (TestKillApproaches < 3 && GetWorld()->LineTraceSingleByChannel(Block, Eye, TestVictim->GetActorLocation(), ECC_Visibility, Sight))
	{
		++TestKillApproaches;
		GetWorldTimerManager().ClearTimer(TestKillTimer);
		UE_LOG(LogCS, Log, TEXT("KILL TEST: no line of sight (blocked by %s), walking up again."), *GetNameSafe(Block.GetActor()));
		const FVector To = TestVictim->GetActorLocation();
		const FVector Back = (Self->GetActorLocation() - To).GetSafeNormal2D();
		TestMoveTo(To + Back * 250.f, [this]()
		{
			GetWorldTimerManager().SetTimer(TestKillTimer, this, &ACSPlayerController::TestKillStep, 0.35f, true);
		});
		return;
	}
	SetControlRotation((TestVictim->GetActorLocation() - Eye).Rotation());
	PressKey(EKeys::LeftMouseButton);
}

void ACSPlayerController::TestKillVerifyLoot()
{
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	const int32 ItemsLeft = CountFilledSlots(ACSPlayerInventory::Find(this, TestVictimId));
	const int32 PickupsNow = ACSWorldPickup::CountAlive(this);
	// Dead: the whole loadout is gone until the respawn hands out a knife and a pistol.
	const bool bStarterKept = Director && Director->GetLoadout(TestVictimId).Weapon == nullptr;

	// A bot victim dies among other bots, and bots loot within a second: the
	// exact pickup count can only be checked for a human victim. For a bot,
	// its inventory must be empty and at least one drop must be on the floor.
	const bool bBotVictim = CSBots::IsBotId(TestVictimId);
	const bool bCountOk = bBotVictim
		? PickupsNow >= TestPickupsBefore + FMath::Min(1, TestVictimItemsBefore)
		: PickupsNow == TestPickupsBefore + TestVictimItemsBefore;
	const bool bOk = ItemsLeft == 0 && bCountOk && bStarterKept;
	UE_LOG(LogCS, Log, TEXT("DEATH LOOT RESULT: victim inventory %d -> %d, pickups %d -> %d%s, loadout emptied %s -> %s"),
		TestVictimItemsBefore, ItemsLeft, TestPickupsBefore, PickupsNow,
		bBotVictim ? TEXT(" (bot victim: other bots may loot first)") : TEXT(""), bStarterKept ? TEXT("yes") : TEXT("no"),
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
	TWeakObjectPtr<ACSWorldPickup> WeakDropped(Dropped);
	TestWalkUpAndPress(Dropped, [this, WeakDropped, bBotVictim]()
	{
		const ACSCharacter* Me = Cast<ACSCharacter>(GetPawn());
		const ACSPlayerInventory* Inv = Me ? ACSPlayerInventory::Find(this, Me->GetOwningPlayerId()) : nullptr;
		const int32 Have = HasItem(Inv, TestLootItemIndex) ? 1 : 0;
		// Gone from the floor but not in our inventory: someone else got it
		// first. That is correct behaviour (exactly one winner) - with bots
		// around it is expected, not a failure.
		const bool bTakenByOther = Have == 0 && (!WeakDropped.IsValid() || !WeakDropped->IsAvailable());
		const bool bClaimantDead = !Me || !Me->IsAliveAuthoritative();
		const TCHAR* Verdict = Have > 0 ? TEXT("CLAIM OK")
			: ((bTakenByOther && bBotVictim) ? TEXT("CLAIM LOST TO A BOT (picked up by another bot first)")
			: ((bClaimantDead && bBotVictim) ? TEXT("CLAIM NOT TRIED (a bot killed the claimant on the way)")
			: TEXT("CLAIM BROKEN")));
		UE_LOG(LogCS, Log, TEXT("LOOT CLAIM RESULT: took the victim's weapon (item %d, %d rounds kept) -> %s"),
			TestLootItemIndex, TestClaimAmmo, Verdict);
	});

	// Respawn check after the respawn delay (TZ tests 7-8).
	GetWorldTimerManager().SetTimer(TestKillTimer, [this]()
	{
		const ACSMatchDirector* D = ACSMatchDirector::Get(this);
		FCSPlayerCombatRecord R;
		const bool bHave = D && D->GetRecord(TestVictimId, R);
		const FCSLoadoutView L = D ? D->GetLoadout(TestVictimId) : FCSLoadoutView();
		const int32 Items = CountFilledSlots(ACSPlayerInventory::Find(this, TestVictimId));
		// A bot is back in a fight at once - shot again, looting again - so for a
		// bot the check is that it did respawn, with something in hand.
		const bool bBot = CSBots::IsBotId(TestVictimId);
		const ACSPlayerInventory* VictimLoadout = ACSPlayerInventory::Find(this, TestVictimId);
		// The spawn loadout this authority hands out: pistol and knife, plus a
		// primary in hand when it runs with -testprimary= (the grab scenarios).
		FString TestPrimary;
		const bool bTestPrimary = FParse::Value(FCommandLine::Get(), TEXT("testprimary="), TestPrimary);
		const bool bFresh = L.Slot == (bTestPrimary ? CSLoadout::Primary : CSLoadout::Pistol) && L.Weapon
			&& L.RoundsInMag == L.Weapon->MagazineSize && VictimLoadout && VictimLoadout->HasItemInSlot(CSLoadout::Knife);
		const bool bOk = bBot
			? (bHave && R.RespawnCounter > TestVictimRespawnsAtDeath && L.Weapon != nullptr)
			: (bHave && R.bAlive && R.Health >= 100.f && bFresh && Items == (bTestPrimary ? 2 : 1));
		UE_LOG(LogCS, Log, TEXT("RESPAWN RESULT: alive %s, hp %.0f, fresh pistol + knife %s (%d rds), guns %d -> %s"),
			(bHave && R.bAlive) ? TEXT("yes") : TEXT("no"), R.Health, bFresh ? TEXT("yes") : TEXT("no"),
			L.RoundsInMag, Items, bOk ? TEXT("RESPAWN OK") : TEXT("RESPAWN BROKEN"));
	}, 4.5f, false);
}

// ---------------------------------------------------------------------------
// -cstestwatchleave
// ---------------------------------------------------------------------------

void ACSPlayerController::CSTestWatchLeave()
{
	CS_SELF_TEST_ONLY();
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	TestVictimId = 0;
	for (TActorIterator<ACSCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != Self && !It->IsBot())
		{
			TestVictimId = It->GetOwningPlayerId();
			break;
		}
	}
	// Wait until the other player exists and has picked up its loot.
	const bool bReady = TestVictimId != 0 && CountFilledSlots(ACSPlayerInventory::Find(this, TestVictimId)) > 0;
	if (TestRetryUntil(bReady, Stage4ArmTimers[2], &ACSPlayerController::CSTestWatchLeave, TEXT("the other player's loot")))
	{
		return;
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
			TestWalkUpAndPress(Dropped, [this]()
			{
				const ACSCharacter* Me2 = Cast<ACSCharacter>(GetPawn());
				const ACSPlayerInventory* Inv = Me2 ? ACSPlayerInventory::Find(this, Me2->GetOwningPlayerId()) : nullptr;
				UE_LOG(LogCS, Log, TEXT("LEAVE CLAIM RESULT: took the leaver's weapon -> %s"),
					HasItem(Inv, TestLootItemIndex) ? TEXT("CLAIM OK") : TEXT("CLAIM BROKEN"));
			});
		}, 1.0f, false);
	}, 1.0f, true);
}

// ---------------------------------------------------------------------------
// -cstestdoubledrop
// ---------------------------------------------------------------------------

void ACSPlayerController::CSTestDoubleDrop()
{
	CS_SELF_TEST_ONLY();
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
	Director->GiveItem(FakeId, Items->FindItemIndex(TEXT("m4")), /*bEquip*/ true);
	Director->GiveItem(FakeId, Items->FindItemIndex(TEXT("grenade")), /*bEquip*/ false);
	const int32 Carried = CountFilledSlots(Inventory);

	const int32 Before = ACSWorldPickup::CountAlive(this);
	Director->RemovePlayer(FakeId, ECSDeathReason::Disconnected);  // leave notification
	Director->RemovePlayer(FakeId, ECSDeathReason::Disconnected);  // duplicate notification
	Director->RemovePlayer(FakeId, ECSDeathReason::Disconnected);  // sweep firing as well
	const int32 After = ACSWorldPickup::CountAlive(this);

	UE_LOG(LogCS, Log, TEXT("DOUBLE DROP TEST RESULT: carried %d, removed 3 times, pickups %d -> %d -> %s"),
		Carried, Before, After, (After - Before == Carried) ? TEXT("DOUBLE DROP OK") : TEXT("DOUBLE DROP BROKEN"));
}
