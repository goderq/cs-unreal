// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Multiplayer/CSGameInstance.h"

#if CS_WITH_FUSION
#include UE_INLINE_GENERATED_CPP_BY_NAME(CSGameInstance.fusion)
#endif

#include "Core/CSAuthority.h"
#include "Core/CSLog.h"
#include "Multiplayer/CSSessionSubsystem.h"

void UCSGameInstance::Init()
{
	Super::Init();

	UE_LOG(LogCS, Log, TEXT("CSGameInstance initialised. Fusion backend: %s"),
		UCSSessionSubsystem::IsFusionAvailable() ? TEXT("ENABLED") : TEXT("DISABLED (offline)"));
}

void UCSGameInstance::Shutdown()
{
	// Leaving the room cleanly here gives the Master Client a chance to run
	// disconnect loot (Stage 4) before the process dies.
	if (UCSSessionSubsystem* Session = GetSessionSubsystem())
	{
		if (Session->IsInRoom())
		{
			Session->LeaveMatch();
		}
	}

	Super::Shutdown();
}

UCSSessionSubsystem* UCSGameInstance::GetSessionSubsystem() const
{
	return const_cast<UCSGameInstance*>(this)->GetSubsystem<UCSSessionSubsystem>();
}

void UCSGameInstance::BroadcastAnnouncement(const FString& Message)
{
	// Announcements are a room-wide side effect, so only the authority may
	// originate one. A client that calls this locally is simply ignored.
	if (!UCSAuthority::IsGameAuthority(this))
	{
		UE_LOG(LogCSAuth, Verbose,
			TEXT("BroadcastAnnouncement rejected: local peer is not the authority."));
		return;
	}

#if CS_WITH_FUSION
	if (UCSAuthority::IsSessionActive(this))
	{
		RpcAnnouncement(Message);
		return;
	}
#endif

	// Offline / no session: deliver straight to the receive handler so that
	// single-process testing behaves identically.
	RpcAnnouncement_Receive(Message);
}

void UCSGameInstance::RpcAnnouncement_Receive(const FString& Message)
{
	UE_LOG(LogCSNet, Log, TEXT("[Announcement] %s"), *Message);
	OnAnnouncement.Broadcast(Message);
}
