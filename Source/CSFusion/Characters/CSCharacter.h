// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// First-person player character.
//
// Mesh split, as required by the brief:
//   FirstPersonMesh - arms, attached to the camera, visible ONLY to the owner.
//   GetMesh()       - full third-person body, visible to everyone EXCEPT the
//                     owner, but still casting a shadow for the owner so the
//                     player sees their own silhouette.
//
// Networking: the pawn carries a UFusionActorComponent with PlayerAttached
// ownership, so the controlling client owns it (responsive input, local
// prediction through Unreal's built-in character networking, which Fusion
// relies on for ACharacter-derived actors) and Fusion destroys it when that
// player leaves the room.
//
// Because the pawn is client-owned, NOTHING a cheater must not control lives
// on it: no health, no ammo, no inventory. Those live on the
// Master-Client-owned ACSMatchDirector (Stage 2). This class only carries
// presentation state that is harmless to forge - stance and view pitch.

#pragma once

#include "CoreMinimal.h"
#include "Core/CSCoreTypes.h"
#include "Core/CSFusionCompat.h"
#include "GameFramework/Character.h"
#include "CSCharacter.generated.h"

class UCameraComponent;
class UCSCharacterMovementComponent;
class UCSInputConfig;
class UInputComponent;
class USkeletalMeshComponent;
class USpringArmComponent;
class UFusionActorComponent;
struct FInputActionValue;

UCLASS(Config = Game)
class CSFUSION_API ACSCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	explicit ACSCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Arms rig. Owner-only visibility. */
	UFUNCTION(BlueprintPure, Category = "CS|Character")
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }

	UFUNCTION(BlueprintPure, Category = "CS|Character")
	UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }

	UFUNCTION(BlueprintPure, Category = "CS|Character")
	UCSCharacterMovementComponent* GetCSMovement() const;

	UFUNCTION(BlueprintPure, Category = "CS|Character")
	ECSStanceState GetStance() const { return Stance; }

	/** Replicated view pitch in degrees, for aiming the third-person body. */
	UFUNCTION(BlueprintPure, Category = "CS|Character")
	float GetRemoteViewPitchDegrees() const;

	/** True when this pawn is the one the local player is looking through. */
	UFUNCTION(BlueprintPure, Category = "CS|Character")
	bool IsLocalFirstPersonView() const;

protected:
	// --- Input handlers ----------------------------------------------------
	void Input_Move(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_JumpStart(const FInputActionValue& Value);
	void Input_JumpStop(const FInputActionValue& Value);
	void Input_SprintStart(const FInputActionValue& Value);
	void Input_SprintStop(const FInputActionValue& Value);
	void Input_CrouchToggle(const FInputActionValue& Value);
	void Input_CrouchRelease(const FInputActionValue& Value);

	/** Push the mapping context onto the local player. Safe to call twice. */
	void ApplyInputMappings();

	/** Owner-only vs remote mesh visibility. Re-run whenever possession changes. */
	void RefreshMeshVisibility();

	/** Recompute Stance from the movement component (owning client only). */
	void UpdateStance();

	/**
	 * Bound to UFusionActorComponent::OnObjectReady / OnOwnerChanged, both of
	 * type FFusionObjectStatusChange (no parameters).
	 *
	 * These must stay plain, unguarded UFUNCTIONs. UHT skips unrecognised #if
	 * blocks entirely, and an unregistered UFUNCTION cannot be bound with
	 * AddDynamic - it would compile and then fail at runtime.
	 */
	UFUNCTION()
	void HandleFusionObjectReady();

	UFUNCTION()
	void HandleFusionOwnerChanged();

	// --- Components --------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CS|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CS|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> FirstPersonMesh;

	/** Bridge to UFusionClient. Unguarded so UHT keeps it GC-tracked. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CS|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UFusionActorComponent> FusionActor;

	// --- Tuning ------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Input")
	TObjectPtr<UCSInputConfig> InputConfig;

	/** Eye height above the capsule centre. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Camera")
	float CameraHeight = 64.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Camera", meta = (ClampMin = "60.0", ClampMax = "130.0"))
	float DefaultFieldOfView = 100.f;

	/** Multiplier applied to raw look input before sensitivity settings. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Camera", meta = (ClampMin = "0.01"))
	float BaseLookScale = 1.f;

	/** Crouch is hold-to-crouch when false, toggle when true. */
	UPROPERTY(Config, EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Input")
	bool bToggleCrouch = true;

	// --- Replicated presentation state -------------------------------------

	UPROPERTY(ReplicatedUsing = OnRep_Stance, BlueprintReadOnly, Category = "CS|Character")
	ECSStanceState Stance = ECSStanceState::Standing;

	UFUNCTION()
	void OnRep_Stance();

private:
	/** Set once the Fusion handshake completes; replicated reads are safe after. */
	bool bNetworkReady = false;
};
