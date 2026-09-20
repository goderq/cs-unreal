// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Under Fusion every client constructs and runs its own AGameMode locally -
// there is no single peer that runs it "for real". Two consequences drive the
// design of this class:
//
//   1. Match-flow code (RestartGame, HandleMatchHasStarted, scoring, login
//      bookkeeping) MUST be gated on authority, otherwise every effect
//      multiplies by the player count.
//
//   2. Pawn spawning is deliberately NOT gated. The documented Fusion flow is
//      that each client runs HandleStartingNewPlayer / RestartPlayer locally
//      and spawns its own controller and pawn, which then carries
//      PlayerAttached ownership so the owner drives it and it is destroyed
//      when that player leaves.
//
// Spawn-point selection is therefore local too, so it must be collision-free
// without negotiation: it is keyed off the stable Photon player id rather than
// picked at random.

#pragma once

#include "CoreMinimal.h"
#include "Core/CSCoreTypes.h"
#include "GameFramework/GameModeBase.h"
#include "CSGameMode.generated.h"

class ACSGameState;

UCLASS()
class CSFUSION_API ACSGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACSGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void RestartGame() override;

	/** Warmup length in seconds before the round starts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Match")
	float WarmupSeconds = 5.f;

	/** Round length in seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Match")
	float RoundSeconds = 600.f;

	/** Post-match scoreboard time before the round restarts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Match")
	float PostMatchSeconds = 10.f;

	/** Players required before warmup begins. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Match", meta = (ClampMin = "1"))
	int32 MinPlayersToStart = 1;

protected:
	/** Authority-only match-phase driver, ticked from Tick(). */
	void UpdateMatchFlow();

	ACSGameState* GetCSGameState() const;

	/** Cached, sorted list of player starts so indices agree on every peer. */
	void CachePlayerStarts();

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> CachedPlayerStarts;
};
