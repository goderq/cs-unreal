// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// How a bot plays at each difficulty. Everything a bot does still goes
// through the authority's normal validation (fire rate, ammo, spread), so a
// "Hard" bot is better at deciding and aiming, never faster than the rules.
//
// v1.1: aiming is a real, smoothly turning view (ACSBotController::
// UpdateControlRotation) and the bot shoots where that view points, so a bot
// has to actually track a target. Where on the body it aims is picked per
// burst from weighted body parts - never always the head.

#pragma once

#include "CoreMinimal.h"
#include "Core/CSCoreTypes.h"

/** Body part a bot aims at; offsets in ACSBotController::AimOffsetFor. */
enum class ECSBotAimPart : uint8 { Head, Chest, Stomach, Pelvis, LeftArm, RightArm, LeftLeg, RightLeg, Count };

struct FCSBotTuning
{
	/** Seconds between first seeing a target and the first shot. */
	float ReactionTime = 0.5f;

	/** Maximum turn rate of the view, degrees per second. */
	float TurnSpeed = 260.f;

	/** How quickly the view closes in on the aim point (higher = snappier), 1/s. */
	float TurnResponsiveness = 7.f;

	/** Only shoots when the view is within this many degrees of the aim point. */
	float FireConeDegrees = 4.f;

	/** Error cone per shot, degrees (on top of the weapon's own spread). */
	float AimErrorDegrees = 2.2f;

	/** Slow wandering of the view around the aim point, degrees. */
	float SwayDegrees = 1.0f;

	/** Leads moving targets by this fraction of their velocity x flight time. */
	float LeadFactor = 0.f;

	/** Body part weights (sum does not matter). */
	float PartWeights[static_cast<int32>(ECSBotAimPart::Count)] = { 10.f, 35.f, 25.f, 10.f, 5.f, 5.f, 5.f, 5.f };

	/** Shots per burst, then a pause (automatic weapons). */
	int32 BurstShots = 4;
	float BurstPause = 0.5f;

	/** Seconds between strafe moves while fighting. */
	float StrafeInterval = 1.4f;

	/** Sight range, cm. */
	float SightRadius = 3600.f;

	/** Chance to throw a grenade when it has one and a target is at a good range. */
	float GrenadeChance = 0.2f;

	static FCSBotTuning For(ECSBotDifficulty Difficulty)
	{
		FCSBotTuning T;
		switch (Difficulty)
		{
		case ECSBotDifficulty::Easy:
		{
			T.ReactionTime = 0.95f;
			T.TurnSpeed = 130.f;
			T.TurnResponsiveness = 3.5f;
			T.FireConeDegrees = 7.f;
			T.AimErrorDegrees = 4.2f;
			T.SwayDegrees = 2.2f;
			T.LeadFactor = 0.f;
			const float W[] = { 2.f, 30.f, 30.f, 12.f, 6.f, 6.f, 7.f, 7.f };
			FMemory::Memcpy(T.PartWeights, W, sizeof(W));
			T.BurstShots = 3;
			T.BurstPause = 0.9f;
			T.StrafeInterval = 2.4f;
			T.SightRadius = 2800.f;
			T.GrenadeChance = 0.f;
			break;
		}
		case ECSBotDifficulty::Hard:
		{
			T.ReactionTime = 0.28f;
			T.TurnSpeed = 480.f;
			T.TurnResponsiveness = 12.f;
			T.FireConeDegrees = 2.5f;
			T.AimErrorDegrees = 0.9f;
			T.SwayDegrees = 0.45f;
			T.LeadFactor = 0.8f;
			const float W[] = { 24.f, 36.f, 18.f, 6.f, 4.f, 4.f, 4.f, 4.f };
			FMemory::Memcpy(T.PartWeights, W, sizeof(W));
			T.BurstShots = 6;
			T.BurstPause = 0.28f;
			T.StrafeInterval = 0.9f;
			T.SightRadius = 4800.f;
			T.GrenadeChance = 0.35f;
			break;
		}
		default:
			break;
		}
		return T;
	}
};
