// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "GameModes/CSGameState.h"

#include "Core/CSAuthority.h"
#include "Core/CSLog.h"
#include "Core/CSModeSettings.h"
#include "Net/UnrealNetwork.h"

ACSGameState::ACSGameState()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;
}

void ACSGameState::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogCS, Log, TEXT("CSGameState ready. Local peer is authority: %s"),
		UCSAuthority::IsGameAuthority(this) ? TEXT("YES") : TEXT("no"));
}

void ACSGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACSGameState, MatchPhase);
	DOREPLIFETIME(ACSGameState, PhaseEndNetworkTime);
	DOREPLIFETIME(ACSGameState, ScoreAlpha);
	DOREPLIFETIME(ACSGameState, ScoreBravo);
	DOREPLIFETIME(ACSGameState, BotCount);
	DOREPLIFETIME(ACSGameState, BotDifficulty);
	DOREPLIFETIME(ACSGameState, GameModeType);
	DOREPLIFETIME(ACSGameState, bModeConfigured);
	DOREPLIFETIME(ACSGameState, RoundNumber);
	DOREPLIFETIME(ACSGameState, BuyEndNetworkTime);
	DOREPLIFETIME(ACSGameState, WinnerTeam);
	DOREPLIFETIME(ACSGameState, WinnerPlayerId);
	DOREPLIFETIME(ACSGameState, LossStreakAlpha);
	DOREPLIFETIME(ACSGameState, LossStreakBravo);
	DOREPLIFETIME(ACSGameState, BackendMatchId);
}

void ACSGameState::SetBackendMatchId(const FString& MatchId)
{
	CS_AUTHORITY_ONLY(this);
	BackendMatchId = MatchId.Left(64);
}

void ACSGameState::SetMatchPhase(ECSMatchPhase NewPhase)
{
	CS_AUTHORITY_ONLY(this);

	if (MatchPhase == NewPhase)
	{
		return;
	}

	MatchPhase = NewPhase;

	// Replication only notifies remote peers; the writer runs it by hand so
	// authority and clients take the same code path.
	OnRep_MatchPhase();
}

void ACSGameState::SetPhaseEndTime(double NetworkTimeSeconds)
{
	CS_AUTHORITY_ONLY(this);
	PhaseEndNetworkTime = NetworkTimeSeconds;
}

void ACSGameState::AddTeamScore(ECSTeam Team, int32 Delta)
{
	CS_AUTHORITY_ONLY(this);

	switch (Team)
	{
	case ECSTeam::Alpha:	ScoreAlpha = FMath::Max(0, ScoreAlpha + Delta); break;
	case ECSTeam::Bravo:	ScoreBravo = FMath::Max(0, ScoreBravo + Delta); break;
	default:				break;
	}
}

float ACSGameState::GetPhaseTimeRemaining() const
{
	if (PhaseEndNetworkTime <= 0.0)
	{
		return 0.f;
	}

	const double Now = UCSAuthority::GetNetworkTimeSeconds(this);
	return static_cast<float>(FMath::Max(0.0, PhaseEndNetworkTime - Now));
}

int32 ACSGameState::GetTeamScore(ECSTeam Team) const
{
	switch (Team)
	{
	case ECSTeam::Alpha:	return ScoreAlpha;
	case ECSTeam::Bravo:	return ScoreBravo;
	default:				return 0;
	}
}

void ACSGameState::OnRep_MatchPhase()
{
	UE_LOG(LogCS, Log, TEXT("Match phase -> %s"), *UEnum::GetValueAsString(MatchPhase));
	OnMatchPhaseChanged.Broadcast(MatchPhase);
}

void ACSGameState::SetBotSettings(int32 Count, ECSBotDifficulty Difficulty)
{
	CS_AUTHORITY_ONLY(this);
	BotCount = FMath::Clamp(Count, 0, 8);
	BotDifficulty = Difficulty;
	UE_LOG(LogCSAI, Log, TEXT("Match bots: %d, difficulty %s."), BotCount, *UEnum::GetValueAsString(BotDifficulty));
}

// ---------------------------------------------------------------------------
// Game mode (v1.1)
// ---------------------------------------------------------------------------

void ACSGameState::ConfigureMode(ECSGameModeType Mode)
{
	CS_AUTHORITY_ONLY(this);
	GameModeType = Mode;
	bModeConfigured = true;
	UE_LOG(LogCS, Log, TEXT("Match mode: %s."), *UCSModeSettings::ModeTag(Mode));
}

const FCSModeRules& ACSGameState::GetRules() const
{
	return UCSModeSettings::Rules(GameModeType);
}

float ACSGameState::GetBuyTimeRemaining() const
{
	if (BuyEndNetworkTime <= 0.0)
	{
		return 0.f;
	}
	return static_cast<float>(FMath::Max(0.0, BuyEndNetworkTime - UCSAuthority::GetNetworkTimeSeconds(this)));
}

void ACSGameState::SetRoundNumber(int32 Round)
{
	CS_AUTHORITY_ONLY(this);
	RoundNumber = Round;
}

void ACSGameState::SetBuyEndNetworkTime(double Time)
{
	CS_AUTHORITY_ONLY(this);
	BuyEndNetworkTime = Time;
}

void ACSGameState::SetWinner(ECSTeam Team, int32 PlayerId)
{
	CS_AUTHORITY_ONLY(this);
	WinnerTeam = Team;
	WinnerPlayerId = PlayerId;
}

void ACSGameState::SetLossStreak(ECSTeam Team, int32 Streak)
{
	CS_AUTHORITY_ONLY(this);
	(Team == ECSTeam::Alpha ? LossStreakAlpha : LossStreakBravo) = FMath::Max(0, Streak);
}

void ACSGameState::ResetTeamScores()
{
	CS_AUTHORITY_ONLY(this);
	ScoreAlpha = 0;
	ScoreBravo = 0;
}
