// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Multiplayer/CSGameInstance.h"

// Required from UE 5.8: generated .gen.cpp files are no longer auto-scanned.
#include UE_INLINE_GENERATED_CPP_BY_NAME(CSGameInstance.fusion)

#include "Core/CSAuthority.h"
#include "Core/CSRpcGuard.h"
#include "Core/CSLog.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Multiplayer/CSSessionSubsystem.h"

void UCSGameInstance::Init()
{
	Super::Init();

	UE_LOG(LogCS, Log, TEXT("CSGameInstance initialised. Fusion backend: %s"),
		UCSSessionSubsystem::IsFusionAvailable() ? TEXT("ENABLED") : TEXT("DISABLED (offline)"));
}

void UCSGameInstance::OnStart()
{
	Super::OnStart();

	// Runs once the initial map is loaded, which is the earliest point where
	// joining a room makes sense.
	AutoConnectFromCommandLine();
}

void UCSGameInstance::AutoConnectFromCommandLine()
{
	const TCHAR* CmdLine = FCommandLine::Get();

	if (FParse::Param(CmdLine, TEXT("noautoconnect")))
	{
		UE_LOG(LogCSNet, Log, TEXT("Auto-connect disabled by -noautoconnect; staying offline."));
		return;
	}

	UCSSessionSubsystem* Session = GetSessionSubsystem();
	if (!Session)
	{
		UE_LOG(LogCSNet, Error, TEXT("Auto-connect: session subsystem unavailable."));
		return;
	}

	if (Session->IsInRoom())
	{
		return;
	}

	FCSSessionRequest Request;

	// Since Stage 5 the main menu is the normal way in. Auto-connect remains
	// for scripted tests and dedicated shortcuts: it only happens when a room
	// is named on the command line.
	if (!FParse::Value(CmdLine, TEXT("room="), Request.RoomName) || Request.RoomName.IsEmpty())
	{
		UE_LOG(LogCSNet, Log, TEXT("No -room= on the command line; showing the main menu."));
		return;
	}

	Request.MaxPlayers = 8;
	FParse::Value(CmdLine, TEXT("maxplayers="), Request.MaxPlayers);

	FString Region;
	if (FParse::Value(CmdLine, TEXT("region="), Region) && !Region.IsEmpty())
	{
		Request.Region = Region;
		Request.bSelectRegion = true;
	}

	// InitialWorld has to be set: FusionMatchmakingHelpers::CreatePhotonRoomOptions
	// only writes the room's MAP_DATA custom property when it is non-null, and
	// that property is how Fusion attaches a map to a room. Leaving it unset
	// creates a room with no map bound to it.
	Request.InitialWorld = UCSSessionSubsystem::DefaultMatchWorld();

	Request.EmptyTtlSeconds = 0;
	// Scripted rooms start without bots unless -bots=N asks for them.
	Request.BotCount = 0;
	Request.bVisible = true;

	UE_LOG(LogCSNet, Log,
		TEXT("Auto-connect: joining room '%s' (max %d, region '%s'). Pass -noautoconnect to skip."),
		*Request.RoomName, Request.MaxPlayers,
		Request.bSelectRegion ? *Request.Region : TEXT("default"));

	Session->HostOrJoin(Request);
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
	if (!CSRpcGuard::FromMasterClient(this, TEXT("RpcAnnouncement")))
	{
		return;
	}
	UE_LOG(LogCSNet, Log, TEXT("[Announcement] %s"), *Message);
	OnAnnouncement.Broadcast(Message);
}
