// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Player/CSPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
#include "Inventory/CSPlayerInventory.h"
#include "Items/CSItemDefinition.h"
#include "Items/CSItemSettings.h"
#include "Pickups/CSWorldPickup.h"
#include "Weapons/CSWeaponDefinition.h"
#include "Core/CSAuthority.h"
#include "Core/CSLog.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "EnhancedPlayerInput.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerInput.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/CoreDelegates.h"
#include "TimerManager.h"
#include "Multiplayer/CSSessionSubsystem.h"
#include "Settings/CSSettingsSubsystem.h"

ACSPlayerController::ACSPlayerController()
{
	bReplicates = true;

	// MUST tick. All player input is processed from the controller's tick:
	// APlayerController::TickActor -> PlayerTick -> TickPlayerInput ->
	// ProcessPlayerInput. With ticking disabled, key events still arrive and
	// are recorded, bindings and mapping contexts all look healthy, and yet
	// nothing is ever evaluated - the pawn silently ignores every key and the
	// mouse. That exact bug shipped in v0.1.0-alpha.
	PrimaryActorTick.bCanEverTick = true;
}

UCSSessionSubsystem* ACSPlayerController::GetSessionSubsystem() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UCSSessionSubsystem>() : nullptr;
}

void ACSPlayerController::InitInputSystem()
{
	Super::InitInputSystem();

	if (PlayerInput && PlayerInput->IsA<UEnhancedPlayerInput>())
	{
		return;
	}

	UE_LOG(LogCS, Warning,
		TEXT("PlayerInput was '%s', not UEnhancedPlayerInput - Enhanced Input would have been ")
		TEXT("silently inert. Replacing it. Check DefaultPlayerInputClass in DefaultInput.ini."),
		*GetNameSafe(PlayerInput ? PlayerInput->GetClass() : nullptr));

	PlayerInput = NewObject<UEnhancedPlayerInput>(this, UEnhancedPlayerInput::StaticClass());

	// The subsystem re-reads PlayerInput on every GetPlayerInput() call, so the
	// swap takes effect immediately; contexts added before this point are still
	// recorded and will now actually be evaluated.
}

void ACSPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	SetUIInputMode(false);

	if (UCSSettingsSubsystem* Settings = UCSSettingsSubsystem::Get(this))
	{
		Settings->ReapplyAudio();
	}

	if (UCSSessionSubsystem* Session = GetSessionSubsystem())
	{
		Session->OnSessionStateChanged.AddDynamic(this, &ACSPlayerController::HandleSessionStateChanged);
		Session->OnMasterClientChanged.AddDynamic(this, &ACSPlayerController::HandleMasterClientChanged);
	}

	ArmSelfTest();
}

void ACSPlayerController::PostSeamlessTravel()
{
	Super::PostSeamlessTravel();

	// Timers die with the old world, so a self-test armed before the room join
	// travel would never fire. Re-arm it in the new world.
	ArmSelfTest();
}

void ACSPlayerController::ArmSelfTest()
{
	if (!IsLocalController())
	{
		return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("cstestinput")))
	{
		GetWorldTimerManager().SetTimer(TestInputTimer, this, &ACSPlayerController::CSTestInput, 6.f, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("cstestloot")))
	{
		GetWorldTimerManager().SetTimer(TestLootTimer, this, &ACSPlayerController::CSTestLoot, 9.f, false);
	}
	struct FArm { const TCHAR* Flag; float Delay; void (ACSPlayerController::*Fn)(); };
	static const FArm Stage4Tests[] = {
		{ TEXT("cstestdoubledrop"), 8.f,  &ACSPlayerController::CSTestDoubleDrop },
		{ TEXT("cstestgrab"),       9.f,  &ACSPlayerController::CSTestGrab },
		{ TEXT("cstestwatchleave"), 20.f, &ACSPlayerController::CSTestWatchLeave },
		{ TEXT("cstestkill"),       22.f, &ACSPlayerController::CSTestKill },
	};
	for (int32 i = 0; i < UE_ARRAY_COUNT(Stage4Tests); ++i)
	{
		if (FParse::Param(FCommandLine::Get(), Stage4Tests[i].Flag))
		{
			// Member handles: re-arming after travel REPLACES the timer instead
			// of adding a second one, so each test runs once.
			GetWorldTimerManager().SetTimer(Stage4ArmTimers[i], this, Stage4Tests[i].Fn, Stage4Tests[i].Delay, false);
		}
	}

	// -cstestleave=SECONDS: leave the match through the ESC-menu path once.
	static bool bLeaveTestDone = false;
	float LeaveAfter = 0.f;
	if (!bLeaveTestDone && FParse::Value(FCommandLine::Get(), TEXT("cstestleave="), LeaveAfter) && LeaveAfter > 0.f)
	{
		bLeaveTestDone = true;
		GetWorldTimerManager().SetTimer(TestLeaveTimer, [this]()
		{
			UE_LOG(LogCS, Log, TEXT("LEAVE TEST: leaving the match via the ESC menu."));
			TogglePauseMenu();
			CloseMenus();
			if (UCSSessionSubsystem* Session = GetSessionSubsystem())
			{
				Session->LeaveToMainMenu();
			}
		}, LeaveAfter, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("cstestweapons")))
	{
		GetWorldTimerManager().SetTimer(TestWeaponsTimer, this, &ACSPlayerController::CSTestWeapons, 8.f, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("cstestround")))
	{
		GetWorldTimerManager().SetTimer(TestRoundTimer, this, &ACSPlayerController::CSTestRound, 10.f, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("cstestperf")))
	{
		// Warm-up (shader/PSO compile, bots spawning) before sampling.
		GetWorldTimerManager().SetTimer(TestPerfTimer, this, &ACSPlayerController::CSTestPerf, 15.f, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("cstestcheat")))
	{
		GetWorldTimerManager().SetTimer(TestCheatTimer, this, &ACSPlayerController::CSTestCheat, 12.f, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("cstestbots")))
	{
		// Bots start spawning 3 s after the map loads, one per second.
		GetWorldTimerManager().SetTimer(TestBotsTimer, this, &ACSPlayerController::CSTestBots, 14.f, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("cstestwalk")))
	{
		TestWalkTime = 0.f;
		GetWorldTimerManager().SetTimer(TestWalkTimer, [this]()
		{
			APawn* Walker = GetPawn();
			TestWalkTime += 0.05f;
			if (!Walker || TestWalkTime < 8.f || TestWalkTime > 60.f)
			{
				TestWalkDir = FVector::ZeroVector;
				return;
			}
			// Forward, right, back, left - 1.5 s each, relative to the view.
			const int32 Leg = FMath::FloorToInt((TestWalkTime - 8.f) / 1.5f) % 4;
			const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
			const FVector Forward = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X);
			const FVector Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
			const FVector Dir[] = { Forward, Right, -Forward, -Right };
			TestWalkDir = Dir[Leg];
		}, 0.05f, true);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("cstestvisual")))
	{
		GetWorldTimerManager().SetTimer(TestVisualTimer, this, &ACSPlayerController::CSTestVisual, 12.f, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("cstestui")))
	{
		GetWorldTimerManager().SetTimer(TestUITimer, this, &ACSPlayerController::CSTestUI, 10.f, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("cstestcontest")))
	{
		GetWorldTimerManager().SetTimer(TestContestTimer, this, &ACSPlayerController::CSTestContest, 12.f, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("cstestshoot")))
	{
		// Later than the input test, so the second client has joined.
		GetWorldTimerManager().SetTimer(TestShootTimer, this, &ACSPlayerController::CSTestShoot, 14.f, false);
	}
}

void ACSPlayerController::PressKey(const FKey& Key)
{
	const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
	FViewport* Viewport = (GetLocalPlayer() && GetLocalPlayer()->ViewportClient)
		? GetLocalPlayer()->ViewportClient->Viewport : nullptr;
	InputKey(FInputKeyEventArgs(Viewport, Device, Key, IE_Pressed, 1.f, false, FPlatformTime::Cycles64()));
	InputKey(FInputKeyEventArgs(Viewport, Device, Key, IE_Released, 0.f, false, FPlatformTime::Cycles64()));
}

bool ACSPlayerController::TestRetryUntil(bool bReady, FTimerHandle& Timer, void (ACSPlayerController::*Fn)(), const TCHAR* What)
{
	if (bReady)
	{
		TestWaitTries = 0;
		return false;
	}
	if (++TestWaitTries > 20)
	{
		UE_LOG(LogCS, Warning, TEXT("SELF-TEST: gave up waiting for %s after 40 s."), What);
		TestWaitTries = 0;
		return false;
	}
	UE_LOG(LogCS, Log, TEXT("SELF-TEST: waiting for %s..."), What);
	GetWorldTimerManager().SetTimer(Timer, this, Fn, 2.f, false);
	return true;
}

void ACSPlayerController::TestMoveTo(const FVector& Dest, TFunction<void()> OnArrived)
{
	// Hops of 400 cm every 0.5 s: 800 cm/s, under the guard's speed limit
	// (~987 cm/s over a second) and each hop under its 600 cm teleport
	// threshold. Hops rather than a smooth walk so the pawn spends almost no
	// time inside crates on the straight line, and arrives quickly.
	constexpr float Speed = 800.f;
	constexpr float Rate = 0.5f;
	TestMoveDest = Dest;
	TestMoveDone = MoveTemp(OnArrived);
	GetWorldTimerManager().SetTimer(TestMoveTimer, [this, Speed, Rate]()
	{
		APawn* Self = GetPawn();
		if (!Self)
		{
			GetWorldTimerManager().ClearTimer(TestMoveTimer);
			return;
		}
		const FVector Here = Self->GetActorLocation();
		const FVector Goal(TestMoveDest.X, TestMoveDest.Y, Here.Z);
		const FVector ToGoal = Goal - Here;
		const float Step = Speed * Rate;
		if (ToGoal.Size2D() <= Step)
		{
			Self->SetActorLocation(Goal);
			GetWorldTimerManager().ClearTimer(TestMoveTimer);
			TFunction<void()> Done = MoveTemp(TestMoveDone);
			if (Done)
			{
				Done();
			}
			return;
		}
		Self->SetActorLocation(Here + ToGoal.GetSafeNormal2D() * Step);
	}, Rate, true);
}

void ACSPlayerController::CSTestLoot()
{
	// Full Stage 3 loop through real input: walk up to a weapon, E to pick it
	// up, 2 to equip it, fire it, G to drop it. Each step is checked against
	// the authoritative inventory, not against anything local.
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	const int32 Me = Self ? Self->GetOwningPlayerId() : 0;
	if (!Self)
	{
		UE_LOG(LogCS, Warning, TEXT("LOOT TEST: no pawn."));
		return;
	}

	ACSWorldPickup* Target = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (TActorIterator<ACSWorldPickup> It(GetWorld()); It; ++It)
	{
		const UCSItemDefinition* Item = It->GetItemDefinition();
		if (It->IsAvailable() && Item && Item->IsWeapon())
		{
			const float D = FVector::Dist(It->GetActorLocation(), Self->GetActorLocation());
			if (D < BestDist)
			{
				BestDist = D;
				Target = *It;
			}
		}
	}

	if (!Target)
	{
		UE_LOG(LogCS, Warning, TEXT("LOOT TEST RESULT: no weapon pickup in the world -> PICKUPS MISSING"));
		return;
	}

	TestLootItemIndex = Target->GetItemIndex();
	UE_LOG(LogCS, Log, TEXT("LOOT TEST: target %s (item %d) at %.0f cm; world has %d pickups"),
		*Target->GetPromptName().ToString(), TestLootItemIndex, BestDist, ACSWorldPickup::CountAlive(this));

	// Walk up to it and look at it (the owning client may move its own pawn).
	const FVector Pos = Target->GetActorLocation();
	TestMoveTo(FVector(Pos.X - 120.f, Pos.Y, 0.f), [this, Me, Pos]()
	{
	ACSCharacter* Walker = Cast<ACSCharacter>(GetPawn());
	if (!Walker)
	{
		return;
	}
	FVector Eye, Unused;
	Walker->GetAimRay(Eye, Unused);
	SetControlRotation((Pos - Eye).Rotation());

	GetWorldTimerManager().SetTimer(TestLootTimer, [this, Me]()
	{
		PressKey(EKeys::E);

		GetWorldTimerManager().SetTimer(TestLootTimer, [this, Me]()
		{
			const ACSPlayerInventory* Inv = ACSPlayerInventory::Find(this, Me);
			const int32 Have = Inv ? Inv->CountItem(TestLootItemIndex) : -1;
			UE_LOG(LogCS, Log, TEXT("LOOT TEST RESULT: pickup -> %s (count %d)"),
				Have == 1 ? TEXT("PICKUP OK") : TEXT("PICKUP BROKEN"), Have);

			PressKey(EKeys::Two); // slot 1 = first inventory slot

			GetWorldTimerManager().SetTimer(TestLootTimer, [this, Me]()
			{
				const ACSMatchDirector* D = ACSMatchDirector::Get(this);
				const FCSLoadoutView L = D ? D->GetLoadout(Me) : FCSLoadoutView();
				UE_LOG(LogCS, Log, TEXT("LOOT TEST RESULT: equip -> %s (weapon %s, slot %d, rounds %d)"),
					!L.IsStarter() ? TEXT("EQUIP OK") : TEXT("EQUIP BROKEN"),
					L.Weapon ? *L.Weapon->DisplayName.ToString() : TEXT("none"), L.Slot, L.RoundsInMag);
				TestRoundsBefore = L.RoundsInMag;

				PressKey(EKeys::LeftMouseButton);

				GetWorldTimerManager().SetTimer(TestLootTimer, [this, Me]()
				{
					const ACSMatchDirector* D2 = ACSMatchDirector::Get(this);
					const FCSLoadoutView L2 = D2 ? D2->GetLoadout(Me) : FCSLoadoutView();
					UE_LOG(LogCS, Log, TEXT("LOOT TEST RESULT: fire inventory weapon -> %s (rounds %d -> %d)"),
						(!L2.IsStarter() && L2.RoundsInMag == TestRoundsBefore - 1) ? TEXT("FIRE OK") : TEXT("FIRE BROKEN"),
						TestRoundsBefore, L2.RoundsInMag);

					TestPickupsBefore = ACSWorldPickup::CountAlive(this);
					PressKey(EKeys::G);

					GetWorldTimerManager().SetTimer(TestLootTimer, [this, Me]()
					{
						const ACSPlayerInventory* Inv2 = ACSPlayerInventory::Find(this, Me);
						const int32 Left = Inv2 ? Inv2->CountItem(TestLootItemIndex) : -1;
						const int32 After = ACSWorldPickup::CountAlive(this);
						const ACSMatchDirector* D3 = ACSMatchDirector::Get(this);
						const bool bBackToStarter = D3 && D3->GetLoadout(Me).IsStarter();
						UE_LOG(LogCS, Log, TEXT("LOOT TEST RESULT: drop -> %s (in inventory %d, pickups %d -> %d, back to starter %s)"),
							(Left == 0 && After == TestPickupsBefore + 1 && bBackToStarter) ? TEXT("DROP OK") : TEXT("DROP BROKEN"),
							Left, TestPickupsBefore, After, bBackToStarter ? TEXT("yes") : TEXT("no"));
					}, 2.0f, false);
				}, 0.8f, false);
			}, 0.6f, false);
		}, 0.8f, false);
	}, 0.3f, false);
	}); // TestMoveTo
}

void ACSPlayerController::CSTestContest()
{
	// Contested pickup: every client running this goes to the same item (the
	// single sniper rifle) and presses E at the same Fusion network time.
	// Exactly one must end up with it.
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	if (!Self)
	{
		return;
	}

	const UCSItemSettings* Items = UCSItemSettings::Get();
	TestContestItem = Items->FindItemIndex(TEXT("sniper"));

	ACSWorldPickup* Target = nullptr;
	for (TActorIterator<ACSWorldPickup> It(GetWorld()); It; ++It)
	{
		if (It->IsAvailable() && It->GetItemIndex() == TestContestItem)
		{
			Target = *It;
			break;
		}
	}
	if (!Target)
	{
		UE_LOG(LogCS, Warning, TEXT("CONTEST TEST: sniper pickup not found."));
		return;
	}

	// Approach from opposite sides depending on player id so pawns do not overlap.
	const FVector Pos = Target->GetActorLocation();
	const float Side = (Self->GetOwningPlayerId() % 2 == 0) ? 1.f : -1.f;
	TestMoveTo(FVector(Pos.X + 110.f * Side, Pos.Y, 0.f), [this, Pos]()
	{
	ACSCharacter* Walker = Cast<ACSCharacter>(GetPawn());
	if (!Walker)
	{
		return;
	}
	FVector Eye, Unused;
	Walker->GetAimRay(Eye, Unused);
	SetControlRotation((Pos - Eye).Rotation());

	// Everyone presses at the same FIXED instant of the shared room clock.
	// Clients join and arrive at different times, so "N seconds after I
	// arrived" would not be simultaneous - an absolute network time is. 40 s
	// leaves room for both to walk over (they no longer teleport).
	const double Now = UCSAuthority::GetNetworkTimeSeconds(this);
	const double PressAt = FMath::Max(40.0, FMath::CeilToDouble((Now + 1.0) / 4.0) * 4.0);
	const float Delay = static_cast<float>(PressAt - Now);

	UE_LOG(LogCS, Log, TEXT("CONTEST TEST: pressing E at network time %.2f (in %.2fs)"), PressAt, Delay);

	GetWorldTimerManager().SetTimer(TestContestTimer, [this]()
	{
		PressKey(EKeys::E);
		GetWorldTimerManager().SetTimer(TestContestTimer, [this]()
		{
			const ACSCharacter* Me = Cast<ACSCharacter>(GetPawn());
			const ACSPlayerInventory* Inv = Me ? ACSPlayerInventory::Find(this, Me->GetOwningPlayerId()) : nullptr;
			const int32 Have = Inv ? Inv->CountItem(TestContestItem) : -1;

			int32 StillOnGround = 0;
			for (TActorIterator<ACSWorldPickup> It(GetWorld()); It; ++It)
			{
				if (It->IsAvailable() && It->GetItemIndex() == TestContestItem)
				{
					++StillOnGround;
				}
			}

			UE_LOG(LogCS, Log, TEXT("CONTEST TEST RESULT: player %d %s (has %d, left on ground %d)"),
				Me ? Me->GetOwningPlayerId() : 0, Have > 0 ? TEXT("WON") : TEXT("LOST"), Have, StillOnGround);
		}, 1.5f, false);
	}, FMath::Max(0.05f, Delay), false);
	}); // TestMoveTo
}

void ACSPlayerController::CSTestShoot()
{
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	if (!Self)
	{
		UE_LOG(LogCS, Warning, TEXT("SHOOT TEST: no pawn."));
		return;
	}

	// Find another player's pawn. Its presence at all proves remote pawn
	// replication; hitting it proves the authority's trace sees it.
	ACSCharacter* Target = nullptr;
	for (TActorIterator<ACSCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != Self)
		{
			Target = *It;
			break;
		}
	}

	if (TestRetryUntil(Target != nullptr, TestShootTimer, &ACSPlayerController::CSTestShoot, TEXT("the other player")))
	{
		return;
	}
	if (!Target)
	{
		UE_LOG(LogCS, Warning, TEXT("SHOOT TEST: no other player found -> REMOTE PAWN MISSING"));
		return;
	}

	TestShootVictimId = Target->GetOwningPlayerId();
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	TestShootVictimHpBefore = Director ? Director->GetHealth(TestShootVictimId) : -1.f;

	// Spawn points can put a crate between the two players. Then walk up to
	// 2.5 m from the target first (legally, see TestMoveTo) and start over.
	{
		FVector Eye0;
		FVector Fwd0;
		Self->GetAimRay(Eye0, Fwd0);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CSTestShootSight), false, Self);
		const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, Eye0, Target->GetActorLocation(), ECC_Visibility, Params)
			&& Hit.GetActor() != Target;
		static int32 Approaches = 0;
		if (bBlocked && Approaches++ < 2)
		{
			const FVector To = Target->GetActorLocation();
			const FVector Back = (Self->GetActorLocation() - To).GetSafeNormal2D();
			UE_LOG(LogCS, Log, TEXT("SHOOT TEST: no line of sight (blocked by %s), moving closer."), *GetNameSafe(Hit.GetActor()));
			TestMoveTo(To + Back * 250.f, [this]() { CSTestShoot(); });
			return;
		}
	}

	FVector Origin;
	FVector Unused;
	Self->GetAimRay(Origin, Unused);
	const FRotator AimAt = (Target->GetActorLocation() - Origin).Rotation();
	SetControlRotation(AimAt);

	UE_LOG(LogCS, Log, TEXT("SHOOT TEST: aiming at player %d (%.0f cm away), hp before %.0f"),
		TestShootVictimId, FVector::Dist(Origin, Target->GetActorLocation()), TestShootVictimHpBefore);

	// Aim down sights now, so the result does not depend on the hip-fire
	// spread cone: one hip shot at 6 m misses the capsule a fair share of the
	// time. Then let rotation and aim state settle before pulling the trigger.
	{
		const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
		FViewport* Viewport = (GetLocalPlayer() && GetLocalPlayer()->ViewportClient)
			? GetLocalPlayer()->ViewportClient->Viewport : nullptr;
		InputKey(FInputKeyEventArgs(Viewport, Device, EKeys::RightMouseButton, IE_Pressed, 1.f, false, FPlatformTime::Cycles64()));
	}
	GetWorldTimerManager().SetTimer(TestShootTimer, [this]()
	{
		const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
		FViewport* Viewport = (GetLocalPlayer() && GetLocalPlayer()->ViewportClient)
			? GetLocalPlayer()->ViewportClient->Viewport : nullptr;
		InputKey(FInputKeyEventArgs(Viewport, Device, EKeys::LeftMouseButton, IE_Pressed, 1.f, false, FPlatformTime::Cycles64()));
		InputKey(FInputKeyEventArgs(Viewport, Device, EKeys::LeftMouseButton, IE_Released, 0.f, false, FPlatformTime::Cycles64()));

		GetWorldTimerManager().SetTimer(TestShootTimer, [this]()
		{
			const FInputDeviceId Device2 = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
			FViewport* Viewport2 = (GetLocalPlayer() && GetLocalPlayer()->ViewportClient)
				? GetLocalPlayer()->ViewportClient->Viewport : nullptr;
			InputKey(FInputKeyEventArgs(Viewport2, Device2, EKeys::RightMouseButton, IE_Released, 0.f, false, FPlatformTime::Cycles64()));
			const ACSMatchDirector* D = ACSMatchDirector::Get(this);
			const float After = D ? D->GetHealth(TestShootVictimId) : -1.f;
			UE_LOG(LogCS, Log, TEXT("SHOOT TEST RESULT: victim %d hp %.0f -> %.0f -> %s"),
				TestShootVictimId, TestShootVictimHpBefore, After,
				(After >= 0.f && After < TestShootVictimHpBefore) ? TEXT("DAMAGE OK") : TEXT("DAMAGE BROKEN"));
		}, 1.5f, false);
	}, 0.2f, false);
}

void ACSPlayerController::CSTestInput()
{
	APawn* ControlledPawn = GetPawn();
	FViewport* Viewport = (GetLocalPlayer() && GetLocalPlayer()->ViewportClient)
		? GetLocalPlayer()->ViewportClient->Viewport
		: nullptr;

	if (!ControlledPawn)
	{
		UE_LOG(LogCS, Warning, TEXT("INPUT TEST: no pawn."));
		return;
	}

	TestStartLocation = ControlledPawn->GetActorLocation();
	TestStartRotation = GetControlRotation();

	const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();

	UE_LOG(LogCS, Log, TEXT("INPUT TEST: pressing W, start=%s yaw=%.1f viewport=%s"),
		*TestStartLocation.ToString(), TestStartRotation.Yaw, Viewport ? TEXT("yes") : TEXT("no"));

	InputKey(FInputKeyEventArgs(Viewport, Device, EKeys::W, IE_Pressed, 1.f, false, FPlatformTime::Cycles64()));

	// A burst of mouse movement over a few frames.
	for (int32 i = 0; i < 10; ++i)
	{
		InputKey(FInputKeyEventArgs(Viewport, Device, EKeys::MouseX, 10.f, 1.f / 60.f, 1, FPlatformTime::Cycles64()));
	}

	GetWorldTimerManager().SetTimer(TestInputTimer, this, &ACSPlayerController::TestInputRelease, 1.5f, false);
}

void ACSPlayerController::TestInputRelease()
{
	const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
	FViewport* Viewport = (GetLocalPlayer() && GetLocalPlayer()->ViewportClient)
		? GetLocalPlayer()->ViewportClient->Viewport
		: nullptr;

	InputKey(FInputKeyEventArgs(Viewport, Device, EKeys::W, IE_Released, 0.f, false, FPlatformTime::Cycles64()));

	const APawn* ControlledPawn = GetPawn();
	const FVector End = ControlledPawn ? ControlledPawn->GetActorLocation() : FVector::ZeroVector;
	const float Moved = FVector::Dist2D(TestStartLocation, End);
	const float Turned = FMath::Abs(FRotator::NormalizeAxis(GetControlRotation().Yaw - TestStartRotation.Yaw));

	UE_LOG(LogCS, Log, TEXT("INPUT TEST RESULT: moved %.1f cm, turned %.1f deg -> %s"),
		Moved, Turned, (Moved > 20.f) ? TEXT("MOVEMENT OK") : TEXT("MOVEMENT BROKEN"));

	// Combat leg: one trigger pull through the same input path, then check the
	// authority's record. A decremented magazine proves the whole chain:
	// input -> weapon component -> fire request -> authority validation ->
	// director state.
	const ACSCharacter* CSPawn = Cast<ACSCharacter>(GetPawn());
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	TestRoundsBefore = (CSPawn && Director) ? Director->GetStarterRoundsInMag(CSPawn->GetOwningPlayerId()) : -1;

	InputKey(FInputKeyEventArgs(Viewport, Device, EKeys::LeftMouseButton, IE_Pressed, 1.f, false, FPlatformTime::Cycles64()));
	InputKey(FInputKeyEventArgs(Viewport, Device, EKeys::LeftMouseButton, IE_Released, 0.f, false, FPlatformTime::Cycles64()));

	GetWorldTimerManager().SetTimer(TestInputTimer, this, &ACSPlayerController::TestFireCheck, 0.75f, false);
}

void ACSPlayerController::TestFireCheck()
{
	const ACSCharacter* CSPawn = Cast<ACSCharacter>(GetPawn());
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	const int32 After = (CSPawn && Director) ? Director->GetStarterRoundsInMag(CSPawn->GetOwningPlayerId()) : -1;

	UE_LOG(LogCS, Log, TEXT("FIRE TEST RESULT: rounds %d -> %d -> %s"),
		TestRoundsBefore, After, (After >= 0 && After == TestRoundsBefore - 1) ? TEXT("FIRE OK") : TEXT("FIRE BROKEN"));
}

void ACSPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseMenus();
	FCoreDelegates::OnEndFrame.Remove(TestPerfTickHandle);

	if (UCSSessionSubsystem* Session = GetSessionSubsystem())
	{
		Session->OnSessionStateChanged.RemoveDynamic(this, &ACSPlayerController::HandleSessionStateChanged);
		Session->OnMasterClientChanged.RemoveDynamic(this, &ACSPlayerController::HandleMasterClientChanged);
	}

	Super::EndPlay(EndPlayReason);
}

void ACSPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	// Gameplay bindings live on the pawn (ACSCharacter). Menu/HUD bindings are
	// added in Stage 5.
}

void ACSPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	UE_LOG(LogCS, Log, TEXT("%s possessed %s"), *GetName(), *GetNameSafe(InPawn));
}

void ACSPlayerController::SetUIInputMode(bool bUIMode, UUserWidget* FocusWidget)
{
	if (bUIMode)
	{
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		if (FocusWidget)
		{
			Mode.SetWidgetToFocus(FocusWidget->TakeWidget());
		}
		SetInputMode(Mode);
		SetShowMouseCursor(true);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
		SetShowMouseCursor(false);
	}
}

void ACSPlayerController::HandleSessionStateChanged(ECSSessionState NewState)
{
	UE_LOG(LogCSNet, Log, TEXT("[PC] session state -> %s"), *UEnum::GetValueAsString(NewState));

	if (NewState == ECSSessionState::Disconnected || NewState == ECSSessionState::Error)
	{
		// Lost the connection mid-match: there is no match to stay in, so go back
		// to the menu (a no-op if we are already on our way there).
		if (UCSSessionSubsystem* Session = GetSessionSubsystem())
		{
			CloseMenus();
			Session->LeaveToMainMenu();
		}
	}
}

void ACSPlayerController::HandleMasterClientChanged(bool bIsLocalPlayerMaster)
{
	UE_LOG(LogCSAuth, Log, TEXT("[PC] local player is %s the Master Client."),
		bIsLocalPlayerMaster ? TEXT("now") : TEXT("no longer"));
}

void ACSPlayerController::CSNetInfo()
{
	const UCSSessionSubsystem* Session = GetSessionSubsystem();

	const FString Info = FString::Printf(
		TEXT("CS NET | backend=%s | authority=%s | state=%s | room='%s' | players=%d | rtt=%dms | id=%d | nettime=%.2f"),
		*UEnum::GetValueAsString(UCSAuthority::GetBackend(this)),
		UCSAuthority::IsGameAuthority(this) ? TEXT("YES") : TEXT("no"),
		Session ? *UEnum::GetValueAsString(Session->GetSessionState()) : TEXT("<no subsystem>"),
		Session ? *Session->GetRoomName() : TEXT(""),
		UCSAuthority::GetRoomPlayerCount(this),
		UCSAuthority::GetRttMs(this),
		UCSAuthority::GetLocalPlayerId(this),
		UCSAuthority::GetNetworkTimeSeconds(this));

	UE_LOG(LogCSNet, Log, TEXT("%s"), *Info);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Cyan, Info);
	}
}
