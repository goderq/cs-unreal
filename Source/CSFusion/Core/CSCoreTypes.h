// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CSCoreTypes.generated.h"

/** Which peer is allowed to mutate authoritative gameplay state. */
UENUM(BlueprintType)
enum class ECSAuthorityBackend : uint8
{
	/** No session running: this process is alone and owns everything. */
	Offline			UMETA(DisplayName = "Offline / Standalone"),

	/** Photon Fusion 3: the elected Master Client arbitrates gameplay. */
	FusionMaster	UMETA(DisplayName = "Photon Fusion - Master Client"),

	/** Reserved: native UE dedicated server (ROLE_Authority). Stage 8+. */
	DedicatedServer	UMETA(DisplayName = "Dedicated Server")
};

/** Room/session lifecycle as surfaced to the UI. Mirrors EFusionStatus. */
UENUM(BlueprintType)
enum class ECSSessionState : uint8
{
	None			UMETA(DisplayName = "None"),
	Connecting		UMETA(DisplayName = "Connecting"),
	Connected		UMETA(DisplayName = "Connected"),
	JoiningRoom		UMETA(DisplayName = "Joining Room"),
	InRoom			UMETA(DisplayName = "In Room"),
	LeavingRoom		UMETA(DisplayName = "Leaving Room"),
	Disconnected	UMETA(DisplayName = "Disconnected"),
	Error			UMETA(DisplayName = "Error")
};

/** High level match phase. Replicated on the GameState (Master Client owned). */
UENUM(BlueprintType)
enum class ECSMatchPhase : uint8
{
	WaitingForPlayers	UMETA(DisplayName = "Waiting For Players"),
	Warmup				UMETA(DisplayName = "Warmup"),
	InProgress			UMETA(DisplayName = "In Progress"),
	PostMatch			UMETA(DisplayName = "Post Match")
};

/** v1.1 game modes. Rules per mode live in UCSModeSettings (DefaultGame.ini). */
UENUM(BlueprintType)
enum class ECSGameModeType : uint8
{
	Deathmatch		UMETA(DisplayName = "Deathmatch"),
	TeamDeathmatch	UMETA(DisplayName = "Team Deathmatch"),
	Competitive		UMETA(DisplayName = "5 vs 5")
};

UENUM(BlueprintType)
enum class ECSTeam : uint8
{
	None	= 0	UMETA(DisplayName = "None"),
	Alpha	= 1	UMETA(DisplayName = "Alpha"),
	Bravo	= 2	UMETA(DisplayName = "Bravo")
};

/** Locomotion state, replicated for third-person animation on remote peers. */
UENUM(BlueprintType)
enum class ECSStanceState : uint8
{
	Standing	UMETA(DisplayName = "Standing"),
	Crouching	UMETA(DisplayName = "Crouching"),
	Sprinting	UMETA(DisplayName = "Sprinting")
};

/** Where a shot landed. Resolved on the authority from the hit bone. */
UENUM(BlueprintType)
enum class ECSHitZone : uint8
{
	None	UMETA(DisplayName = "None"),
	Head	UMETA(DisplayName = "Head"),
	Torso	UMETA(DisplayName = "Torso"),
	Limb	UMETA(DisplayName = "Limb")
};

/** Why a player died. Stage 4 uses this to decide what loot drops. */
UENUM(BlueprintType)
enum class ECSDeathReason : uint8
{
	Killed			UMETA(DisplayName = "Killed"),
	Suicide			UMETA(DisplayName = "Suicide"),
	Disconnected	UMETA(DisplayName = "Disconnected")
};

/**
 * Reasons the authority can refuse a fire request. Logged, and sent back to
 * the requester so the client can stop predicting.
 */
UENUM(BlueprintType)
enum class ECSFireRejection : uint8
{
	Accepted			UMETA(DisplayName = "Accepted"),
	ShooterDead			UMETA(DisplayName = "Shooter Dead"),
	NoRecord			UMETA(DisplayName = "No Combat Record"),
	FireRate			UMETA(DisplayName = "Fire Rate Violation"),
	OutOfAmmo			UMETA(DisplayName = "Out Of Ammo"),
	Reloading			UMETA(DisplayName = "Reloading"),
	OriginTooFar		UMETA(DisplayName = "Fire Origin Too Far From Pawn"),
	BadDirection		UMETA(DisplayName = "Malformed Aim Direction"),
	NoWeapon			UMETA(DisplayName = "No Weapon Definition"),
	MatchOver			UMETA(DisplayName = "Round Over (post-match)")
};

/**
 * Bot identities. Photon numbers human players from 1 upwards; bots take ids
 * from FirstBotId so the two can never collide in the director's records.
 */
namespace CSBots
{
	constexpr int32 FirstBotId = 1000;

	inline bool IsBotId(int32 PlayerId) { return PlayerId >= FirstBotId; }
}

UENUM(BlueprintType)
enum class ECSBotDifficulty : uint8
{
	Easy	UMETA(DisplayName = "Easy"),
	Normal	UMETA(DisplayName = "Normal"),
	Hard	UMETA(DisplayName = "Hard")
};
