// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Keeps the match's bots alive. One local (non-replicated) instance exists on
// every peer; only the one on the current authority acts:
//
//   - reads the bot count / difficulty from ACSGameState (set once from the
//     creating client's menu choice or -bots=N)
//   - spawns missing bots as ordinary ACSCharacter pawns, owned by the Master
//     Client, and gives each an ACSBotController
//   - after a master migration, gives the bots it now owns new controllers
//     (controllers are local objects and die with the old master)
//   - removes surplus bots (their inventory drops as loot, like a player
//     leaving)

#pragma once

#include "CoreMinimal.h"
#include "Core/CSCoreTypes.h"
#include "GameFramework/Actor.h"
#include "CSBotManager.generated.h"

class ACSCharacter;
class ACSBotController;

UCLASS(NotBlueprintable)
class CSFUSION_API ACSBotManager : public AActor
{
	GENERATED_BODY()

public:
	ACSBotManager();

	virtual void Tick(float DeltaSeconds) override;

	static ACSBotManager* Get(const UObject* WorldContextObject);

	/** Display name for a bot id ("Bot Alpha"...). */
	static FString GetBotName(int32 BotId);

	/** Bots currently alive in the world (any peer). */
	static int32 CountBots(const UObject* WorldContextObject);

private:
	void Maintain();
	ACSCharacter* SpawnBot(int32 BotId);
	void EnsureController(ACSCharacter* Bot, ECSBotDifficulty Difficulty);
	void RemoveBot(ACSCharacter* Bot);

	float Accumulator = 0.f;
	float Age = 0.f;
	int32 NextBotId = CSBots::FirstBotId;
};
