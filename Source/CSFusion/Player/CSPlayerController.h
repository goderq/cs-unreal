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

	/**
	 * Guarantees PlayerInput is a UEnhancedPlayerInput.
	 *
	 * Enhanced Input only evaluates mappings through
	 * UEnhancedInputLocalPlayerSubsystem::GetPlayerInput(), which is
	 * Cast<UEnhancedPlayerInput>(PlayerController->PlayerInput) re-read on
	 * every call. When that cast fails the subsystem does nothing at all -
	 * AddMappingContext still records the context so HasMappingContext keeps
	 * returning true, bindings still register on the EnhancedInputComponent,
	 * and not one line of error is logged. The pawn simply never receives
	 * input.
	 *
	 * DefaultPlayerInputClass in DefaultInput.ini is supposed to set this, but
	 * relying on it means a silent total loss of input if it fails to resolve,
	 * so the class is pinned here where a failure is impossible and visible.
	 */
	virtual void InitInputSystem() override;

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

	/**
	 * Input self-test, enabled with -cstestinput.
	 *
	 * Feeds W and mouse deltas into PlayerInput through APlayerController::
	 * InputKey - the same entry the platform layer uses - so the whole Enhanced
	 * Input chain runs, but without depending on the OS giving the window
	 * focus (which SendInput-based probing cannot guarantee). Logs how far the
	 * pawn moved and how far the view turned.
	 */
	UFUNCTION(Exec)
	void CSTestInput();

	void ArmSelfTest();

	/** Two-client combat self-test, enabled with -cstestshoot. */
	UFUNCTION(Exec)
	void CSTestShoot();
	FTimerHandle TestShootTimer;
	int32 TestShootVictimId = 0;
	float TestShootVictimHpBefore = -1.f;
	virtual void PostSeamlessTravel() override;
	void TestInputRelease();
	void TestFireCheck();
	int32 TestRoundsBefore = -1;

	FTimerHandle TestInputTimer;
	FVector TestStartLocation = FVector::ZeroVector;
	FRotator TestStartRotation = FRotator::ZeroRotator;
};
