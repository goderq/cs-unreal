// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Multiplayer/CSGameInstance.h"

// Required from UE 5.8: generated .gen.cpp files are no longer auto-scanned.
#include UE_INLINE_GENERATED_CPP_BY_NAME(CSGameInstance.fusion)

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

	// Fusion requires FString RPC parameters to be non-const references, so the
	// const& public API is copied into a local before it goes on the wire.
	FString Payload = Message;

	// Runtime, not compile-time, fallback: with no room joined there is nobody
	// to send to, so deliver straight to the receive handler. Single-process
	// testing then exercises exactly the same handler as networked play.
	if (UCSAuthority::IsSessionActive(this))
	{
		RpcAnnouncement(Payload);
		return;
	}

	RpcAnnouncement_Receive(Payload);
}

void UCSGameInstance::RpcAnnouncement_Receive(FString& Message)
{
	UE_LOG(LogCSNet, Log, TEXT("[Announcement] %s"), *Message);
	OnAnnouncement.Broadcast(Message);
}
