// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// How a bot plays at each difficulty. Everything a bot does still goes
// through the authority's normal validation (fire rate, ammo, spread), so a
// "Hard" bot is better at deciding and aiming, never faster than the rules.

#pragma once

#include "CoreMinimal.h"
#include "Core/CSCoreTypes.h"

struct FCSBotTuning
{
	/** Seconds between first seeing a target and the first shot. */
	float ReactionTime = 0.45f;

	/** Extra aim error cone, degrees (on top of the weapon's own hip spread). */
	float AimErrorDegrees = 2.2f;

	/** Chance to aim at the head instead of the chest. */
	float HeadshotChance = 0.1f;

	/** Shots per burst, then a pause (automatic weapons). */
	int32 BurstShots = 4;
	float BurstPause = 0.45f;

	/** Seconds between strafe moves while fighting. */
	float StrafeInterval = 1.4f;

	/** Sight range, cm. */
	float SightRadius = 3500.f;

	static FCSBotTuning For(ECSBotDifficulty Difficulty)
	{
		FCSBotTuning T;
		switch (Difficulty)
		{
		case ECSBotDifficulty::Easy:
			T.ReactionTime = 0.85f;
			T.AimErrorDegrees = 4.5f;
			T.HeadshotChance = 0.0f;
			T.BurstShots = 3;
			T.BurstPause = 0.8f;
			T.StrafeInterval = 2.2f;
			T.SightRadius = 2800.f;
			break;
		case ECSBotDifficulty::Hard:
			T.ReactionTime = 0.25f;
			T.AimErrorDegrees = 1.0f;
			T.HeadshotChance = 0.3f;
			T.BurstShots = 6;
			T.BurstPause = 0.25f;
			T.StrafeInterval = 0.9f;
			T.SightRadius = 4500.f;
			break;
		default:
			break;
		}
		return T;
	}
};
