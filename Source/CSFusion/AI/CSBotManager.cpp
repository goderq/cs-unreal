// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "AI/CSBotManager.h"

#include "AI/CSBotController.h"
#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
#include "Core/CSAuthority.h"
#include "Core/CSLog.h"
#include "Core/CSModeSettings.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameModes/CSGameMode.h"
#include "GameModes/CSGameState.h"
#include "Multiplayer/CSSessionSubsystem.h"

namespace
{
	/** Seconds after the map starts before the first bot spawns (authority settles). */
	constexpr float GStartDelay = 3.f;
	constexpr float GMaintainInterval = 1.f;
}

ACSBotManager::ACSBotManager()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	SetCanBeDamaged(false);
}

ACSBotManager* ACSBotManager::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ACSBotManager> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

FString ACSBotManager::GetBotName(int32 BotId)
{
	static const TCHAR* Names[] = { TEXT("Charlie"), TEXT("Delta"), TEXT("Echo"), TEXT("Foxtrot"),
		TEXT("Golf"), TEXT("Hotel"), TEXT("India"), TEXT("Juliet"), TEXT("Kilo"), TEXT("Lima") };
	const int32 Index = FMath::Max(0, BotId - CSBots::FirstBotId) % UE_ARRAY_COUNT(Names);
	return FString::Printf(TEXT("Bot %s"), Names[Index]);
}

int32 ACSBotManager::CountBots(const UObject* WorldContextObject)
{
	int32 Count = 0;
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (World)
	{
		for (TActorIterator<ACSCharacter> It(const_cast<UWorld*>(World)); It; ++It)
		{
			Count += (It->IsBot() && !It->IsActorBeingDestroyed()) ? 1 : 0;
		}
	}
	return Count;
}

void ACSBotManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	Accumulator += DeltaSeconds;
	if (Age < GStartDelay || Accumulator < GMaintainInterval)
	{
		return;
	}
	Accumulator = 0.f;
	Maintain();
}

void ACSBotManager::Maintain()
{
	if (!UCSAuthority::IsGameAuthority(this) || !ACSMatchDirector::Get(this))
	{
		return;
	}
	ACSGameState* GS = GetWorld()->GetGameState<ACSGameState>();
	if (!GS)
	{
		return;
	}

	// First authority to get here configures the match from its own choice.
	if (GS->GetBotCount() < 0)
	{
		int32 Count = 0;
		ECSBotDifficulty Difficulty = ECSBotDifficulty::Normal;
		if (const UCSSessionSubsystem* Session = GetGameInstance()->GetSubsystem<UCSSessionSubsystem>())
		{
			Session->GetMatchBotSettings(Count, Difficulty);
		}
		GS->SetBotSettings(Count, Difficulty);
	}

	TArray<ACSCharacter*> Bots;
	for (TActorIterator<ACSCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsBot() && !It->IsActorBeingDestroyed())
		{
			Bots.Add(*It);
			NextBotId = FMath::Max(NextBotId, It->GetOwningPlayerId() + 1);
		}
	}

	for (ACSCharacter* Bot : Bots)
	{
		EnsureController(Bot, GS->GetBotDifficulty());
	}

	int32 Wanted = GS->GetBotCount();
	const FCSModeRules& Rules = GS->GetRules();
	if (Rules.bBotsFillTeams)
	{
		// 5 vs 5: bots fill both teams up to TeamSize, whatever the menu said.
		int32 Humans = 0;
		for (const FCSPlayerCombatRecord& Record : ACSMatchDirector::Get(this)->GetAllRecords())
		{
			Humans += CSBots::IsBotId(Record.PlayerId) ? 0 : 1;
		}
		Wanted = FMath::Clamp(Rules.TeamSize * 2 - Humans, 0, 12);
	}
	if (Bots.Num() < Wanted)
	{
		// One per tick: spreads the spawn cost and the network burst.
		SpawnBot(NextBotId++);
	}
	else if (Bots.Num() > Wanted)
	{
		// From the bigger team, newest first.
		const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
		const ECSTeam Bigger = Director->CountMembers(ECSTeam::Alpha) >= Director->CountMembers(ECSTeam::Bravo) ? ECSTeam::Alpha : ECSTeam::Bravo;
		Bots.Sort([Director, Bigger](const ACSCharacter& A, const ACSCharacter& B)
		{
			const bool bA = Director->GetTeam(A.GetOwningPlayerId()) == Bigger;
			const bool bB = Director->GetTeam(B.GetOwningPlayerId()) == Bigger;
			return bA != bB ? bA : A.GetOwningPlayerId() > B.GetOwningPlayerId();
		});
		RemoveBot(Bots[0]);
	}
}

ACSCharacter* ACSBotManager::SpawnBot(int32 BotId)
{
	ACSGameMode* GameMode = GetWorld()->GetAuthGameMode<ACSGameMode>();
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	if (!GameMode || !Director)
	{
		return nullptr;
	}
	UClass* PawnClass = GameMode->DefaultPawnClass ? GameMode->DefaultPawnClass.Get() : ACSCharacter::StaticClass();
	if (!PawnClass->IsChildOf(ACSCharacter::StaticClass()))
	{
		PawnClass = ACSCharacter::StaticClass();
	}

	// Use the starts after the first few so bots do not spawn on top of the
	// first human players.
	FTransform Transform = FTransform::Identity;
	if (const AActor* Start = GameMode->GetPlayerStartByIndex(BotId - CSBots::FirstBotId + 4))
	{
		Transform = Start->GetActorTransform();
	}

	ACSCharacter* Bot = GetWorld()->SpawnActorDeferred<ACSCharacter>(PawnClass, Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Bot)
	{
		return nullptr;
	}
	Bot->InitAsBot(BotId);
	Bot->FinishSpawning(Transform);

	Director->EnsurePlayer(BotId);
	EnsureController(Bot, GetWorld()->GetGameState<ACSGameState>()->GetBotDifficulty());

	UE_LOG(LogCSAI, Log, TEXT("Spawned %s (id %d) at %s."), *GetBotName(BotId), BotId, *Transform.GetLocation().ToString());
	return Bot;
}

void ACSBotManager::EnsureController(ACSCharacter* Bot, ECSBotDifficulty Difficulty)
{
	ACSBotController* Controller = Cast<ACSBotController>(Bot->GetController());
	if (!Controller)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		Controller = GetWorld()->SpawnActor<ACSBotController>(ACSBotController::StaticClass(), Bot->GetActorTransform(), Params);
		if (!Controller)
		{
			return;
		}
		Controller->SetDifficulty(Difficulty);
		Controller->Possess(Bot);
		UE_LOG(LogCSAI, Log, TEXT("%s now controlled by this peer."), *GetBotName(Bot->GetOwningPlayerId()));
	}
	else if (Controller->GetDifficulty() != Difficulty)
	{
		Controller->SetDifficulty(Difficulty);
	}
}

void ACSBotManager::RemoveBot(ACSCharacter* Bot)
{
	const int32 BotId = Bot->GetOwningPlayerId();
	if (ACSMatchDirector* Director = ACSMatchDirector::Get(this))
	{
		// Same path as a player leaving: the inventory drops as loot, once.
		Director->RemovePlayer(BotId, ECSDeathReason::Disconnected);
	}
	if (AController* Controller = Bot->GetController())
	{
		Controller->UnPossess();
		Controller->Destroy();
	}
	Bot->Destroy();
	UE_LOG(LogCSAI, Log, TEXT("Removed %s (id %d)."), *GetBotName(BotId), BotId);
}
