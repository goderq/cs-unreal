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

	/** Map loaded on join. Leave unset to stay on the current world. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "CS|Session")
	TSoftObjectPtr<UWorld> InitialWorld;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSSessionStateChanged, ECSSessionState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSSessionFailed, const FString&, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSSessionJoined);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSSessionLeft);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSMasterClientChanged, bool, bIsLocalPlayerMaster);

UCLASS()
class CSFUSION_API UCSSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// --- UGameInstanceSubsystem -------------------------------------------
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- Commands ----------------------------------------------------------

	/** Connect to Photon (if needed) and host/join the requested room. */
	UFUNCTION(BlueprintCallable, Category = "CS|Session")
	bool HostOrJoin(const FCSSessionRequest& Request);

	/** Quick match: connect and drop into any open room, creating one if none. */
	UFUNCTION(BlueprintCallable, Category = "CS|Session")
	bool QuickMatch(const FCSSessionRequest& Request);

	/** Join a room by exact name. Fails if it does not exist. */
	UFUNCTION(BlueprintCallable, Category = "CS|Session")
	bool JoinByName(const FCSSessionRequest& Request);

	/** Leave the room but stay connected to the cloud. */
	UFUNCTION(BlueprintCallable, Category = "CS|Session")
	void LeaveMatch();

	/** Full teardown of the Photon connection. */
	UFUNCTION(BlueprintCallable, Category = "CS|Session")
	void Disconnect();

	// --- Queries -----------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "CS|Session")
	ECSSessionState GetSessionState() const { return CachedState; }

	UFUNCTION(BlueprintPure, Category = "CS|Session")
	bool IsInRoom() const { return CachedState == ECSSessionState::InRoom; }

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
	UFusionOnlineSubsystem* GetFusion() const;

	/** Only place that talks to the Fusion matchmaking API. */
	bool StartRoomOperation(const FCSSessionRequest& Request, bool bAllowCreate, bool bRandom);

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

	FTimerHandle PollTimerHandle;

	/** Guards against re-entrant room operations. */
	bool bOperationInFlight = false;

	/** Join-by-name is a two-step flow; this stops the second step re-firing. */
	bool bJoinByNameIssued = false;
};
