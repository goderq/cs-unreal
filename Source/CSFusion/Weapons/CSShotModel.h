// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Where a shot goes, decided by the authority (docs/AUDIT.md B8).
//
// A shot is the direction the player aims, plus the weapon's recoil pattern
// for its place in the current series, plus a random cone whose size depends
// on aiming down sights, the series, running and being in the air. The
// Master Client keeps each player's series and aim state and reads speed and
// height from its own observations, so a client that skips its camera kick
// (no-recoil) or claims to be aiming gains nothing.
//
// The owning client runs the same model to move its view with the pattern
// and to size the crosshair; that copy is only a prediction.
//
// Pure functions of their inputs: unit-tested (CSFusion.Unit.Weapon.ShotModel).

#pragma once

#include "CoreMinimal.h"
#include "Core/CSCoreTypes.h"

class UCSWeaponDefinition;

/** One player's current series of shots. */
struct FCSSprayState
{
	/** Shots in the series (fractional while recovering). */
	float Series = 0.f;
	/** Network time of the last shot; < 0 = none yet. */
	double LastShotTime = -1.0;
};

/** What the authority knows about the shooter at the moment of a shot. */
struct FCSShooterState
{
	/** Aiming down sights, and for long enough (UCSCombatSettings::MinAimSeconds). */
	bool bAimed = false;
	/** Horizontal speed as a fraction of a full run (0 standing, 1 running). */
	float SpeedRatio = 0.f;
	bool bAirborne = false;
};

namespace CSShotModel
{
	/** The series at Now: continuous fire keeps it, a pause lets it recover. */
	CSFUSION_API float SeriesAt(const FCSSprayState& State, const UCSWeaponDefinition& Weapon, double Now);

	/** Record a shot fired at Now. */
	CSFUSION_API void CommitShot(FCSSprayState& State, const UCSWeaponDefinition& Weapon, double Now);

	/** Recoil for a shot fired with the series at Series: pitch up, yaw sway, degrees. 0 for the first shot. */
	CSFUSION_API FRotator RecoilAt(const UCSWeaponDefinition& Weapon, float Series);

	/** Cone half-angle, degrees. */
	CSFUSION_API float SpreadDegrees(const UCSWeaponDefinition& Weapon, const FCSShooterState& Shooter, float Series);

	/** Speed (cm/s) -> SpeedRatio: nothing up to a slow walk, full at a run. */
	CSFUSION_API float SpeedRatioFor(float HorizontalSpeed);

	// --- Light hitboxes (C6) --------------------------------------------------

	/** Body height for the stance, cm (standing 176, crouched 120). */
	CSFUSION_API float BodyHeight(bool bCrouched);

	/**
	 * A ray (Direction normalised) against a body standing at Feet, facing
	 * Forward: head sphere, torso capsule, legs capsule. True with the zone
	 * and distance of the nearest shape it enters within MaxDistance.
	 */
	CSFUSION_API bool TraceHitboxes(const FVector& Feet, const FVector& Forward, bool bCrouched,
		const FVector& Origin, const FVector& Direction, float MaxDistance, ECSHitZone& OutZone, float& OutDistance);

	/** Where a ray (Dir normalised) enters the capsule A-B of radius R (a sphere when A == B); < 0 = misses. */
	CSFUSION_API float RayCapsule(const FVector& Origin, const FVector& Dir, const FVector& A, const FVector& B, float R);
}
