// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Static, immutable description of a weapon. One asset per weapon type.
//
// Deliberately split from runtime state: a UCSWeaponDefinition never changes
// during a match, so it is safe for every peer to read it locally, and the
// authority validates against its OWN copy of the asset rather than anything
// the requesting client sends. Per-player mutable state (rounds in the
// magazine, last shot time) lives in the Master-Client-owned ACSMatchDirector.
//
// Stage 3 adds FItemDefinition on top of this for weapons that live in the
// inventory. The starter pistol is not one of them.

#pragma once

#include "CoreMinimal.h"
#include "Core/CSCoreTypes.h"
#include "Engine/DataAsset.h"
#include "CSWeaponDefinition.generated.h"

class USkeletalMesh;
class USoundBase;
class UTexture2D;

/** Which animation set the character uses while holding the weapon. */
UENUM(BlueprintType)
enum class ECSWeaponStance : uint8
{
	Pistol	UMETA(DisplayName = "Pistol"),
	Rifle	UMETA(DisplayName = "Rifle")
};

UCLASS(BlueprintType)
class CSFUSION_API UCSWeaponDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	// --- Identity ----------------------------------------------------------

	/** Stable id. Never reuse one; Stage 3 keys inventory entries off it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FName WeaponId = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity", meta = (MultiLine = "true"))
	FText Description;

	/**
	 * True only for the starter pistol. Enforced by the authority: a weapon
	 * with this flag is never added to, or removed from, an inventory, and is
	 * never turned into loot.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	bool bIsStarterWeapon = false;

	// --- Ballistics --------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ballistics", meta = (ClampMin = "1.0"))
	float BaseDamage = 22.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ballistics", meta = (ClampMin = "1.0"))
	float HeadshotMultiplier = 4.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ballistics", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float LimbMultiplier = 0.75f;

	/** Maximum trace length in centimetres. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ballistics", meta = (ClampMin = "100.0"))
	float Range = 12000.f;

	/** Damage is full up to this distance, then falls off linearly. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ballistics", meta = (ClampMin = "0.0"))
	float FalloffStartDistance = 2000.f;

	/** At and beyond this distance damage is MinDamageMultiplier of base. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ballistics", meta = (ClampMin = "0.0"))
	float FalloffEndDistance = 8000.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ballistics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinDamageMultiplier = 0.4f;

	/** Shots per trace. Greater than 1 makes it a shotgun. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ballistics", meta = (ClampMin = "1", ClampMax = "16"))
	int32 PelletsPerShot = 1;

	// --- Handling ----------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "1.0"))
	float RoundsPerMinute = 400.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling")
	bool bAutomatic = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "1"))
	int32 MagazineSize = 12;

	/**
	 * Reserve rounds carried. Negative means unlimited, which is what the
	 * starter pistol uses so a player is never left unable to act.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling")
	int32 ReserveAmmo = -1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "0.1"))
	float ReloadSeconds = 1.6f;

	/** Cone half-angle in degrees while hip firing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "0.0"))
	float HipSpreadDegrees = 1.6f;

	/** Cone half-angle in degrees while aiming down sights. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "0.0"))
	float AimSpreadDegrees = 0.3f;

	/** Extra spread added per shot while firing, and how fast it decays. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "0.0"))
	float SpreadPerShot = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "0.0"))
	float MaxBloomSpreadDegrees = 4.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "0.0"))
	float SpreadRecoveryPerSecond = 6.f;

	/** Per-shot view kick, degrees. Applied locally; purely cosmetic/feel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling")
	float RecoilPitch = 0.6f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling")
	float RecoilYaw = 0.18f;

	// --- Presentation -------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<USkeletalMesh> FirstPersonMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<USkeletalMesh> ThirdPersonMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<USoundBase> FireSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<USoundBase> EmptySound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<USoundBase> ReloadSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<USoundBase> EquipSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	ECSWeaponStance Stance = ECSWeaponStance::Rifle;

	/** Uniform scale of the weapon mesh in the hand (lets one mesh stand in for several guns). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation", meta = (ClampMin = "0.1"))
	float MeshScale = 1.f;

	/** Socket on the weapon mesh where muzzle flash and tracers start. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	FName MuzzleSocket = TEXT("Muzzle");

	/** Muzzle flash size multiplier (shotguns and snipers flash bigger). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation", meta = (ClampMin = "0.1"))
	float MuzzleFlashScale = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	FLinearColor TracerColor = FLinearColor(1.f, 0.75f, 0.35f);

	// --- Derived -----------------------------------------------------------

	/** Seconds between shots. The authority validates fire rate against this. */
	UFUNCTION(BlueprintPure, Category = "CS|Weapon")
	float GetFireInterval() const
	{
		return 60.f / FMath::Max(1.f, RoundsPerMinute);
	}

	UFUNCTION(BlueprintPure, Category = "CS|Weapon")
	bool HasUnlimitedReserve() const { return ReserveAmmo < 0; }

	/** Damage multiplier for a hit zone. */
	UFUNCTION(BlueprintPure, Category = "CS|Weapon")
	float GetZoneMultiplier(ECSHitZone Zone) const
	{
		switch (Zone)
		{
		case ECSHitZone::Head:	return HeadshotMultiplier;
		case ECSHitZone::Limb:	return LimbMultiplier;
		case ECSHitZone::Torso:	return 1.f;
		default:				return 1.f;
		}
	}

	/** Linear falloff between FalloffStart and FalloffEnd. */
	UFUNCTION(BlueprintPure, Category = "CS|Weapon")
	float GetDistanceMultiplier(float Distance) const
	{
		if (Distance <= FalloffStartDistance)
		{
			return 1.f;
		}
		if (Distance >= FalloffEndDistance || FalloffEndDistance <= FalloffStartDistance)
		{
			return MinDamageMultiplier;
		}

		const float Alpha = (Distance - FalloffStartDistance) / (FalloffEndDistance - FalloffStartDistance);
		return FMath::Lerp(1.f, MinDamageMultiplier, Alpha);
	}

	/** Final damage for one pellet. Used by the authority only. */
	UFUNCTION(BlueprintPure, Category = "CS|Weapon")
	float ComputeDamage(ECSHitZone Zone, float Distance) const
	{
		return BaseDamage * GetZoneMultiplier(Zone) * GetDistanceMultiplier(Distance);
	}
};
