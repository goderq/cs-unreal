// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Combat/CSCheatGuard.h"

#include "Core/CSLog.h"

namespace
{
	/** Token bucket per request kind: sustained rate per second and burst size. */
	struct FBucket
	{
		double Rate;
		double Burst;
	};

	// Fire is also rate-checked per weapon by ValidateFire; this only stops
	// floods far beyond any weapon (the fastest fires ~13/s).
	constexpr FBucket GBuckets[] = {
		{ 20.0, 30.0 },	// Fire
		{ 3.0, 5.0 },	// Reload
		{ 6.0, 10.0 },	// Pickup
		{ 8.0, 12.0 },	// Slot
		{ 4.0, 6.0 },	// Drop
		{ 5.0, 8.0 },	// Buy
		{ 2.0, 3.0 },	// Throw
	};
	static_assert(UE_ARRAY_COUNT(GBuckets) == static_cast<int32>(ECSRequestKind::Count), "one bucket per request kind");

	/** Horizontal distance a legal pawn can cover in Seconds, plus slack for jitter. */
	float AllowedDistance(double Seconds)
	{
		return FCSCheatGuard::MaxLegalSpeed * static_cast<float>(Seconds) + 150.f;
	}
}

void FCSCheatGuard::ObservePosition(int32 PlayerId, const FVector& Location, double Now, bool bFalling, int32 RespawnCounter)
{
	FPlayerState& State = Players.FindOrAdd(PlayerId);

	// First sighting or a respawn: the pawn legitimately jumped across the map.
	if (!State.bInitialised || RespawnCounter != State.LastRespawnCounter)
	{
		State.bInitialised = true;
		State.LastRespawnCounter = RespawnCounter;
		State.GraceUntil = Now + GraceSeconds;
		State.WindowStart = State.LastLocation = Location;
		State.WindowStartTime = State.LastTime = Now;
		return;
	}

	if (Now < State.GraceUntil)
	{
		State.WindowStart = State.LastLocation = Location;
		State.WindowStartTime = State.LastTime = Now;
		return;
	}

	// Teleport: one observation step far longer than the time allows.
	const double Step = FMath::Max(0.001, Now - State.LastTime);
	const float StepDistance = FVector::Dist2D(Location, State.LastLocation);
	if (StepDistance > TeleportDistance && StepDistance > AllowedDistance(Step) * 2.f)
	{
		AddStrike(PlayerId, State, ECSCheatReason::Teleport, Now, /*Weight*/ 3.f,
			FString::Printf(TEXT("moved %.0f cm in %.2f s"), StepDistance, Step));
		State.WindowStart = Location;
		State.WindowStartTime = Now;
	}
	State.LastLocation = Location;
	State.LastTime = Now;

	// Speed: average over a one-second window, so single late updates do not count.
	const double Window = Now - State.WindowStartTime;
	if (Window >= 1.0)
	{
		const float Distance = FVector::Dist2D(Location, State.WindowStart);
		// Falling adds air control, not speed; the same horizontal limit applies.
		if (Distance > AllowedDistance(Window))
		{
			AddStrike(PlayerId, State, ECSCheatReason::Speed, Now, /*Weight*/ 1.f,
				FString::Printf(TEXT("%.0f cm/s over %.1f s (limit %.0f)%s"), Distance / Window, Window,
					MaxLegalSpeed, bFalling ? TEXT(", airborne") : TEXT("")));
		}
		State.WindowStart = Location;
		State.WindowStartTime = Now;
	}
}

bool FCSCheatGuard::AllowRequest(int32 PlayerId, ECSRequestKind Kind, double Now)
{
	FPlayerState& State = Players.FindOrAdd(PlayerId);
	const int32 K = static_cast<int32>(Kind);
	const FBucket& Bucket = GBuckets[K];

	// Refill.
	if (!State.bBucketStarted[K])
	{
		State.bBucketStarted[K] = true;
		State.Tokens[K] = Bucket.Burst;
	}
	else
	{
		State.Tokens[K] = FMath::Min(Bucket.Burst, State.Tokens[K] + (Now - State.TokenTime[K]) * Bucket.Rate);
	}
	State.TokenTime[K] = Now;

	if (State.Tokens[K] < 1.0)
	{
		if (!State.bFloodReported)
		{
			State.bFloodReported = true;
			AddStrike(PlayerId, State, ECSCheatReason::Flood, Now, /*Weight*/ 1.f,
				FString::Printf(TEXT("request flood (kind %d)"), K));
		}
		return false;
	}
	State.Tokens[K] -= 1.0;
	State.bFloodReported = false;

	if (Kind != ECSRequestKind::Reload && Now < State.SuspendedUntil)
	{
		return false;
	}
	return true;
}

bool FCSCheatGuard::IsSuspended(int32 PlayerId, double Now) const
{
	const FPlayerState* State = Players.Find(PlayerId);
	return State && Now < State->SuspendedUntil;
}

void FCSCheatGuard::ReportViolation(int32 PlayerId, ECSCheatReason Reason, double Now, float Weight, const FString& Detail)
{
	AddStrike(PlayerId, Players.FindOrAdd(PlayerId), Reason, Now, Weight, Detail);
}

const TCHAR* LexToString(ECSCheatReason Reason)
{
	switch (Reason)
	{
	case ECSCheatReason::Speed:     return TEXT("speed");
	case ECSCheatReason::Teleport:  return TEXT("teleport");
	case ECSCheatReason::Flood:     return TEXT("flood");
	case ECSCheatReason::ForgedRpc: return TEXT("forged rpc");
	case ECSCheatReason::BadInput:  return TEXT("bad input");
	default:                        return TEXT("none");
	}
}

void FCSCheatGuard::Forget(int32 PlayerId)
{
	Players.Remove(PlayerId);
}

ECSCheatReason FCSCheatGuard::GetLastReason(int32 PlayerId) const
{
	const FPlayerState* State = Players.Find(PlayerId);
	return State ? State->LastReason : ECSCheatReason::None;
}

int32 FCSCheatGuard::GetStrikes(int32 PlayerId) const
{
	const FPlayerState* State = Players.Find(PlayerId);
	return State ? FMath::FloorToInt(State->Strikes) : 0;
}

void FCSCheatGuard::AddStrike(int32 PlayerId, FPlayerState& State, ECSCheatReason Reason, double Now, float Weight, const FString& Detail)
{
	// Strikes decay linearly so an isolated hitch is forgotten.
	const double SinceLast = Now - State.LastStrikeTime;
	State.Strikes = FMath::Max(0.f, State.Strikes - static_cast<float>(SinceLast / StrikeDecaySeconds));
	State.Strikes += Weight;
	State.LastStrikeTime = Now;
	State.LastReason = Reason;

	UE_LOG(LogCSAuth, Warning, TEXT("Cheat guard: player %d strike (%s): %s. Strikes %.1f/%d."),
		PlayerId, LexToString(Reason), *Detail, State.Strikes, StrikesToSuspend);

	if (State.Strikes >= StrikesToSuspend && Now >= State.SuspendedUntil)
	{
		State.SuspendedUntil = Now + SuspensionSeconds;
		State.Strikes = 0.f;
		UE_LOG(LogCSAuth, Warning, TEXT("Cheat guard: player %d SUSPENDED for %.0f s (combat and item requests rejected)."),
			PlayerId, SuspensionSeconds);
	}
}
