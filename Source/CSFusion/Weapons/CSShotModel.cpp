// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Weapons/CSShotModel.h"

#include "Weapons/CSWeaponDefinition.h"

namespace CSShotModel
{
	float SeriesAt(const FCSSprayState& State, const UCSWeaponDefinition& Weapon, double Now)
	{
		if (State.LastShotTime < 0.0 || State.Series <= 0.f)
		{
			return 0.f;
		}
		// Continuous fire (the next shot within 1.25 intervals) keeps the
		// series; after that it drains to zero in RecoilRecoverySeconds.
		const double Held = Weapon.GetFireInterval() * 1.25;
		const double Recovering = FMath::Max(0.0, (Now - State.LastShotTime) - Held);
		const float Rate = static_cast<float>(FMath::Max(1, Weapon.RecoilPatternShots)) / FMath::Max(0.05f, Weapon.RecoilRecoverySeconds);
		return FMath::Max(0.f, State.Series - static_cast<float>(Recovering) * Rate);
	}

	void CommitShot(FCSSprayState& State, const UCSWeaponDefinition& Weapon, double Now)
	{
		// Capped a little past the pattern so a long spray recovers in time.
		State.Series = FMath::Min(SeriesAt(State, Weapon, Now) + 1.f, static_cast<float>(Weapon.RecoilPatternShots) + 4.f);
		State.LastShotTime = Now;
	}

	FRotator RecoilAt(const UCSWeaponDefinition& Weapon, float Series)
	{
		const float S = FMath::Max(0.f, Series);
		const float Climb = Weapon.RecoilPitch * FMath::Min(S, static_cast<float>(Weapon.RecoilPatternShots));
		// Sway starts after the first few shots and swings left and right.
		const float Sway = Weapon.RecoilYaw * 3.f * FMath::Sin(S * 0.85f) * FMath::Clamp((S - 2.f) / 4.f, 0.f, 1.f);
		return FRotator(Climb, Sway, 0.f);
	}

	float SpreadDegrees(const UCSWeaponDefinition& Weapon, const FCSShooterState& Shooter, float Series)
	{
		const float Base = Shooter.bAimed ? Weapon.AimSpreadDegrees : Weapon.HipSpreadDegrees;
		const float Bloom = FMath::Min(Weapon.MaxBloomSpreadDegrees, FMath::Max(0.f, Series) * Weapon.SpreadPerShot);
		const float Move = FMath::Clamp(Shooter.SpeedRatio, 0.f, 1.f) * Weapon.MoveSpreadDegrees;
		const float Air = Shooter.bAirborne ? Weapon.JumpSpreadDegrees : 0.f;
		return Base + Bloom + Move + Air;
	}

	float SpeedRatioFor(float HorizontalSpeed)
	{
		// A careful walk (~150 cm/s) is free; a full run (~620) costs it all.
		return FMath::Clamp((HorizontalSpeed - 150.f) / (620.f - 150.f), 0.f, 1.f);
	}

	float BodyHeight(bool bCrouched)
	{
		return bCrouched ? 120.f : 176.f;
	}

	float RayCapsule(const FVector& Origin, const FVector& Dir, const FVector& A, const FVector& B, float R)
	{
		// After Inigo Quilez's ray-capsule intersection.
		auto Sphere = [&Origin, &Dir, R](const FVector& C)
		{
			const FVector OC = Origin - C;
			const double Bq = Dir | OC;
			const double H = Bq * Bq - ((OC | OC) - static_cast<double>(R) * R);
			return H >= 0.0 ? static_cast<float>(-Bq - FMath::Sqrt(H)) : -1.f;
		};
		const FVector BA = B - A;
		const FVector OA = Origin - A;
		const double BABA = BA | BA;
		if (BABA < 1.0)
		{
			return Sphere(A);
		}
		const double BARD = BA | Dir;
		const double BAOA = BA | OA;
		const double RDOA = Dir | OA;
		const double OAOA = OA | OA;
		const double Qa = BABA - BARD * BARD;
		const double Qb = BABA * RDOA - BAOA * BARD;
		const double Qc = BABA * OAOA - BAOA * BAOA - static_cast<double>(R) * R * BABA;
		if (Qa > 1.0e-6)
		{
			const double H = Qb * Qb - Qa * Qc;
			if (H < 0.0)
			{
				return -1.f;
			}
			const double T = (-Qb - FMath::Sqrt(H)) / Qa;
			const double Y = BAOA + T * BARD;
			if (Y > 0.0 && Y < BABA)
			{
				return static_cast<float>(T);
			}
			return Sphere(Y <= 0.0 ? A : B);
		}
		// Along the axis: whichever end cap comes first.
		const float TA = Sphere(A);
		const float TB = Sphere(B);
		return TA < 0.f ? TB : (TB < 0.f ? TA : FMath::Min(TA, TB));
	}

	bool TraceHitboxes(const FVector& Feet, const FVector& Forward, bool bCrouched,
		const FVector& Origin, const FVector& Direction, float MaxDistance, ECSHitZone& OutZone, float& OutDistance)
	{
		// v2.0 (C6): three shapes instead of the whole 68 cm capsule - a head is
		// a head, and a shot through the gap beside the body misses. Built from
		// numbers rather than the animated skeleton, so the authority's answer
		// never depends on an animation frame.
		const float Height = BodyHeight(bCrouched);
		const FVector Ahead = Forward.GetSafeNormal2D();
		struct FShape { ECSHitZone Zone; float Low; float High; float Radius; float Forward; };
		// Fractions of the body height. Crouched, the body leans forward and
		// the bent legs are bulkier.
		static const FShape Standing[] = {
			{ ECSHitZone::Head,  0.935f, 0.935f, 11.f, 3.f },
			{ ECSHitZone::Torso, 0.56f,  0.79f,  17.f, 0.f },
			{ ECSHitZone::Limb,  0.06f,  0.53f,  15.f, 0.f },
		};
		static const FShape Crouched[] = {
			{ ECSHitZone::Head,  0.925f, 0.925f, 11.f, 8.f },
			{ ECSHitZone::Torso, 0.45f,  0.72f,  17.f, 4.f },
			{ ECSHitZone::Limb,  0.05f,  0.42f,  17.f, 6.f },
		};
		bool bHit = false;
		OutDistance = MaxDistance;
		for (const FShape& Shape : bCrouched ? Crouched : Standing)
		{
			const FVector A = Feet + FVector(0.f, 0.f, Height * Shape.Low) + Ahead * Shape.Forward;
			const FVector B = Feet + FVector(0.f, 0.f, Height * Shape.High) + Ahead * Shape.Forward;
			const float T = RayCapsule(Origin, Direction, A, B, Shape.Radius);
			if (T >= 0.f && T <= OutDistance)
			{
				bHit = true;
				OutDistance = T;
				OutZone = Shape.Zone;
			}
		}
		return bHit;
	}
}
