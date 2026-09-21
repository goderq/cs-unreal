// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v1.1 game modes and maps.
//
//   Deathmatch       free for all, instant respawn, spawn protection with a
//                    buy window, first to the kill limit or the most kills
//                    when time runs out
//   Team Deathmatch  two teams, the same respawn / protection / buy rules,
//                    team kill limit
//   5 vs 5           rounds, no respawn inside a round, a team wins the
//                    round by eliminating the other (or by having more
//                    players alive when the clock runs out); buy time at the
//                    start of each round, CS-style economy; no protection
//
// Every number is config (DefaultGame.ini), read by the authority. Clients
// read the same values to draw the HUD; the authority's copy decides.

#pragma once

#include "CoreMinimal.h"
#include "Core/CSCoreTypes.h"
#include "Engine/DeveloperSettings.h"
#include "CSModeSettings.generated.h"

USTRUCT(BlueprintType)
struct CSFUSION_API FCSModeRules
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Mode") bool bTeams = false;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Mode") bool bRounds = false;

	/** Respawn after death inside a round/match (false in 5v5). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Mode") bool bRespawn = true;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Mode") float RespawnDelay = 3.f;

	/** Spawn protection; 0 = none. Moving, jumping, crouching or firing ends it early. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Mode") float ProtectionSeconds = 0.f;

	/**
	 * Buy window at the start of each round (rounds modes). With 0 the shop
	 * is open exactly while the player is spawn-protected.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Mode") float BuySeconds = 0.f;

	/** Kills (DM: per player, TDM: per team) or rounds (5v5) that win the match. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Mode") int32 ScoreLimit = 30;

	/** Match length (non-round modes) or round length (rounds modes), seconds. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Mode") float TimeLimitSeconds = 600.f;

	/** Pause between rounds (rounds modes), seconds. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Mode") float RoundEndSeconds = 5.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Economy") int32 StartMoney = 800;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Economy") int32 MaxMoney = 16000;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Economy") int32 KillReward = 300;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Economy") int32 HeadshotBonus = 0;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Economy") int32 RoundWinReward = 3250;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Economy") int32 RoundLossReward = 1400;
	/** Added per consecutive lost round, up to LossRewardMax. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Economy") int32 LossStreakBonus = 500;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Economy") int32 LossRewardMax = 3400;

	/** Players per team (team modes) or in total (DM). Bots fill up to it when BotsFill. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Mode") int32 TeamSize = 5;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Mode") bool bBotsFillTeams = false;
};

/** One selectable map. */
USTRUCT(BlueprintType)
struct CSFUSION_API FCSMapInfo
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Map") FName Id;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Map") FString DisplayName;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Map") FString Description;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Map") TSoftObjectPtr<UWorld> World;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "CS Game Modes"))
class CSFUSION_API UCSModeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UCSModeSettings();

	UPROPERTY(Config, EditAnywhere, Category = "Modes") FCSModeRules Deathmatch;
	UPROPERTY(Config, EditAnywhere, Category = "Modes") FCSModeRules TeamDeathmatch;
	UPROPERTY(Config, EditAnywhere, Category = "Modes") FCSModeRules Competitive;

	UPROPERTY(Config, EditAnywhere, Category = "Maps") TArray<FCSMapInfo> Maps;

	static const UCSModeSettings* Get() { return GetDefault<UCSModeSettings>(); }
	static const FCSModeRules& Rules(ECSGameModeType Mode);

	/** Short tag for room names and logs: DM / TDM / 5V5. */
	static FString ModeTag(ECSGameModeType Mode);
	static FText ModeName(ECSGameModeType Mode);
	static bool ParseModeTag(const FString& Tag, ECSGameModeType& OutMode);

	/** Map by id, or the first map. */
	const FCSMapInfo* FindMap(FName Id) const;
	/** Map by world package path (e.g. from Fusion's room map data). */
	const FCSMapInfo* FindMapByWorld(const FString& PackageName) const;
};
