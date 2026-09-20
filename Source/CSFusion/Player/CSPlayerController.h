// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Under Fusion each client spawns and possesses its own PlayerController
// locally through the standard HandleStartingNewPlayer / RestartPlayer chain,
// so this class is purely a local-player object: input mode, view setup, and
// the bridge between the session layer and the UI.
//
// It never carries authoritative gameplay state.

#pragma once

#include "CoreMinimal.h"
#include "Core/CSCoreTypes.h"
#include "GameFramework/PlayerController.h"
#include "CSPlayerController.generated.h"

class UCSSessionSubsystem;
class UUserWidget;

UCLASS()
class CSFUSION_API ACSPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACSPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;

	/** Switch between game input and UI input without duplicating boilerplate. */
	UFUNCTION(BlueprintCallable, Category = "CS|UI")
	void SetUIInputMode(bool bUIMode, UUserWidget* FocusWidget = nullptr);

	UFUNCTION(BlueprintPure, Category = "CS|Session")
	UCSSessionSubsystem* GetSessionSubsystem() const;

protected:
	UFUNCTION()
	void HandleSessionStateChanged(ECSSessionState NewState);

	UFUNCTION()
	void HandleMasterClientChanged(bool bIsLocalPlayerMaster);

	/** `cs.netinfo` - prints session diagnostics to the log and screen. */
	UFUNCTION(Exec)
	void CSNetInfo();
};
