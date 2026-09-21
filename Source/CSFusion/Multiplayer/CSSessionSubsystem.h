// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Game-facing session layer. Wraps UFusionOnlineSubsystem so that UI, game
// modes and gameplay code never touch Photon types directly.
//
// Design note - why this polls instead of binding async delegates:
// Fusion's room operations return UFusionRoomActionBase subclasses built for
// Blueprint latent nodes. Their OnSuccess/OnFailure signatures are still
// moving during the 3.0 preview. UFusionOnlineSubsystem::Status() on the other
// hand is a documented, stable state machine
// (None -> Connecting -> Connected -> JoiningRoom -> InRoom -> LeavingRoom,
// with Disconnected/Error reachable from any non-terminal state). Polling it
// on a 10 Hz timer costs nothing and keeps this file the only place that has
// to be reconciled with SDK churn.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Core/CSCoreTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CSSessionSubsystem.generated.h"

class UFusionOnlineSubsystem;

/** Parameters the UI supplies when hosting or joining. */
USTRUCT(BlueprintType)
struct CSFUSION_API FCSSessionRequest
{
	GENERATED_BODY()

	/** Empty = join a random room. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "CS|Session")
	FString RoomName;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "CS|Session", meta = (ClampMin = "2", ClampMax = "16"))
	int32 MaxPlayers = 8;

	/** Photon region code ("eu", "us", "ru", ...). Ignored unless bSelectRegion. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "CS|Session")
	FString Region;

	/** False = ping all regions and pick the fastest (recommended first run). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "CS|Session")
	bool bSelectRegion = false;

	/** Invite-only rooms are hidden from the random/browser list. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "CS|Session")
	bool bVisible = true;

	/** Seconds the empty room survives after the last player leaves. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "CS|Session", meta = (ClampMin = "0", ClampMax = "255"))
	int32 EmptyTtlSeconds = 0;

	/** Map loaded on join. Unset = UCSSessionSubsystem::DefaultMatchWorld(). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "CS|Session")
	TSoftObjectPtr<UWorld> InitialWorld;

	/** Bots the match starts with, if this client ends up creating the room. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "CS|Session", meta = (ClampMin = "0", ClampMax = "8"))
	int32 BotCount = 3;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "CS|Session")
	ECSBotDifficulty BotDifficulty = ECSBotDifficulty::Normal;

	/** v1.1: game mode of a room this client creates. Joiners follow the room's. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "CS|Session")
	ECSGameModeType Mode = ECSGameModeType::Deathmatch;

	/** v1.1: map id from UCSModeSettings::Maps. Used when InitialWorld is unset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "CS|Session")
	FName MapId;
};

/** One row of the session browser, copied out of Photon's lobby room list. */
USTRUCT(BlueprintType)
struct CSFUSION_API FCSRoomInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CS|Session")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "CS|Session")
	int32 PlayerCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CS|Session")
	int32 MaxPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CS|Session")
	bool bIsOpen = false;

	bool IsJoinable() const { return bIsOpen && (MaxPlayers == 0 || PlayerCount < MaxPlayers); }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSSessionStateChanged, ECSSessionState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSSessionFailed, const FString&, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSSessionJoined);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSSessionLeft);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSMasterClientChanged, bool, bIsLocalPlayerMaster);
DECLARE_MULTICAST_DELEGATE(FCSRoomListChanged);

UCLASS(Config = Game)
class CSFUSION_API UCSSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// --- UGameInstanceSubsystem -------------------------------------------
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Region used when the player picked none (Config/DefaultGame.ini). Never
	 * "best ping": two machines can measure different best regions, which
	 * split friends joining the same room name into two rooms.
	 */
	UPROPERTY(Config)
	FString DefaultRegion = TEXT("eu");

	// --- Commands ----------------------------------------------------------

	/** Connect to Photon (if needed) and join the named room, creating it if absent. */
	UFUNCTION(BlueprintCallable, Category = "CS|Session")
	bool HostOrJoin(const FCSSessionRequest& Request);

	/** Quick match: join any open room, creating one only if none exists. */
	UFUNCTION(BlueprintCallable, Category = "CS|Session")
	bool QuickMatch(const FCSSessionRequest& Request);

	/** Join a room by exact name. Fails if it does not exist. */
	UFUNCTION(BlueprintCallable, Category = "CS|Session")
	bool JoinByName(const FCSSessionRequest& Request);

	/** Leave the room but stay connected to the cloud. */
	UFUNCTION(BlueprintCallable, Category = "CS|Session")
	void LeaveMatch();

	/**
	 * Leave the match, drop the Photon connection and open the main menu map
	 * once the disconnect has completed (or after a short timeout).
	 *
	 * A full disconnect rather than a bare LeaveRoom: the next Play then goes
	 * through the same connect-then-join path as the first one, and Fusion's
	 * disconnect action - which runs on a world timer - finishes before its
	 * world is torn down by the map change.
	 */
	UFUNCTION(BlueprintCallable, Category = "CS|Session")
	void LeaveToMainMenu();

	/** Full teardown of the Photon connection. */
	UFUNCTION(BlueprintCallable, Category = "CS|Session")
	void Disconnect();

	/** Offline match against bots: no Photon, this machine is the authority. */
	UFUNCTION(BlueprintCallable, Category = "CS|Session")
	void StartOfflinePractice(int32 BotCount, ECSBotDifficulty Difficulty,
		ECSGameModeType Mode = ECSGameModeType::Deathmatch, FName MapId = NAME_None);

	/** Mode for a match this machine creates: the last request's, overridden by -mode=dm|tdm|5v5. */
	ECSGameModeType GetMatchMode() const;

	/** Map for a request: InitialWorld if set, else the MapId's world, else the default. */
	static TSoftObjectPtr<UWorld> ResolveWorld(const FCSSessionRequest& Request);

	/**
	 * Bots for a match this machine creates: the last request's values,
	 * overridden by -bots=N and -botdifficulty=easy|normal|hard.
	 */
	void GetMatchBotSettings(int32& OutCount, ECSBotDifficulty& OutDifficulty) const;

	// --- Session browser ---------------------------------------------------

	/** Connect if needed, join Photon's default lobby and start receiving the room list. */
	UFUNCTION(BlueprintCallable, Category = "CS|Session")
	void StartBrowsing(const FString& Region);

	/** Stop refreshing the list. The connection is kept so a join is instant. */
	UFUNCTION(BlueprintCallable, Category = "CS|Session")
	void StopBrowsing();

	UFUNCTION(BlueprintPure, Category = "CS|Session")
	bool IsBrowsing() const { return bBrowsing; }

	/** True once the lobby has been joined and the list is live. */
	UFUNCTION(BlueprintPure, Category = "CS|Session")
	bool IsInLobby() const;

	UFUNCTION(BlueprintPure, Category = "CS|Session")
	const TArray<FCSRoomInfo>& GetRoomList() const { return RoomList; }

	/** Fires whenever the browser's room list changes. */
	FCSRoomListChanged OnRoomListChanged;

	/** Long package name of the main menu map. */
	static const TCHAR* MainMenuMapPath() { return TEXT("/Game/Maps/Lvl_MainMenu"); }

	/** The gameplay map every room is bound to. */
	static TSoftObjectPtr<UWorld> DefaultMatchWorld();

	// --- Queries -----------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "CS|Session")
	ECSSessionState GetSessionState() const { return CachedState; }

	UFUNCTION(BlueprintPure, Category = "CS|Session")
	bool IsInRoom() const { return CachedState == ECSSessionState::InRoom; }

	/** A connect or room join is in progress. */
	UFUNCTION(BlueprintPure, Category = "CS|Session")
	bool IsBusy() const { return bOperationInFlight || bReturningToMenu; }

	UFUNCTION(BlueprintPure, Category = "CS|Session")
	bool IsMasterClient() const { return bCachedIsMaster; }

	UFUNCTION(BlueprintPure, Category = "CS|Session")
	FString GetRoomName() const { return CachedRoomName; }

	UFUNCTION(BlueprintPure, Category = "CS|Session")
	int32 GetPlayerCount() const { return CachedPlayerCount; }

	UFUNCTION(BlueprintPure, Category = "CS|Session")
	int32 GetRttMs() const;

	UFUNCTION(BlueprintPure, Category = "CS|Session")
	FString GetLastError() const { return LastError; }

	/** True once the Fusion SDK is compiled in (CS_WITH_FUSION). */
	UFUNCTION(BlueprintPure, Category = "CS|Session")
	static bool IsFusionAvailable();

	// --- Events ------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "CS|Session")
	FCSSessionStateChanged OnSessionStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "CS|Session")
	FCSSessionJoined OnSessionJoined;

	UPROPERTY(BlueprintAssignable, Category = "CS|Session")
	FCSSessionLeft OnSessionLeft;

	UPROPERTY(BlueprintAssignable, Category = "CS|Session")
	FCSSessionFailed OnSessionFailed;

	/** Fires on master migration - the new master must pick up arbitration. */
	UPROPERTY(BlueprintAssignable, Category = "CS|Session")
	FCSMasterClientChanged OnMasterClientChanged;

private:
	/** Which room call to make once the connection is up. */
	enum class EPendingRoomOp : uint8 { None, JoinOrCreate, JoinRandomOrCreate, JoinOnly };

	UFusionOnlineSubsystem* GetFusion() const;

	/** Only place that starts a Fusion matchmaking flow. */
	bool StartRoomOperation(const FCSSessionRequest& Request, EPendingRoomOp Op);

	/** Connects with the pending request's region options. */
	void Connect(const FCSSessionRequest& Request);

	/** Issues the pending room call. Requires a connected, room-less client. */
	void IssuePendingRoomOp();

	/** Lobby join and room-list copy, run from the poller while browsing. */
	void PollLobby();

	/** Tail of LeaveToMainMenu. */
	void OpenMainMenu();

	void StartPolling();
	void StopPolling();
	void PollSession();

	void SetState(ECSSessionState NewState);
	void Fail(const FString& Reason);

	UPROPERTY(Transient)
	ECSSessionState CachedState = ECSSessionState::None;

	UPROPERTY(Transient)
	bool bCachedIsMaster = false;

	UPROPERTY(Transient)
	FString CachedRoomName;

	UPROPERTY(Transient)
	int32 CachedPlayerCount = 0;

	UPROPERTY(Transient)
	FString LastError;

	/** The request currently in flight, retained for reconnect. */
	UPROPERTY(Transient)
	FCSSessionRequest PendingRequest;

	UPROPERTY(Transient)
	TArray<FCSRoomInfo> RoomList;

	FTSTicker::FDelegateHandle PollTickerHandle;
	bool bPolling = false;

	/** Status seen on the previous poll, to spot a failed room call (JoiningRoom -> Connected). */
	ECSSessionState PreviousPolledState = ECSSessionState::None;

	/** Guards against re-entrant room operations. */
	bool bOperationInFlight = false;

	/**
	 * Room call deferred until Status() reaches Connected.
	 *
	 * Every flow is connect-then-join with exactly ONE room call. The SDK's
	 * ConnectAndJoinRoom is not used any more: it refuses to start while the
	 * client is already connected (which the session browser leaves it), and
	 * it always issues JoinOrCreateRoom - with an empty name that CREATES a
	 * new room instead of joining a random one, so Quick Match never met
	 * anybody. Issuing a second call next to it was the "Game does not exist"
	 * (32758) race fixed in v0.1.0.
	 */
	EPendingRoomOp PendingOp = EPendingRoomOp::None;

	bool bBrowsing = false;
	bool bLobbyJoinIssued = false;

	/** Bot settings of the last Play request (see GetMatchBotSettings). */
	int32 MatchBotCount = 0;
	ECSBotDifficulty MatchBotDifficulty = ECSBotDifficulty::Normal;
	ECSGameModeType MatchMode = ECSGameModeType::Deathmatch;

	/**
	 * Quick Match goes to a room NAMED after mode and map ("QM TDM Depot"), so
	 * players only ever meet others who asked for the same thing. When that
	 * room is full the next suffix is tried ("QM TDM Depot #2", ...).
	 */
	FString QuickMatchBaseName;
	int32 QuickMatchAttempt = 0;

	/** Set by LeaveToMainMenu; the poller opens the menu once disconnected. */
	bool bReturningToMenu = false;
	double ReturnToMenuDeadline = 0.0;
};
