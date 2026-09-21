// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Server-side (Master Client) cheat detection, owned by ACSMatchDirector.
//
// What Fusion's shared-authority model leaves open, and what this closes:
//
//   Movement   Each player owns and simulates their own pawn, so its position
//              is whatever that client says. A speed hack or teleport would
//              also defeat every distance check (pickup reach, fire origin).
//              The guard samples every human pawn, rejects displacement no
//              legal movement could produce, and suspends the player.
//
//   Flooding   RPCs cost the Master Client work. Every request type has a
//              token bucket; bursts beyond it are dropped.
//
//   Everything else a client could fake - damage, health, ammo, inventory,
//   fire rate, spread, reach - is already decided or re-checked by the
//   authority (see ValidateFire and the RPC handlers).
//
// A suspended player's fire, pickup, drop and slot requests are rejected for
// SuspensionSeconds. Strikes decay, so a single lag spike never suspends
// anybody. Bots are exempt: they run on the authority itself.
//
// Limits of the model (documented in ARCHITECTURE.md): the Master Client is
// the authority, so a cheating host cannot be stopped from the inside; aim
// assistance is indistinguishable from skill at this level.

#pragma once

#include "CoreMinimal.h"

enum class ECSRequestKind : uint8
{
	Fire,
	Reload,
	Pickup,
	Slot,
	Drop,
	Count
};

enum class ECSCheatReason : uint8
{
	None,
	Speed,
	Teleport,
	Flood
};

class CSFUSION_API FCSCheatGuard
{
public:
	/** Authority, every director tick: feed the observed pawn position. */
	void ObservePosition(int32 PlayerId, const FVector& Location, double Now, bool bFalling, int32 RespawnCounter);

	/** Authority, at the top of every RPC handler. False = drop the request. */
	bool AllowRequest(int32 PlayerId, ECSRequestKind Kind, double Now);

	bool IsSuspended(int32 PlayerId, double Now) const;

	/** Forget a player (left the match). */
	void Forget(int32 PlayerId);

	/** Most recent reason a player was suspended, for logs and tests. */
	ECSCheatReason GetLastReason(int32 PlayerId) const;
	int32 GetStrikes(int32 PlayerId) const;

	/** Legal ground speed ceiling, cm/s (sprint plus margin). */
	static constexpr float MaxLegalSpeed = 620.f * 1.35f;

	/** A single observed jump longer than this is a teleport. */
	static constexpr float TeleportDistance = 600.f;

	static constexpr int32 StrikesToSuspend = 3;
	static constexpr double StrikeDecaySeconds = 6.0;
	static constexpr double SuspensionSeconds = 10.0;

	/** Grace after a respawn teleport or first sighting, seconds. */
	static constexpr double GraceSeconds = 2.0;

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

		float Strikes = 0.f;
		double LastStrikeTime = 0.0;
		double SuspendedUntil = 0.0;
		ECSCheatReason LastReason = ECSCheatReason::None;

		double Tokens[static_cast<int32>(ECSRequestKind::Count)] = {};
		double TokenTime[static_cast<int32>(ECSRequestKind::Count)] = {};
		bool bBucketStarted[static_cast<int32>(ECSRequestKind::Count)] = {};
		bool bFloodReported = false;
	};

	void AddStrike(int32 PlayerId, FPlayerState& State, ECSCheatReason Reason, double Now, float Weight, const FString& Detail);

	TMap<int32, FPlayerState> Players;
};
