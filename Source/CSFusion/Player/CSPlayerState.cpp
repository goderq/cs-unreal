// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Player/CSPlayerState.h"

#include "Core/CSAuthority.h"
#include "Core/CSLog.h"
#include "Net/UnrealNetwork.h"

ACSPlayerState::ACSPlayerState()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;

	// Fusion attaches its own UFusionActorComponent (PlayerAttached) to every
	// PlayerState automatically, so none is added here.
}

void ACSPlayerState::BeginPlay()
{
	Super::BeginPlay();

	// Only the owning client may write this actor, so only it stamps its id.
	if (UCSAuthority::CanWrite(this))
	{
		PhotonPlayerId = UCSAuthority::GetLocalPlayerId(this);

		if (GetPlayerName().IsEmpty())
		{
			SetPlayerName(FString::Printf(TEXT("Player %d"), PhotonPlayerId));
		}

		UE_LOG(LogCS, Log, TEXT("Local PlayerState ready: id=%d name=%s"),
			PhotonPlayerId, *GetPlayerName());
	}
}

void ACSPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACSPlayerState, PhotonPlayerId);
	DOREPLIFETIME(ACSPlayerState, Team);
	DOREPLIFETIME(ACSPlayerState, bIsBot);
}

FString ACSPlayerState::GetDisplayName() const
{
	const FString Name = GetPlayerName();
	return Name.IsEmpty() ? FString::Printf(TEXT("Player %d"), PhotonPlayerId) : Name;
}

void ACSPlayerState::OnRep_Team()
{
	UE_LOG(LogCS, Verbose, TEXT("%s joined team %s"),
		*GetDisplayName(), *UEnum::GetValueAsString(Team));
}
