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
