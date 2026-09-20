// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Multiplayer/CSSessionSubsystem.h"

#include "Core/CSFusionCompat.h"
#include "Core/CSLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
	constexpr float GSessionPollInterval = 0.1f;

#if CS_WITH_FUSION
	/** Translate Fusion's status enum into the project-facing one. */
	ECSSessionState TranslateStatus(EFusionStatus Status)
	{
		switch (Status)
		{
		case EFusionStatus::Connecting:		return ECSSessionState::Connecting;
		case EFusionStatus::Connected:		return ECSSessionState::Connected;
		case EFusionStatus::JoiningRoom:	return ECSSessionState::JoiningRoom;
		case EFusionStatus::InRoom:			return ECSSessionState::InRoom;
		case EFusionStatus::LeavingRoom:	return ECSSessionState::LeavingRoom;
		case EFusionStatus::Disconnected:	return ECSSessionState::Disconnected;
		case EFusionStatus::Error:			return ECSSessionState::Error;
		case EFusionStatus::None:
		default:							return ECSSessionState::None;
		}
	}
#endif
}

void UCSSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if CS_WITH_FUSION
	// Make sure Fusion's own subsystem is constructed before ours so that
	// GetSubsystem() below never returns null mid-flow.
	Collection.InitializeDependency(UFusionOnlineSubsystem::StaticClass());
	UE_LOG(LogCSNet, Log, TEXT("CSSessionSubsystem initialised (Photon Fusion 3 backend)."));
#else
	UE_LOG(LogCSNet, Warning,
		TEXT("CSSessionSubsystem initialised WITHOUT Photon Fusion. All session ")
		TEXT("calls will fail; gameplay runs standalone. See docs/PHOTON_SETUP.md."));
#endif
}

void UCSSessionSubsystem::Deinitialize()
{
	StopPolling();
	Super::Deinitialize();
}

bool UCSSessionSubsystem::IsFusionAvailable()
{
#if CS_WITH_FUSION
	return true;
#else
	return false;
#endif
}

UFusionOnlineSubsystem* UCSSessionSubsystem::GetFusion() const
{
#if CS_WITH_FUSION
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UFusionOnlineSubsystem>() : nullptr;
#else
	return nullptr;
#endif
}

int32 UCSSessionSubsystem::GetRttMs() const
{
#if CS_WITH_FUSION
	if (UFusionOnlineSubsystem* Fusion = GetFusion())
	{
		return Fusion->GetRtt();
	}
#endif
	return 0;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

bool UCSSessionSubsystem::HostOrJoin(const FCSSessionRequest& Request)
{
	return StartRoomOperation(Request, /*bAllowCreate*/ true, /*bRandom*/ false);
}

bool UCSSessionSubsystem::QuickMatch(const FCSSessionRequest& Request)
{
	return StartRoomOperation(Request, /*bAllowCreate*/ true, /*bRandom*/ true);
}

bool UCSSessionSubsystem::JoinByName(const FCSSessionRequest& Request)
{
	if (Request.RoomName.IsEmpty())
	{
		Fail(TEXT("JoinByName requires a room name."));
		return false;
	}
	return StartRoomOperation(Request, /*bAllowCreate*/ false, /*bRandom*/ false);
}

// ---------------------------------------------------------------------------
// The single Fusion matchmaking touch point.
//
// Everything below this line that is version-sensitive lives here. If the
// preview SDK renames a field or a method, this is the only function to fix.
// ---------------------------------------------------------------------------
bool UCSSessionSubsystem::StartRoomOperation(const FCSSessionRequest& Request, bool bAllowCreate, bool bRandom)
{
	if (bOperationInFlight)
	{
		UE_LOG(LogCSNet, Warning, TEXT("Session operation already in flight; request ignored."));
		return false;
	}

#if !CS_WITH_FUSION
	Fail(TEXT("Photon Fusion 3 SDK is not installed. Drop it into Plugins/PhotonFusion and rebuild."));
	return false;
#else
	UFusionOnlineSubsystem* Fusion = GetFusion();
	if (!Fusion)
	{
		Fail(TEXT("UFusionOnlineSubsystem is unavailable."));
		return false;
	}

	if (Fusion->IsJoiningOrInRoom())
	{
		Fail(TEXT("Already in a room. Leave the current match first."));
		return false;
	}

	PendingRequest = Request;
	LastError.Reset();
	bJoinByNameIssued = false;

	FFusionConnectOptions ConnectOptions;
	ConnectOptions.Region = Request.Region;
	ConnectOptions.RegionSelectionMode = Request.bSelectRegion
		? EFusionRegionSelectionMode::Select
		: EFusionRegionSelectionMode::Best;

	FFusionRoomOptions RoomOptions;
	RoomOptions.RoomName = Request.RoomName;
	RoomOptions.MaxPlayers = static_cast<uint8>(FMath::Clamp(Request.MaxPlayers, 2, 16));
	RoomOptions.bIsOpen = true;
	RoomOptions.bIsVisible = Request.bVisible;
	RoomOptions.EmptyTTL = static_cast<uint8>(FMath::Clamp(Request.EmptyTtlSeconds, 0, 255));
	RoomOptions.InitialWorld = Request.InitialWorld;

	// ConnectAndJoinRoom chains connect + join + StartFusionSession in one
	// action, which is exactly the flow the menu needs. It returns a
	// UFusionConnectAndJoinRoomAsync; we deliberately ignore the returned
	// latent action and drive our own state machine off Status().
	UObject* WorldContext = GetGameInstance();

	if (bAllowCreate)
	{
		// Quick match and host-or-join share the same entry point: an empty
		// RoomName makes Photon pick/generate one, a filled one makes the room
		// joinable by name for friends.
		if (bRandom)
		{
			RoomOptions.RoomName.Reset();
		}
		Fusion->ConnectAndJoinRoom(ConnectOptions, RoomOptions, WorldContext);
	}
	else
	{
		// Join-only: connect first, then JoinRoom by exact name once connected.
		// PollSession() performs the second half when Status() reaches Connected.
		Fusion->ConnectToPhoton(ConnectOptions, WorldContext);
	}

	bOperationInFlight = true;
	StartPolling();

	UE_LOG(LogCSNet, Log,
		TEXT("Session request started. Room='%s' Max=%d Region='%s' Mode=%s"),
		*Request.RoomName, RoomOptions.MaxPlayers, *Request.Region,
		bRandom ? TEXT("QuickMatch") : (bAllowCreate ? TEXT("HostOrJoin") : TEXT("JoinByName")));

	return true;
#endif
}

void UCSSessionSubsystem::LeaveMatch()
{
#if CS_WITH_FUSION
	if (UFusionOnlineSubsystem* Fusion = GetFusion())
	{
		if (Fusion->IsJoiningOrInRoom())
		{
			UE_LOG(LogCSNet, Log, TEXT("Leaving room '%s'."), *CachedRoomName);
			Fusion->LeaveRoom(GetGameInstance());
			StartPolling();
			return;
		}
	}
#endif
	SetState(ECSSessionState::None);
}

void UCSSessionSubsystem::Disconnect()
{
#if CS_WITH_FUSION
	if (UFusionOnlineSubsystem* Fusion = GetFusion())
	{
		UE_LOG(LogCSNet, Log, TEXT("Disconnecting from Photon."));
		Fusion->DisconnectFromPhoton(GetGameInstance());
		StartPolling();
		return;
	}
#endif
	SetState(ECSSessionState::None);
}

// ---------------------------------------------------------------------------
// Polling state machine
// ---------------------------------------------------------------------------

void UCSSessionSubsystem::StartPolling()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	if (!World->GetTimerManager().IsTimerActive(PollTimerHandle))
	{
		World->GetTimerManager().SetTimer(
			PollTimerHandle, this, &UCSSessionSubsystem::PollSession,
			GSessionPollInterval, /*bLoop*/ true);
	}
}

void UCSSessionSubsystem::StopPolling()
{
	if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		World->GetTimerManager().ClearTimer(PollTimerHandle);
	}
}

void UCSSessionSubsystem::PollSession()
{
#if CS_WITH_FUSION
	UFusionOnlineSubsystem* Fusion = GetFusion();
	if (!Fusion)
	{
		StopPolling();
		return;
	}

	const ECSSessionState NewState = TranslateStatus(Fusion->Status());

	// Deferred second half of JoinByName: we are connected, now join the room.
	if (NewState == ECSSessionState::Connected
		&& bOperationInFlight
		&& !bJoinByNameIssued
		&& !PendingRequest.RoomName.IsEmpty())
	{
		bJoinByNameIssued = true;
		Fusion->JoinRoom(PendingRequest.RoomName, GetGameInstance());
	}

	SetState(NewState);

	if (NewState == ECSSessionState::InRoom)
	{
		int32 Players = 0;
		FString RoomName;
		Fusion->CurrentRoomInfo(RoomName, Players);
		CachedRoomName = RoomName;
		CachedPlayerCount = Players;

		const bool bIsMaster = Fusion->IsMasterClient();
		if (bIsMaster != bCachedIsMaster)
		{
			bCachedIsMaster = bIsMaster;
			UE_LOG(LogCSAuth, Log,
				TEXT("Master Client changed: local player is %s the authority."),
				bIsMaster ? TEXT("NOW") : TEXT("NO LONGER"));
			OnMasterClientChanged.Broadcast(bIsMaster);
		}
	}
	else if (NewState == ECSSessionState::Error || NewState == ECSSessionState::Disconnected)
	{
		if (bOperationInFlight)
		{
			Fail(NewState == ECSSessionState::Error
				? TEXT("Photon reported an error.")
				: TEXT("Disconnected from Photon."));
		}
	}
	else if (NewState == ECSSessionState::None)
	{
		StopPolling();
	}
#else
	StopPolling();
#endif
}

void UCSSessionSubsystem::SetState(ECSSessionState NewState)
{
	if (NewState == CachedState)
	{
		return;
	}

	const ECSSessionState OldState = CachedState;
	CachedState = NewState;

	UE_LOG(LogCSNet, Log, TEXT("Session state: %s -> %s"),
		*UEnum::GetValueAsString(OldState), *UEnum::GetValueAsString(NewState));

	OnSessionStateChanged.Broadcast(NewState);

	if (NewState == ECSSessionState::InRoom)
	{
		bOperationInFlight = false;
		OnSessionJoined.Broadcast();
	}
	else if (OldState == ECSSessionState::InRoom && NewState != ECSSessionState::LeavingRoom)
	{
		bCachedIsMaster = false;
		CachedRoomName.Reset();
		CachedPlayerCount = 0;
		OnSessionLeft.Broadcast();
	}
	else if (OldState == ECSSessionState::LeavingRoom)
	{
		bCachedIsMaster = false;
		CachedRoomName.Reset();
		CachedPlayerCount = 0;
		OnSessionLeft.Broadcast();
	}
}

void UCSSessionSubsystem::Fail(const FString& Reason)
{
	bOperationInFlight = false;
	LastError = Reason;
	UE_LOG(LogCSNet, Error, TEXT("Session failure: %s"), *Reason);
	OnSessionFailed.Broadcast(Reason);
}
