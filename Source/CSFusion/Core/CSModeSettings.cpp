// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Core/CSModeSettings.h"

#define LOCTEXT_NAMESPACE "CSModes"

UCSModeSettings::UCSModeSettings()
{
	// Defaults; DefaultGame.ini overrides any of them.
	Deathmatch.bTeams = false;
	Deathmatch.bRespawn = true;
	Deathmatch.RespawnDelay = 3.f;
	Deathmatch.ProtectionSeconds = 7.f;
	Deathmatch.ScoreLimit = 30;
	Deathmatch.TimeLimitSeconds = 600.f;
	Deathmatch.StartMoney = 2500;
	Deathmatch.KillReward = 400;
	Deathmatch.HeadshotBonus = 100;
	Deathmatch.TeamSize = 10;

	TeamDeathmatch = Deathmatch;
	TeamDeathmatch.bTeams = true;
	TeamDeathmatch.ScoreLimit = 75;
	TeamDeathmatch.TeamSize = 5;
	TeamDeathmatch.bBotsFillTeams = false;

	Competitive.bTeams = true;
	Competitive.bRounds = true;
	Competitive.bRespawn = false;
	Competitive.ProtectionSeconds = 0.f;
	Competitive.BuySeconds = 15.f;
	Competitive.ScoreLimit = 8;
	Competitive.TimeLimitSeconds = 115.f;
	Competitive.RoundEndSeconds = 5.f;
	Competitive.StartMoney = 800;
	Competitive.KillReward = 300;
	Competitive.TeamSize = 5;
	Competitive.bBotsFillTeams = true;
}

const FCSModeRules& UCSModeSettings::Rules(ECSGameModeType Mode)
{
	const UCSModeSettings* S = Get();
	switch (Mode)
	{
	case ECSGameModeType::TeamDeathmatch:	return S->TeamDeathmatch;
	case ECSGameModeType::Competitive:		return S->Competitive;
	default:								return S->Deathmatch;
	}
}

FString UCSModeSettings::ModeTag(ECSGameModeType Mode)
{
	switch (Mode)
	{
	case ECSGameModeType::TeamDeathmatch:	return TEXT("TDM");
	case ECSGameModeType::Competitive:		return TEXT("5V5");
	default:								return TEXT("DM");
	}
}

FText UCSModeSettings::ModeName(ECSGameModeType Mode)
{
	switch (Mode)
	{
	case ECSGameModeType::TeamDeathmatch:	return LOCTEXT("TDM", "Team Deathmatch");
	case ECSGameModeType::Competitive:		return LOCTEXT("Comp", "5 vs 5");
	default:								return LOCTEXT("DM", "Deathmatch");
	}
}

bool UCSModeSettings::ParseModeTag(const FString& Tag, ECSGameModeType& OutMode)
{
	const FString T = Tag.TrimStartAndEnd().ToUpper();
	if (T == TEXT("DM") || T == TEXT("DEATHMATCH")) { OutMode = ECSGameModeType::Deathmatch; return true; }
	if (T == TEXT("TDM") || T == TEXT("TEAMDEATHMATCH")) { OutMode = ECSGameModeType::TeamDeathmatch; return true; }
	if (T == TEXT("5V5") || T == TEXT("COMP") || T == TEXT("COMPETITIVE")) { OutMode = ECSGameModeType::Competitive; return true; }
	return false;
}

const FCSMapInfo* UCSModeSettings::FindMap(FName Id) const
{
	for (const FCSMapInfo& Map : Maps)
	{
		if (Map.Id == Id)
		{
			return &Map;
		}
	}
	return Maps.Num() > 0 ? &Maps[0] : nullptr;
}

const FCSMapInfo* UCSModeSettings::FindMapByWorld(const FString& PackageName) const
{
	for (const FCSMapInfo& Map : Maps)
	{
		if (Map.World.ToSoftObjectPath().GetLongPackageName() == PackageName)
		{
			return &Map;
		}
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
