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
		{ 10.0, 16.0 },	// Aim
	};
	static_assert(UE_ARRAY_COUNT(GBuckets) == static_cast<int32>(ECSRequestKind::Count), "one bucket per request kind");
}

float FCSCheatGuard::AllowedDistance(double Seconds) const
{
	return Tuning.MaxLegalSpeed * static_cast<float>(Seconds) + 150.f;
}

void FCSCheatGuard::ObservePosition(int32 PlayerId, const FVector& Location, double Now, bool bFalling, int32 RespawnCounter)
{
	FCSMoveSample Sample;
	Sample.Location = Location;
	Sample.Now = Now;
	Sample.bFalling = bFalling;
	Sample.RespawnCounter = RespawnCounter;
	Observe(PlayerId, Sample);
}

void FCSCheatGuard::Observe(int32 PlayerId, const FCSMoveSample& Sample)
{
	FPlayerState& State = Players.FindOrAdd(PlayerId);
	const FVector& Location = Sample.Location;
	const double Now = Sample.Now;

	// First sighting or a respawn: the pawn legitimately jumped across the map.
	if (!State.bInitialised || Sample.RespawnCounter != State.LastRespawnCounter)
	{
		// Only a respawn has a known destination; the first sighting of a
		// player joining mid-match does not.
		State.bCheckSpawn = State.bInitialised && Sample.bHasSpawnPoint;
		State.ExpectedSpawn = Sample.SpawnPoint;
		State.bInitialised = true;
		State.LastRespawnCounter = Sample.RespawnCounter;
		State.GraceUntil = Now + Tuning.GraceSeconds;
		State.WindowStart = State.LastLocation = Location;
		State.WindowStartTime = State.LastTime = Now;
		State.AirborneSince = -1.0;
		return;
	}

	if (Now < State.GraceUntil)
	{
		State.WindowStart = State.LastLocation = Location;
		State.WindowStartTime = State.LastTime = Now;
		return;
	}

	// The respawn put the pawn at a spawn point; after the grace it can be at
	// most a sprint away from it (B10).
	if (State.bCheckSpawn)
	{
		State.bCheckSpawn = false;
		const float Allowed = Tuning.SpawnTolerance + AllowedDistance(Tuning.GraceSeconds);
		const float FromSpawn = FVector::Dist2D(Location, State.ExpectedSpawn);
		if (FromSpawn > Allowed)
		{
			AddStrike(PlayerId, State, ECSCheatReason::SpawnPoint, Now, /*Weight*/ 3.f,
				FString::Printf(TEXT("respawned %.0f cm from its spawn point (limit %.0f)"), FromSpawn, Allowed));
		}
	}

	// Teleport: one observation step far longer than the time allows.
	const double Step = FMath::Max(0.001, Now - State.LastTime);
	const float StepDistance = FVector::Dist2D(Location, State.LastLocation);
	if (StepDistance > Tuning.TeleportDistance && StepDistance > AllowedDistance(Step) * 2.f)
	{
		AddStrike(PlayerId, State, ECSCheatReason::Teleport, Now, /*Weight*/ 3.f,
			FString::Printf(TEXT("moved %.0f cm in %.2f s"), StepDistance, Step));
		State.WindowStart = Location;
		State.WindowStartTime = Now;
	}
	else if (Sample.bPathBlocked)
	{
		// Through a wall. A teleport already covers long jumps; this is the
		// short step through a door or a thin wall.
		AddStrike(PlayerId, State, ECSCheatReason::Wall, Now, /*Weight*/ 1.f,
			FString::Printf(TEXT("moved %.0f cm through static geometry"), StepDistance));
	}
	State.LastLocation = Location;
	State.LastTime = Now;

	// Hanging in the air: feet far above any floor for longer than any fall
	// or jump on our maps lasts.
	if (Sample.HeightAboveFloor > Tuning.AirborneHeight)
	{
		if (State.AirborneSince < 0.0)
		{
			State.AirborneSince = Now;
		}
		else if (Now - State.AirborneSince > Tuning.MaxAirborneSeconds)
		{
			AddStrike(PlayerId, State, ECSCheatReason::Flying, Now, /*Weight*/ 2.f,
				FString::Printf(TEXT("%.1f s in the air, %.0f cm above the floor"), Now - State.AirborneSince, Sample.HeightAboveFloor));
			State.AirborneSince = Now;
		}
	}
	else
	{
		State.AirborneSince = -1.0;
	}

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
					Tuning.MaxLegalSpeed, Sample.bFalling ? TEXT(", airborne") : TEXT("")));
		}
		// Rising: a jump peaks well below this; stairs at a sprint too.
		const float Rise = static_cast<float>((Location.Z - State.WindowStart.Z) / Window);
		if (Rise > Tuning.MaxRiseSpeed)
		{
			AddStrike(PlayerId, State, ECSCheatReason::Flying, Now, /*Weight*/ 1.f,
				FString::Printf(TEXT("rising at %.0f cm/s (limit %.0f)"), Rise, Tuning.MaxRiseSpeed));
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

	if (State.bRemove || (Kind != ECSRequestKind::Reload && Now < State.SuspendedUntil))
	{
		return false;
	}
	return true;
}

bool FCSCheatGuard::IsSuspended(int32 PlayerId, double Now) const
{
	const FPlayerState* State = Players.Find(PlayerId);
	return State && (State->bRemove || Now < State->SuspendedUntil);
}

void FCSCheatGuard::ReportViolation(int32 PlayerId, ECSCheatReason Reason, double Now, float Weight, const FString& Detail)
{
	AddStrike(PlayerId, Players.FindOrAdd(PlayerId), Reason, Now, Weight, Detail);
}

bool FCSCheatGuard::ShouldRemove(int32 PlayerId) const
{
	const FPlayerState* State = Players.Find(PlayerId);
	return State && State->bRemove;
}

TArray<FCSCheatIncident> FCSCheatGuard::TakeIncidents()
{
	return MoveTemp(Incidents);
}

const TCHAR* LexToString(ECSCheatReason Reason)
{
	switch (Reason)
	{
	case ECSCheatReason::Speed:      return TEXT("speed");
	case ECSCheatReason::Teleport:   return TEXT("teleport");
	case ECSCheatReason::Flood:      return TEXT("flood");
	case ECSCheatReason::ForgedRpc:  return TEXT("forged rpc");
	case ECSCheatReason::BadInput:   return TEXT("bad input");
	case ECSCheatReason::Flying:     return TEXT("flying");
	case ECSCheatReason::Wall:       return TEXT("through a wall");
	case ECSCheatReason::SpawnPoint: return TEXT("spawn point");
	default:                         return TEXT("none");
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

int32 FCSCheatGuard::GetSuspensions(int32 PlayerId) const
{
	const FPlayerState* State = Players.Find(PlayerId);
	return State ? State->Suspensions : 0;
}

void FCSCheatGuard::AddStrike(int32 PlayerId, FPlayerState& State, ECSCheatReason Reason, double Now, float Weight, const FString& Detail)
{
	if (State.bRemove)
	{
		return; // already on the way out
	}
	// Strikes decay linearly so an isolated hitch is forgotten.
	const double SinceLast = Now - State.LastStrikeTime;
	State.Strikes = FMath::Max(0.f, State.Strikes - static_cast<float>(SinceLast / Tuning.StrikeDecaySeconds));
	State.Strikes += Weight;
	State.LastStrikeTime = Now;
	State.LastReason = Reason;

	UE_LOG(LogCSAuth, Warning, TEXT("Cheat guard: player %d strike (%s): %s. Strikes %.1f/%.0f."),
		PlayerId, LexToString(Reason), *Detail, State.Strikes, Tuning.StrikesToSuspend);

	if (State.Strikes < Tuning.StrikesToSuspend || Now < State.SuspendedUntil)
	{
		return;
	}
	State.SuspendedUntil = Now + Tuning.SuspensionSeconds;
	State.Strikes = 0.f;
	++State.Suspensions;

	FCSCheatIncident& Incident = Incidents.AddDefaulted_GetRef();
	Incident.PlayerId = PlayerId;
	Incident.Reason = Reason;
	Incident.Detail = Detail;

	if (Tuning.SuspensionsToRemove > 0 && State.Suspensions >= Tuning.SuspensionsToRemove)
	{
		State.bRemove = true;
		Incident.bRemoved = true;
		UE_LOG(LogCSSecurity, Warning, TEXT("Cheat guard: player %d REMOVED from the match after %d suspensions (last: %s)."),
			PlayerId, State.Suspensions, LexToString(Reason));
		return;
	}
	UE_LOG(LogCSAuth, Warning, TEXT("Cheat guard: player %d SUSPENDED for %.0f s (combat and item requests rejected), suspension %d of %d."),
		PlayerId, Tuning.SuspensionSeconds, State.Suspensions, Tuning.SuspensionsToRemove);
}
