// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Adds sprint on top of UCharacterMovementComponent.
//
// Sprint is implemented as a movement-mode-independent speed multiplier driven
// by a bool that is packed into the standard saved-move/compressed-flags
// pipeline. That matters because Fusion relies on Unreal's built-in character
// networking for ACharacter-derived actors: keeping sprint inside FSavedMove_
// means the prediction, correction and replay paths keep working unchanged,
// instead of bolting on a separately replicated variable that would desync
// during a correction.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "CSCharacterMovementComponent.generated.h"

UCLASS()
class CSFUSION_API UCSCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UCSCharacterMovementComponent();

	virtual float GetMaxSpeed() const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;

	/** Called from the owning character's input handler. */
	void SetSprintInput(bool bNewSprint) { bWantsToSprint = bNewSprint; }
	bool IsSprintInputHeld() const { return bWantsToSprint; }

	/** True when sprint conditions are actually met (grounded, moving forward). */
	UFUNCTION(BlueprintPure, Category = "CS|Movement")
	bool IsSprinting() const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Movement")
	float WalkSpeed = 420.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Movement")
	float SprintSpeed = 620.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Movement")
	float CrouchSpeed = 220.f;

	/**
	 * Dot product between view forward and velocity required to sprint.
	 * Stops backwards/strafe sprinting the way competitive shooters do.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Movement", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float SprintForwardDotThreshold = 0.5f;

protected:
	/** Mirrors the sprint key. Packed into compressed flags, never replicated on its own. */
	bool bWantsToSprint = false;

	friend class FSavedMove_CSCharacter;
};

/** Saved move that carries the sprint intent through prediction and replay. */
class FSavedMove_CSCharacter : public FSavedMove_Character
{
	using Super = FSavedMove_Character;

public:
	virtual void Clear() override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel,
		class FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* Character) override;

	uint8 bSavedWantsToSprint : 1 = 0;
};

class FNetworkPredictionData_Client_CSCharacter : public FNetworkPredictionData_Client_Character
{
	using Super = FNetworkPredictionData_Client_Character;

public:
	explicit FNetworkPredictionData_Client_CSCharacter(const UCharacterMovementComponent& ClientMovement);
	virtual FSavedMovePtr AllocateNewMove() override;
};
