// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Sanity checks for numbers that arrive in RPCs (docs/AUDIT.md B9).
//
// Every comparison with NaN is false, so "distance > limit" lets a NaN
// through every range check after it; an infinite vector breaks traces and
// physics. Each handler that takes a vector or a float from the network
// checks it here first. A request that fails is a strike against its sender;
// an event that fails (from a broken host) is simply dropped.

#pragma once

#include "CoreMinimal.h"

namespace CSValidate
{
	/** No map is larger than this, cm (20 km). */
	constexpr double MaxWorldExtent = 2.0e6;

	/** A point in the world: finite and inside MaxWorldExtent. */
	inline bool IsSaneLocation(const FVector& V)
	{
		return !V.ContainsNaN() && V.GetAbsMax() <= MaxWorldExtent;
	}

	/** A direction: finite, not zero, not absurdly long (normalised by the receiver). */
	inline bool IsSaneDirection(const FVector& V)
	{
		return !V.ContainsNaN() && V.GetAbsMax() <= 1.0e4 && !V.IsNearlyZero(1.0e-4);
	}

	/** A velocity in cm/s: finite and below MaxSpeed. */
	inline bool IsSaneVelocity(const FVector& V, double MaxSpeed = 1.0e5)
	{
		return !V.ContainsNaN() && V.GetAbsMax() <= MaxSpeed;
	}

	/** A number: finite and |F| <= Max. */
	inline bool IsSaneNumber(double F, double Max)
	{
		return FMath::IsFinite(F) && FMath::Abs(F) <= Max;
	}
}
