// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Server-side (Master Client) cheat detection, owned by ACSMatchDirector.
//
// What Fusion's shared-authority model leaves open, and what this closes:
//
//   Movement   Each player owns and simulates their own pawn, so its position
//              is whatever that client says. A speed hack or teleport would
//              also defeat every distance check (pickup reach, fire origin).
//              The guard samples every human pawn and rejects what no legal
//              movement could produce: too fast, a jump across the map, rising
//              faster than stairs allow, hanging in the air, passing through
//              static geometry, or turning up far from the spawn point it was
//              sent to (v2.0, B10).
//
//   Flooding   RPCs cost the Master Client work. Every request type has a
//              token bucket; bursts beyond it are dropped.
//
//   Everything else a client could fake - damage, health, ammo, inventory,
//   fire rate, spread, reach, the sender of an RPC - is already decided or
//   re-checked by the authority (ValidateFire, CSRpcGuard, the handlers).
//
// The ladder of measures (B11), thresholds in UCSCombatSettings:
//   strike      logged; strikes decay, so one lag spike is forgotten
//   suspension  enough strikes: fire, pickup, drop, slot, buy refused for a while
//   removal     enough suspensions in one match: out of the match (the Master
//               Client cannot disconnect anybody from Photon, AUDIT K8)
// Every suspension and removal is queued as an incident; the director logs
// it and reports it to the backend (security_log). Bots are exempt: they run
// on the authority itself.
//
// Limits of the model (docs/AUDIT.md 3.6): the Master Client is the
// authority, so a cheating host cannot be stopped from the inside; aim
// assistance is indistinguishable from skill at this level; small speed-ups
// under the thresholds look like lag.

#pragma once

#include "CoreMinimal.h"

enum class ECSRequestKind : uint8
{
	Fire,
	Reload,
	Pickup,
	Slot,
	Drop,
	Buy,
	Throw,
	/** Aim down sights on / off (B8). */
	Aim,
	Count
};

enum class ECSCheatReason : uint8
{
	None,
	Speed,
	Teleport,
	Flood,
	/** An RPC sent by someone other than the owner / Master Client (A1). */
	ForgedRpc,
	/** A request with NaN, infinite or out-of-range values (B9). */
	BadInput,
	/** Rising too fast, or hanging in the air (B10). */
	Flying,
	/** The path between two samples went through static geometry (B10). */
	Wall,
	/** After a respawn the pawn turned up far from its spawn point (B10). */
	SpawnPoint
};

CSFUSION_API const TCHAR* LexToString(ECSCheatReason Reason);

/** Thresholds and the ladder (B10, B11). The director loads them from UCSCombatSettings; the defaults serve the unit tests. */
struct CSFUSION_API FCSCheatTuning
{
	/** Legal ground speed ceiling, cm/s (sprint plus margin). */
	float MaxLegalSpeed = 620.f * 1.35f;
	/** A single observed jump longer than this is a teleport, cm. */
	float TeleportDistance = 600.f;
	/** Upward speed averaged over one second, cm/s. Stairs at a sprint stay below it. */
	float MaxRiseSpeed = 550.f;
	/** Feet this far above any floor count as airborne, cm. */
	float AirborneHeight = 150.f;
	/** Airborne continuously for longer than this is flying, s. */
	double MaxAirborneSeconds = 3.0;
	/** After the respawn grace the pawn must be within this of its spawn point plus a sprint of GraceSeconds, cm. */
	float SpawnTolerance = 300.f;

	float StrikesToSuspend = 3.f;
	double StrikeDecaySeconds = 6.0;
	double SuspensionSeconds = 10.0;
	/** Suspensions in one match that remove the player from it; 0 = never. */
	int32 SuspensionsToRemove = 3;

	/** No movement checks right after a respawn or a first sighting, s. */
	double GraceSeconds = 2.0;
};

/** What the authority saw of one human pawn this tick. */
struct FCSMoveSample
{
	FVector Location = FVector::ZeroVector;
	double Now = 0.0;
	bool bFalling = false;
	int32 RespawnCounter = 0;
	/** Distance from the feet down to the floor; large when there is none. */
	float HeightAboveFloor = 0.f;
	/** The straight path from the previous sample went through static geometry. */
	bool bPathBlocked = false;
	/** The spawn point the authority chose for the current life. */
	FVector SpawnPoint = FVector::ZeroVector;
	bool bHasSpawnPoint = false;
};

/** A suspension or removal, for the log and the backend. */
struct FCSCheatIncident
{
	int32 PlayerId = 0;
	bool bRemoved = false;
	ECSCheatReason Reason = ECSCheatReason::None;
	FString Detail;
};

class CSFUSION_API FCSCheatGuard
{
public:
	FCSCheatTuning Tuning;

	/** Authority, every director tick for every live human pawn. */
	void Observe(int32 PlayerId, const FCSMoveSample& Sample);

	/** Short form for the unit tests: no geometry, no spawn point. */
	void ObservePosition(int32 PlayerId, const FVector& Location, double Now, bool bFalling, int32 RespawnCounter);

	/** Authority, at the top of every RPC handler. False = drop the request. */
	bool AllowRequest(int32 PlayerId, ECSRequestKind Kind, double Now);

	bool IsSuspended(int32 PlayerId, double Now) const;

	/** Authority: a violation found outside the guard (forged RPC, bad input). Weight 3 suspends at once. */
	void ReportViolation(int32 PlayerId, ECSCheatReason Reason, double Now, float Weight, const FString& Detail);

	/** The ladder reached removal for this player (it stays so until Forget). */
	bool ShouldRemove(int32 PlayerId) const;

	/** Suspensions and removals since the last call. */
	TArray<FCSCheatIncident> TakeIncidents();

	/** Forget a player (left the match). */
	void Forget(int32 PlayerId);

	/** Most recent reason a player got a strike, for logs and tests. */
	ECSCheatReason GetLastReason(int32 PlayerId) const;
	int32 GetStrikes(int32 PlayerId) const;
	int32 GetSuspensions(int32 PlayerId) const;

private:
	struct FPlayerState
	{
		FVector WindowStart = FVector::ZeroVector;
		double WindowStartTime = 0.0;
		FVector LastLocation = FVector::ZeroVector;
		double LastTime = 0.0;
		double GraceUntil = 0.0;
		int32 LastRespawnCounter = -1;
		bool bInitialised = false;

		/** After a respawn: check the spawn point once the grace is over. */
		bool bCheckSpawn = false;
		FVector ExpectedSpawn = FVector::ZeroVector;
		/** Since when the feet are far above any floor; < 0 = on the ground. */
		double AirborneSince = -1.0;

		float Strikes = 0.f;
		double LastStrikeTime = 0.0;
		double SuspendedUntil = 0.0;
		int32 Suspensions = 0;
		bool bRemove = false;
		ECSCheatReason LastReason = ECSCheatReason::None;

		double Tokens[static_cast<int32>(ECSRequestKind::Count)] = {};
		double TokenTime[static_cast<int32>(ECSRequestKind::Count)] = {};
		bool bBucketStarted[static_cast<int32>(ECSRequestKind::Count)] = {};
		bool bFloodReported = false;
	};

	/** Horizontal distance a legal pawn can cover in Seconds, plus slack for jitter. */
	float AllowedDistance(double Seconds) const;

	void AddStrike(int32 PlayerId, FPlayerState& State, ECSCheatReason Reason, double Now, float Weight, const FString& Detail);

	TMap<int32, FPlayerState> Players;
	TArray<FCSCheatIncident> Incidents;
};
