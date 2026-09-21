// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Multiplayer/CSSessionSubsystem.h"

#include "Containers/Ticker.h"
#include "Core/CSFusionCompat.h"
#include "Core/CSLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	constexpr float GSessionPollInterval = 0.1f;

	/** How long LeaveToMainMenu waits for Photon before opening the menu anyway. */
	constexpr double GReturnToMenuTimeout = 5.0;

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

	PhotonMatchmaking::RealtimeClient* GetPhotonClient(UFusionOnlineSubsystem* Fusion)
	{
		UFusionRealtimeClient* Realtime = Fusion ? Fusion->GetRealtimeClient() : nullptr;
		return Realtime ? Realtime->GetClient() : nullptr;
	}

	FString ToFString(const PhotonCommon::StringType& In)
	{
		return FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(In.c_str())));
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

TSoftObjectPtr<UWorld> UCSSessionSubsystem::DefaultMatchWorld()
{
	// Must be set on every room: FusionMatchmakingHelpers::CreatePhotonRoomOptions
	// only writes the room's MAP_DATA custom property when it is non-null, and
	// that property is how Fusion attaches a map to a room and makes joiners
	// load it.
	return TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/Lvl_Warehouse.Lvl_Warehouse")));
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
		// GetRtt() is a double in the SDK.
		return FMath::RoundToInt32(Fusion->GetRtt());
	}
#endif
	return 0;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

bool UCSSessionSubsystem::HostOrJoin(const FCSSessionRequest& Request)
{
	if (Request.RoomName.IsEmpty())
	{
		Fail(TEXT("Creating a session requires a room name."));
		return false;
	}
	return StartRoomOperation(Request, EPendingRoomOp::JoinOrCreate);
}

bool UCSSessionSubsystem::QuickMatch(const FCSSessionRequest& Request)
{
	return StartRoomOperation(Request, EPendingRoomOp::JoinRandomOrCreate);
}

bool UCSSessionSubsystem::JoinByName(const FCSSessionRequest& Request)
{
	if (Request.RoomName.IsEmpty())
	{
		Fail(TEXT("Joining a session requires a room name."));
		return false;
	}
	return StartRoomOperation(Request, EPendingRoomOp::JoinOnly);
}

// ---------------------------------------------------------------------------
// The Fusion matchmaking touch points.
//
// Everything below this line that is version-sensitive lives here. If the
// preview SDK renames a field or a method, these are the functions to fix.
// ---------------------------------------------------------------------------
bool UCSSessionSubsystem::StartRoomOperation(const FCSSessionRequest& Request, EPendingRoomOp Op)
{
	if (bOperationInFlight || bReturningToMenu)
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
	if (PendingRequest.InitialWorld.IsNull())
	{
		PendingRequest.InitialWorld = DefaultMatchWorld();
	}
	PendingOp = Op;
	LastError.Reset();
	bOperationInFlight = true;
	StartPolling();

	UE_LOG(LogCSNet, Log,
		TEXT("Session request started. Room='%s' Max=%d Region='%s' Mode=%s Connected=%s"),
		*Request.RoomName, Request.MaxPlayers, *Request.Region,
		Op == EPendingRoomOp::JoinRandomOrCreate ? TEXT("QuickMatch")
			: (Op == EPendingRoomOp::JoinOrCreate ? TEXT("HostOrJoin") : TEXT("JoinByName")),
		Fusion->IsConnected() ? TEXT("yes") : TEXT("no"));

	if (Fusion->IsConnected())
	{
		// Already connected (the browser keeps the connection): join straight away.
		IssuePendingRoomOp();
	}
	else
	{
		Connect(PendingRequest);
	}
	return true;
#endif
}

void UCSSessionSubsystem::Connect(const FCSSessionRequest& Request)
{
#if CS_WITH_FUSION
	UFusionOnlineSubsystem* Fusion = GetFusion();
	if (!Fusion)
	{
		return;
	}

	FFusionConnectOptions ConnectOptions;
	ConnectOptions.Region = Request.Region;
	ConnectOptions.RegionSelectionMode = Request.bSelectRegion && !Request.Region.IsEmpty()
		? EFusionRegionSelectionMode::Select
		: EFusionRegionSelectionMode::Best;

	Fusion->ConnectToPhoton(ConnectOptions, GetGameInstance());
#endif
}

void UCSSessionSubsystem::IssuePendingRoomOp()
{
#if CS_WITH_FUSION
	UFusionOnlineSubsystem* Fusion = GetFusion();
	const EPendingRoomOp Op = PendingOp;
	PendingOp = EPendingRoomOp::None;
	if (!Fusion || Op == EPendingRoomOp::None)
	{
		return;
	}

	FFusionRoomOptions RoomOptions;
	RoomOptions.RoomName = PendingRequest.RoomName;
	RoomOptions.MaxPlayers = static_cast<uint8>(FMath::Clamp(PendingRequest.MaxPlayers, 2, 16));
	RoomOptions.bIsOpen = true;
	RoomOptions.bIsVisible = PendingRequest.bVisible;
	RoomOptions.EmptyTTL = static_cast<uint8>(FMath::Clamp(PendingRequest.EmptyTtlSeconds, 0, 255));
	RoomOptions.InitialWorld = PendingRequest.InitialWorld;

	UObject* WorldContext = GetGameInstance();
	switch (Op)
	{
	case EPendingRoomOp::JoinOrCreate:
		UE_LOG(LogCSNet, Log, TEXT("Connected; join-or-create room '%s'."), *RoomOptions.RoomName);
		Fusion->JoinOrCreateRoom(RoomOptions, WorldContext);
		break;

	case EPendingRoomOp::JoinRandomOrCreate:
		// The name is only used if no open room exists and one gets created.
		RoomOptions.RoomName.Reset();
		UE_LOG(LogCSNet, Log, TEXT("Connected; join random room or create one."));
		Fusion->JoinOrCreateRandomRoom(RoomOptions, WorldContext);
		break;

	case EPendingRoomOp::JoinOnly:
		UE_LOG(LogCSNet, Log, TEXT("Connected; join-only for room '%s'."), *RoomOptions.RoomName);
		Fusion->JoinRoom(RoomOptions.RoomName, WorldContext);
		break;

	default:
		break;
	}
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

void UCSSessionSubsystem::LeaveToMainMenu()
{
	if (bReturningToMenu)
	{
		return;
	}

	UE_LOG(LogCSNet, Log, TEXT("Leaving match '%s' for the main menu."), *CachedRoomName);

	bBrowsing = false;
	PendingOp = EPendingRoomOp::None;
	bOperationInFlight = false;

#if CS_WITH_FUSION
	UFusionOnlineSubsystem* Fusion = GetFusion();
	if (Fusion && Fusion->Status() != EFusionStatus::None)
	{
		bReturningToMenu = true;
		ReturnToMenuDeadline = FPlatformTime::Seconds() + GReturnToMenuTimeout;
		Fusion->DisconnectFromPhoton(GetGameInstance());
		StartPolling();
		return;
	}
#endif

	OpenMainMenu();
}

void UCSSessionSubsystem::OpenMainMenu()
{
	bReturningToMenu = false;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UWorld* World = GI->GetWorld())
		{
			UGameplayStatics::OpenLevel(World, FName(MainMenuMapPath()));
		}
	}
}

void UCSSessionSubsystem::Disconnect()
{
	bBrowsing = false;
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
// Session browser
// ---------------------------------------------------------------------------

void UCSSessionSubsystem::StartBrowsing(const FString& Region)
{
#if CS_WITH_FUSION
	UFusionOnlineSubsystem* Fusion = GetFusion();
	if (!Fusion || bReturningToMenu)
	{
		return;
	}

	bBrowsing = true;
	bLobbyJoinIssued = false;
	RoomList.Reset();
	OnRoomListChanged.Broadcast();
	StartPolling();

	if (!Fusion->IsConnected() && !bOperationInFlight
		&& Fusion->Status() != EFusionStatus::Connecting)
	{
		FCSSessionRequest Request;
		Request.Region = Region;
		Request.bSelectRegion = !Region.IsEmpty();
		PendingRequest = Request;
		UE_LOG(LogCSNet, Log, TEXT("Browser: connecting to Photon (region '%s')."),
			Region.IsEmpty() ? TEXT("best") : *Region);
		Connect(Request);
	}
#endif
}

void UCSSessionSubsystem::StopBrowsing()
{
	bBrowsing = false;
}

bool UCSSessionSubsystem::IsInLobby() const
{
#if CS_WITH_FUSION
	PhotonMatchmaking::RealtimeClient* Client = GetPhotonClient(GetFusion());
	return Client && Client->IsInLobby();
#else
	return false;
#endif
}

void UCSSessionSubsystem::PollLobby()
{
#if CS_WITH_FUSION
	UFusionOnlineSubsystem* Fusion = GetFusion();
	PhotonMatchmaking::RealtimeClient* Client = GetPhotonClient(Fusion);
	if (!Client || !Fusion->IsConnected() || Fusion->IsJoiningOrInRoom())
	{
		return;
	}

	if (!Client->IsInLobby())
	{
		if (!bLobbyJoinIssued)
		{
			bLobbyJoinIssued = true;
			UE_LOG(LogCSNet, Log, TEXT("Browser: joining the default lobby."));
			// The returned task is a coroutine driven by RealtimeClient::Service();
			// letting it go out of scope detaches it and it still completes.
			auto LobbyTask = Client->JoinLobby();
		}
		return;
	}

	TArray<FCSRoomInfo> Fresh;
	for (const PhotonMatchmaking::RoomListing& Listing : Client->GetCachedRoomList())
	{
		FCSRoomInfo& Info = Fresh.AddDefaulted_GetRef();
		Info.Name = ToFString(Listing.Name);
		Info.PlayerCount = Listing.PlayerCount;
		Info.MaxPlayers = Listing.MaxPlayers;
		Info.bIsOpen = Listing.IsOpen;
	}
	Fresh.Sort([](const FCSRoomInfo& A, const FCSRoomInfo& B)
	{
		return A.PlayerCount != B.PlayerCount ? A.PlayerCount > B.PlayerCount : A.Name < B.Name;
	});

	bool bChanged = Fresh.Num() != RoomList.Num();
	for (int32 i = 0; !bChanged && i < Fresh.Num(); ++i)
	{
		bChanged = Fresh[i].Name != RoomList[i].Name
			|| Fresh[i].PlayerCount != RoomList[i].PlayerCount
			|| Fresh[i].MaxPlayers != RoomList[i].MaxPlayers
			|| Fresh[i].bIsOpen != RoomList[i].bIsOpen;
	}

	if (bChanged)
	{
		RoomList = MoveTemp(Fresh);
		UE_LOG(LogCSNet, Log, TEXT("Browser: %d room(s) listed."), RoomList.Num());
		OnRoomListChanged.Broadcast();
	}
#endif
}

// ---------------------------------------------------------------------------
// Polling state machine
//
// Driven by the core ticker, not a world timer: the room join loads a new map
// and a world timer would die with the old world mid-flow.
// ---------------------------------------------------------------------------

void UCSSessionSubsystem::StartPolling()
{
	if (bPolling)
	{
		return;
	}

	PollTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateWeakLambda(this, [this](float)
		{
			PollSession();
			return true;
		}),
		GSessionPollInterval);
	bPolling = true;
}

void UCSSessionSubsystem::StopPolling()
{
	if (bPolling)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PollTickerHandle);
		bPolling = false;
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

	if (bReturningToMenu)
	{
		SetState(NewState);
		const bool bGone = NewState == ECSSessionState::None;
		if (bGone || FPlatformTime::Seconds() >= ReturnToMenuDeadline)
		{
			if (!bGone)
			{
				UE_LOG(LogCSNet, Warning, TEXT("Disconnect did not finish in %.0f s; opening the menu anyway."),
					GReturnToMenuTimeout);
			}
			OpenMainMenu();
		}
		return;
	}

	// The deferred room call, issued exactly once when the connection is up.
	if (NewState == ECSSessionState::Connected && bOperationInFlight && PendingOp != EPendingRoomOp::None)
	{
		IssuePendingRoomOp();
	}

	if (bBrowsing)
	{
		PollLobby();
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
			PendingOp = EPendingRoomOp::None;
			Fail(NewState == ECSSessionState::Error
				? TEXT("Photon reported an error.")
				: TEXT("Disconnected from Photon."));
		}
	}
	else if (NewState == ECSSessionState::Connected && bOperationInFlight
		&& PendingOp == EPendingRoomOp::None && PreviousPolledState == ECSSessionState::JoiningRoom)
	{
		// Back to Connected straight out of JoiningRoom: the room call failed
		// (full, closed, or no such room for a join-only).
		Fail(PendingRequest.RoomName.IsEmpty()
			? TEXT("Could not join a room.")
			: FString::Printf(TEXT("Could not join room '%s' (it may be full or no longer exist)."), *PendingRequest.RoomName));
	}
	else if (NewState == ECSSessionState::None && !bOperationInFlight && !bBrowsing)
	{
		StopPolling();
	}

	PreviousPolledState = NewState;
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
		bBrowsing = false;
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
