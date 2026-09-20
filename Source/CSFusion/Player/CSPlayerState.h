// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// IMPORTANT AUTHORITY NOTE
//
// UFusionClient auto-attaches a UFusionActorComponent with
// Ownership = PlayerAttached to every APlayerState. That means the *player
// themselves* owns their PlayerState and is the only peer whose writes to it
// propagate - including a modified client.
//
// Therefore this class holds identity and presentation data ONLY:
//   - who this player is (Photon id, display name)
//   - which team they are on
//
// Authoritative combat state - health, armor, ammo, inventory, kills, deaths,
// alive/dead - must NOT live here. It lives on the Master-Client-owned
// ACSMatchDirector introduced in Stage 2, where a client physically cannot
// write it. See docs/ARCHITECTURE.md.
//
// The PlayerState lifetime follows the owning Fusion player: created on join,
// destroyed on leave, and moved into AGameMode::InactivePlayerArray on peers
// that observed the disconnect.

#pragma once

#include "CoreMinimal.h"
#include "Core/CSCoreTypes.h"
#include "GameFramework/PlayerState.h"
#include "CSPlayerState.generated.h"

UCLASS()
class CSFUSION_API ACSPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	ACSPlayerState();

	virtual void BeginPlay() override;

	/**
	 * Stable Photon player id for this session.
	 *
	 * This is written by the owning client, so never trust it for
	 * authorisation. The authority resolves the true sender of an RPC from
	 * Fusion itself, not from this field - this copy exists so other peers can
	 * map a PlayerState to a player id for UI.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "CS|Player")
	int32 PhotonPlayerId = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Team, BlueprintReadOnly, Category = "CS|Player")
	ECSTeam Team = ECSTeam::None;

	/** True for bot-controlled players. Set locally by the authority. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "CS|Player")
	bool bIsBot = false;

	UFUNCTION(BlueprintPure, Category = "CS|Player")
	FString GetDisplayName() const;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_Team();
};
