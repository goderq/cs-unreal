// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Per-character weapon runtime.
//
// Split of responsibility, which is the whole point of the class:
//
//   UCSWeaponDefinition  immutable numbers, identical on every peer
//   ACSMatchDirector     authoritative mutable state (ammo, last shot, alive)
//   UCSWeaponComponent   local feel and the authority-side shot resolution
//
// This component holds NO authoritative state. Its local fields are cosmetic
// only - spread bloom and the predicted shot time that keeps the trigger
// responsive. Everything that decides whether a shot counted lives on the
// director and is validated by the Master Client.
//
// Why the RPCs are on ACSCharacter and not here: Fusion does support RPCs on
// subobjects of networked actors, but the character is the surface already
// proven to work end to end in this project, and keeping the wire contract on
// one class makes the trust boundary easy to audit. This component supplies
// the logic that both sides run.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/CSCoreTypes.h"
#include "CSWeaponComponent.generated.h"

class ACSCharacter;
class ACSMatchDirector;
class UCSWeaponDefinition;

/** Result of an authority-side shot resolution. */
USTRUCT(BlueprintType)
struct FCSShotResolution
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CS|Weapon")
	bool bHit = false;

	UPROPERTY(BlueprintReadOnly, Category = "CS|Weapon")
	FVector ImpactPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "CS|Weapon")
	ECSHitZone Zone = ECSHitZone::None;

	/** 0 when nothing damageable was hit. */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Weapon")
	int32 VictimPlayerId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CS|Weapon")
	float Damage = 0.f;
};

UCLASS(ClassGroup = (CS), meta = (BlueprintSpawnableComponent))
class CSFUSION_API UCSWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCSWeaponComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * The weapon currently in hand.
	 *
	 * Stage 2 always returns the starter pistol. Stage 3 returns the equipped
	 * inventory weapon when there is one, falling back to the pistol, which by
	 * design can never be lost.
	 */
	UFUNCTION(BlueprintPure, Category = "CS|Weapon")
	const UCSWeaponDefinition* GetActiveWeapon() const;

	/** The knife everybody carries (hands fallback while the loadout replicates). */
	const UCSWeaponDefinition* GetKnifeWeapon() const;

	// --- Local input (owning client) ---------------------------------------

	/** Trigger pressed. Fires immediately if local checks pass. */
	void StartFire();

	/** Trigger released. */
	void StopFire();

	/** Reload requested by the player. */
	void RequestReload();

	/** Current spread half-angle in degrees, including bloom. */
	UFUNCTION(BlueprintPure, Category = "CS|Weapon")
	float GetCurrentSpreadDegrees() const;

	UFUNCTION(BlueprintPure, Category = "CS|Weapon")
	/** False while a grenade is in hand (nothing to aim down). */
	bool IsAiming() const;

	/** With the knife in hand this is the heavy stab instead. */
	void SetAiming(bool bNewAiming);

	// --- Authority-side resolution -----------------------------------------

	/**
	 * Runs the trace and computes damage. Authority only.
	 *
	 * The direction comes from the client because only the client knows where
	 * the player aimed, but the ORIGIN used for the trace is the authority's
	 * own view of the pawn, and the client's claimed origin is only checked
	 * for plausibility by the director. That way a client cannot shoot from
	 * somewhere it is not.
	 */
	FCSShotResolution ResolveShotOnAuthority(const FVector& AuthoritativeOrigin,
		const FVector& AimDirection, const UCSWeaponDefinition* Weapon) const;

	/** Maps a hit bone to a damage zone. */
	static ECSHitZone ResolveHitZone(const FName& BoneName);

protected:
	/** Owning character, cached. */
	UPROPERTY(Transient)
	TObjectPtr<ACSCharacter> OwnerCharacter;

	/** Local, cosmetic: extra spread accumulated by firing. */
	float CurrentBloomDegrees = 0.f;

	/** Local predicted shot time, so the trigger feels immediate. */
	double LocalLastFireTime = 0.0;

	bool bTriggerHeld = false;
	bool bAiming = false;

	/** v2.0 knife: local swing gate and the request. */
	void TrySwing(bool bHeavy);
	double LocalNextSwingTime = 0.0;

	/** Attempts one shot: local gate, local effects, then the server request. */
	void TryFireOnce();

	ACSMatchDirector* GetDirector() const;
};
