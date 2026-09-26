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
// Every weapon is carried through an item (UCSItemDefinition::Weapon). v2.0
// adds the knife and the grenades as kinds of weapon, so the hands, the HUD
// and the fire gate treat everything a player can hold the same way.

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
	Rifle	UMETA(DisplayName = "Rifle"),
	/** v2.0: one-handed, blade forward. */
	Knife	UMETA(DisplayName = "Knife"),
	/** v2.0: one-handed, the grenade in the right hand. */
	Grenade	UMETA(DisplayName = "Grenade")
};

/** v2.0: what pulling the trigger does. */
UENUM(BlueprintType)
enum class ECSWeaponKind : uint8
{
	Firearm	UMETA(DisplayName = "Firearm"),
	Knife	UMETA(DisplayName = "Knife"),
	Grenade	UMETA(DisplayName = "Grenade")
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

	/** Firearm, knife or grenade. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	ECSWeaponKind Kind = ECSWeaponKind::Firearm;

	// --- Knife (Kind == Knife) ----------------------------------------------

	/** How far the blade reaches, from the eyes, in cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Knife", meta = (ClampMin = "30.0"))
	float MeleeRange = 110.f;

	/** Slash (left mouse); BaseDamage is not used by the knife. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Knife", meta = (ClampMin = "1.0"))
	float MeleeDamage = 40.f;

	/** Stab (right mouse): slower and harder. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Knife", meta = (ClampMin = "1.0"))
	float MeleeHeavyDamage = 65.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Knife", meta = (ClampMin = "0.1"))
	float MeleeInterval = 0.45f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Knife", meta = (ClampMin = "0.1"))
	float MeleeHeavyInterval = 1.1f;

	/** Hits from behind (victim facing away) are multiplied by this. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Knife", meta = (ClampMin = "1.0"))
	float BackstabMultiplier = 2.5f;

	bool IsFirearm() const { return Kind == ECSWeaponKind::Firearm; }
	bool IsKnife() const { return Kind == ECSWeaponKind::Knife; }
	bool IsGrenade() const { return Kind == ECSWeaponKind::Grenade; }

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
	 * The most spare rounds this weapon carries. A bought weapon comes with a
	 * full magazine and this many spare; ammo machines top it back up.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "0"))
	int32 ReserveAmmo = 90;

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

	/**
	 * Recoil pattern (v2.0, B8): how far each shot of a series climbs, degrees.
	 * The authority applies the pattern to the bullet; the owner's view follows
	 * it and recovers once the trigger is released, so the crosshair shows
	 * where the next bullet goes and holding a spray on target means pulling
	 * the mouse down - as in CS. See CSShotModel.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling")
	float RecoilPitch = 0.6f;

	/** Sideways sway of the pattern once the climb is under way, degrees. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling")
	float RecoilYaw = 0.18f;

	/** Shots over which the pattern climbs; later shots stay at the top and sway. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "1", ClampMax = "30"))
	int32 RecoilPatternShots = 8;

	/** Seconds for a full series to recover once the trigger is released. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "0.05"))
	float RecoilRecoverySeconds = 0.45f;

	/** Extra cone half-angle at full running speed, degrees (B8). Walking slowly costs little. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "0.0"))
	float MoveSpreadDegrees = 3.f;

	/** Extra cone half-angle while in the air, degrees (B8). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "0.0"))
	float JumpSpreadDegrees = 8.f;

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

	/** v2.0 phase 3: seconds to raise the sights (the look only; the authority's aim rule is MinAimSeconds). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation", meta = (ClampMin = "0.05", ClampMax = "0.6"))
	float AimSeconds = 0.16f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	FLinearColor TracerColor = FLinearColor(1.f, 0.75f, 0.35f);

	// --- Derived -----------------------------------------------------------

	/** Seconds between shots. The authority validates fire rate against this. */
	UFUNCTION(BlueprintPure, Category = "CS|Weapon")
	float GetFireInterval() const
	{
		return 60.f / FMath::Max(1.f, RoundsPerMinute);
	}

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
