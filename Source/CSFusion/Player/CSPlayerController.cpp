// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Player/CSPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Characters/CSCharacter.h"
#include "Core/CSAuthority.h"
#include "Core/CSLog.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Multiplayer/CSSessionSubsystem.h"

ACSPlayerController::ACSPlayerController()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;
}

UCSSessionSubsystem* ACSPlayerController::GetSessionSubsystem() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UCSSessionSubsystem>() : nullptr;
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
