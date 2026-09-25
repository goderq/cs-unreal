// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Project-wide combat tuning, editable at Project Settings > Game > CS Combat
// and stored in DefaultGame.ini.
//
// These are the numbers the AUTHORITY validates against, so they must be the
// same on every peer. They live in config rather than on a Blueprint so that a
// modified client cannot ship different limits: the Master Client reads its
// own copy and ignores whatever the requester believes.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CSCombatSettings.generated.h"

class UCSWeaponDefinition;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "CS Combat"))
class CSFUSION_API UCSCombatSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	static const UCSCombatSettings* Get() { return GetDefault<UCSCombatSettings>(); }

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Player", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Player", meta = (ClampMin = "0.0"))
	float MaxArmor = 100.f;

	/**
	 * Share of incoming damage absorbed by armor while armor remains.
	 * 0.5 means half the damage hits armor and half hits health.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Player", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ArmorAbsorptionRatio = 0.5f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Player", meta = (ClampMin = "0.0"))
	float RespawnDelaySeconds = 3.f;

	// --- Anti-cheat tolerances ---------------------------------------------

	/**
	 * How far a client-reported muzzle position may sit from where the
	 * authority believes that pawn is, before the shot is rejected.
	 *
	 * Has to absorb honest latency and interpolation, so it cannot be tight.
	 * It exists to stop teleport-shooting, not to be a precise check.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Anti-Cheat", meta = (ClampMin = "0.0"))
	float MaxFireOriginDeviation = 400.f;

	/**
	 * Fire rate (B15): a budget of shots per player. It refills at the
	 * weapon's rate, so the average never exceeds its rounds per minute, and
	 * holds up to 1 + FireJitterSeconds / interval shots (at most 2): two
	 * honest shots that the network delivered close together both count,
	 * while a slow weapon still cannot fire twice in a row.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Anti-Cheat", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FireJitterSeconds = 0.2f;

	/** Log every rejected request. Noisy, but the only way to see cheating. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Anti-Cheat")
	bool bLogRejections = true;

	// --- Match ---------------------------------------------------------------

	/** Hard cap on tracked players. Fusion networked arrays cap at 64. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Match", meta = (ClampMin = "2", ClampMax = "64"))
	int32 MaxTrackedPlayers = 16;

	// --- v1.1 grenade ---

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Grenade")
	float GrenadeFuseSeconds = 1.6f;

	/** Throw speed along the view, cm/s (plus a little lift and the thrower's own motion). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Grenade")
	float GrenadeThrowSpeed = 1450.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Grenade")
	float GrenadeRadius = 500.f;

	/** Damage at the centre; falls off to nothing at the radius, and walls block it. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Grenade")
	float GrenadeMaxDamage = 98.f;

	/** Seconds between two throws by one player (the authority enforces it). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Grenade")
	float GrenadeThrowInterval = 0.9f;

	// --- v2.0 flashbang ---

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Flashbang")
	float FlashFuseSeconds = 1.5f;

	/** Beyond this distance a flashbang does nothing. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Flashbang", meta = (ClampMin = "100.0"))
	float FlashRadius = 2200.f;

	/** Blindness from a flash right in the face. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Flashbang", meta = (ClampMin = "0.5"))
	float FlashMaxSeconds = 4.5f;
};
