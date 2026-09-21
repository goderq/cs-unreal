// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Player/CSPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
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
#include "TimerManager.h"
#include "Multiplayer/CSSessionSubsystem.h"

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
	if (FParse::Param(FCommandLine::Get(), TEXT("cstestshoot")))
	{
		// Later than the input test, so the second client has joined.
		GetWorldTimerManager().SetTimer(TestShootTimer, this, &ACSPlayerController::CSTestShoot, 14.f, false);
	}
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

	if (!Target)
	{
		UE_LOG(LogCS, Warning, TEXT("SHOOT TEST: no other player found -> REMOTE PAWN MISSING"));
		return;
	}

	TestShootVictimId = Target->GetOwningPlayerId();
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	TestShootVictimHpBefore = Director ? Director->GetHealth(TestShootVictimId) : -1.f;

	FVector Origin;
	FVector Unused;
	Self->GetAimRay(Origin, Unused);
	const FRotator AimAt = (Target->GetActorLocation() - Origin).Rotation();
	SetControlRotation(AimAt);

	UE_LOG(LogCS, Log, TEXT("SHOOT TEST: aiming at player %d (%.0f cm away), hp before %.0f"),
		TestShootVictimId, FVector::Dist(Origin, Target->GetActorLocation()), TestShootVictimHpBefore);

	// Let the new rotation propagate a frame before pulling the trigger.
	GetWorldTimerManager().SetTimer(TestShootTimer, [this]()
	{
		const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
		FViewport* Viewport = (GetLocalPlayer() && GetLocalPlayer()->ViewportClient)
			? GetLocalPlayer()->ViewportClient->Viewport : nullptr;
		InputKey(FInputKeyEventArgs(Viewport, Device, EKeys::LeftMouseButton, IE_Pressed, 1.f, false, FPlatformTime::Cycles64()));
		InputKey(FInputKeyEventArgs(Viewport, Device, EKeys::LeftMouseButton, IE_Released, 0.f, false, FPlatformTime::Cycles64()));

		GetWorldTimerManager().SetTimer(TestShootTimer, [this]()
		{
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
		// Stage 5 replaces this with a reconnect dialog.
		SetUIInputMode(true);
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
