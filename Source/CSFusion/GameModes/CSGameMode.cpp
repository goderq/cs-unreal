// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "GameModes/CSGameMode.h"

#include "Characters/CSCharacter.h"
#include "Core/CSAuthority.h"
#include "Core/CSLog.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameModes/CSGameState.h"
#include "Multiplayer/CSGameInstance.h"
#include "Player/CSPlayerController.h"
#include "Player/CSPlayerState.h"

ACSGameMode::ACSGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f; // Match flow does not need frame rate.

	GameStateClass = ACSGameState::StaticClass();
	PlayerStateClass = ACSPlayerState::StaticClass();
	PlayerControllerClass = ACSPlayerController::StaticClass();
	DefaultPawnClass = ACSCharacter::StaticClass();

	bStartPlayersAsSpectators = false;
}

void ACSGameMode::BeginPlay()
{
	Super::BeginPlay();

	CachePlayerStarts();

	UE_LOG(LogCS, Log,
		TEXT("CSGameMode BeginPlay. Authority: %s. PlayerStarts found: %d"),
		UCSAuthority::IsGameAuthority(this) ? TEXT("YES") : TEXT("no"),
		CachedPlayerStarts.Num());

	if (UCSAuthority::IsGameAuthority(this))
	{
		if (ACSGameState* GS = GetCSGameState())
		{
			GS->SetMatchPhase(ECSMatchPhase::WaitingForPlayers);
		}
	}
}

void ACSGameMode::CachePlayerStarts()
{
	CachedPlayerStarts.Reset();

	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		CachedPlayerStarts.Add(*It);
	}

	// Actor iteration order is not guaranteed to match between peers, so sort
	// by a value every peer agrees on. Spawn indices must be stable because
	// each client picks its own start with no negotiation.
	CachedPlayerStarts.Sort([](const AActor& A, const AActor& B)
	{
		return A.GetName() < B.GetName();
	});
}

AActor* ACSGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (CachedPlayerStarts.Num() == 0)
	{
		CachePlayerStarts();
	}

	if (CachedPlayerStarts.Num() == 0)
	{
		UE_LOG(LogCS, Warning, TEXT("No APlayerStart in the level; falling back to engine default."));
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	// Key the start off the Photon player id. Every peer derives the same
	// index for the same player, so two clients spawning simultaneously do not
	// land on the same pad as long as there are enough starts.
	const int32 PlayerId = UCSAuthority::GetLocalPlayerId(this);
	const int32 Index = FMath::Abs(PlayerId) % CachedPlayerStarts.Num();

	UE_LOG(LogCS, Verbose, TEXT("ChoosePlayerStart: PlayerId=%d -> start #%d (%s)"),
		PlayerId, Index, *GetNameSafe(CachedPlayerStarts[Index]));

	return CachedPlayerStarts[Index];
}

void ACSGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// NOT authority-gated on purpose: under Fusion each client spawns its own
	// controller and pawn locally. See the class comment.
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

void ACSGameMode::RestartGame()
{
	// Authority-gated: without this every peer restarts the round and the
	// effects multiply by player count.
	CS_AUTHORITY_ONLY(this);

	UE_LOG(LogCS, Log, TEXT("Authority restarting the round."));
	Super::RestartGame();
}

ACSGameState* ACSGameMode::GetCSGameState() const
{
	return GetWorld() ? GetWorld()->GetGameState<ACSGameState>() : nullptr;
}

void ACSGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateMatchFlow();
}

void ACSGameMode::UpdateMatchFlow()
{
	CS_AUTHORITY_ONLY(this);

	ACSGameState* GS = GetCSGameState();
	if (!GS)
	{
		return;
	}

	const double Now = UCSAuthority::GetNetworkTimeSeconds(this);
	const int32 PlayerCount = UCSAuthority::GetRoomPlayerCount(this);

	switch (GS->GetMatchPhase())
	{
	case ECSMatchPhase::WaitingForPlayers:
		if (PlayerCount >= MinPlayersToStart)
		{
			GS->SetPhaseEndTime(Now + WarmupSeconds);
			GS->SetMatchPhase(ECSMatchPhase::Warmup);

			if (UCSGameInstance* GI = GetGameInstance<UCSGameInstance>())
			{
				GI->BroadcastAnnouncement(TEXT("Warmup started."));
			}
		}
		break;

	case ECSMatchPhase::Warmup:
		if (GS->GetPhaseTimeRemaining() <= 0.f)
		{
			GS->SetPhaseEndTime(Now + RoundSeconds);
			GS->SetMatchPhase(ECSMatchPhase::InProgress);

			if (UCSGameInstance* GI = GetGameInstance<UCSGameInstance>())
			{
				GI->BroadcastAnnouncement(TEXT("Round started. Good luck."));
			}
		}
		break;

	case ECSMatchPhase::InProgress:
		if (GS->GetPhaseTimeRemaining() <= 0.f)
		{
			GS->SetPhaseEndTime(Now + PostMatchSeconds);
			GS->SetMatchPhase(ECSMatchPhase::PostMatch);

			if (UCSGameInstance* GI = GetGameInstance<UCSGameInstance>())
			{
				GI->BroadcastAnnouncement(TEXT("Round over."));
			}
		}
		break;

	case ECSMatchPhase::PostMatch:
		if (GS->GetPhaseTimeRemaining() <= 0.f)
		{
			GS->SetMatchPhase(ECSMatchPhase::WaitingForPlayers);
		}
		break;
	}
}
