// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "GameModes/CSGameState.h"

#include "Core/CSAuthority.h"
#include "Core/CSLog.h"
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
