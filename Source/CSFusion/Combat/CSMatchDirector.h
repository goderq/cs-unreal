// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// THE authoritative combat store. This is the keystone of the whole
// anti-cheat design, so the reasoning is worth spelling out.
//
// Under Fusion only an object's owner may write its replicated properties;
// every other peer holds a read-only copy and its writes are discarded. The
// player's pawn and PlayerState are both PlayerAttached, i.e. owned by that
// player, so anything stored there is writable by that player - including a
// modified client. Health, armor, ammo and (Stage 3) inventory therefore
// cannot live on them.
//
// ACSMatchDirector is owned by the Master Client (EFusionObjectOwnerFlags::
// MasterClient), which means:
//   * only the elected authority peer can write these records;
//   * ownership re-targets automatically when the master migrates, with no
//     application code;
//   * a client that tries to write its own health is simply ignored by the
//     replication layer, not by a check that could be patched out.
//
// Clients read the records for their HUD. They never write them. Every
// mutation below is guarded by CS_AUTHORITY_ONLY.
//
// Sizing: Fusion pre-allocates replicated arrays and hard-caps them at 64
// elements, so the record array carries an explicit FusionArraySize.

#pragma once

#include "CoreMinimal.h"
#include "Core/CSCoreTypes.h"
#include "Core/CSFusionCompat.h"
#include "Combat/CSCheatGuard.h"
#include "GameFramework/Actor.h"

// Required because Records uses the FusionArraySize meta tag.
#include "CSMatchDirector.fusion.h"
#include "CSMatchDirector.generated.h"

class UCSWeaponDefinition;
class UFusionActorComponent;

/**
 * One player's authoritative combat state.
 *
 * Two SDK details that the documentation gets wrong, both verified against
 * build 1498:
 *
 * 1. No FUSION_BODY() here. The Fusion UBT plugin only emits the _FUSION_BODY
 *    macro for UCLASSes (FusionHeaderCodeGeneratorHFile guards on
 *    `field is UhtClass`), so putting it in a USTRUCT leaves the macro
 *    undefined and fails to compile, despite the example in the docs.
 *
 * 2. No Replicated on these members. Stock UHT rejects it outright with
 *    "Struct members cannot be replicated", and it is not needed: when Fusion
 *    builds the descriptor for a nested struct it passes
 *    EFusionBuildStructOptions::AddDefaultProperties (FusionTypeLookup.cpp),
 *    which puts every UPROPERTY of the struct on the wire regardless of the
 *    CPF_Net flag. Only the owning array on the class carries ReplicatedUsing.
 */
USTRUCT(BlueprintType)
struct CSFUSION_API FCSPlayerCombatRecord
{
	GENERATED_BODY()

	/** Photon player id, as resolved by Fusion ownership. 0 means unused. */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Combat")
	int32 PlayerId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CS|Combat")
	float Health = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "CS|Combat")
	float Armor = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "CS|Combat")
	bool bAlive = false;

	UPROPERTY(BlueprintReadOnly, Category = "CS|Combat")
	int32 Kills = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CS|Combat")
	int32 Deaths = 0;

	/** Rounds left in the starter pistol's magazine. */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Combat")
	int32 StarterRoundsInMag = 0;

	/** Fusion network time of the last accepted shot. Fire-rate check reads this. */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Combat")
	double LastFireNetworkTime = 0.0;

	/** Non-zero while reloading; the authority refuses shots until then. */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Combat")
	double ReloadCompleteNetworkTime = 0.0;

	/** When a dead player becomes eligible to respawn. */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Combat")
	double RespawnAtNetworkTime = 0.0;

	/**
	 * Index into the sorted PlayerStart list, chosen by the authority on
	 * respawn. The owning client moves itself there - the authority cannot,
	 * because it does not own that pawn.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Combat")
	int32 RespawnPointIndex = 0;

	/** Bumped by the authority on every respawn so owning clients notice. */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Combat")
	int32 RespawnCounter = 0;

	/** Which weapon the running reload belongs to: INDEX_NONE = starter pistol, else an inventory slot. */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Combat")
	int32 ReloadSlot = INDEX_NONE;

	bool IsValidRecord() const { return PlayerId != 0; }
};

/**
 * The weapon a player is holding right now and its ammunition, resolved from
 * authoritative state only (director record + Master-Client-owned inventory).
 * Valid on every peer, which is how the HUD and the local fire gate agree with
 * the authority about what is in hand.
 */
struct FCSLoadoutView
{
	const UCSWeaponDefinition* Weapon = nullptr;

	/** INDEX_NONE = starter pistol, otherwise the inventory slot. */
	int32 Slot = INDEX_NONE;

	int32 RoundsInMag = 0;

	/** Rounds available to reload from. -1 means unlimited (starter pistol). */
	int32 Reserve = 0;

	bool bReloading = false;

	bool IsStarter() const { return Slot == INDEX_NONE; }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FCSPlayerKilled, int32, VictimId, int32, KillerId, ECSHitZone, Zone);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSRecordsChanged, int32, PlayerId);

/**
 * Cosmetic combat notification, delivered to every peer: feeds the kill feed,
 * the shooter's hit marker and the victim's damage-direction indicator.
 * Carries no authority - health and kills are already in Records.
 */
struct FCSCombatEvent
{
	int32 VictimId = 0;
	int32 InstigatorId = 0;
	float Damage = 0.f;
	bool bKilled = false;
	ECSHitZone Zone = ECSHitZone::None;
	FString WeaponName;
	FVector FromLocation = FVector::ZeroVector;
};
DECLARE_MULTICAST_DELEGATE_OneParam(FCSCombatEventSignature, const FCSCombatEvent&);

UCLASS()
class CSFUSION_API ACSMatchDirector : public AActor
{
	GENERATED_BODY()
	FUSION_BODY();

public:
	ACSMatchDirector();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Finds the director in the world. There is exactly one. */
	UFUNCTION(BlueprintPure, Category = "CS|Combat", meta = (WorldContext = "WorldContextObject"))
	static ACSMatchDirector* Get(const UObject* WorldContextObject);

	// --- Reads (every peer) ------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "CS|Combat")
	bool GetRecord(int32 PlayerId, FCSPlayerCombatRecord& OutRecord) const;

	UFUNCTION(BlueprintPure, Category = "CS|Combat")
	bool IsPlayerAlive(int32 PlayerId) const;

	UFUNCTION(BlueprintPure, Category = "CS|Combat")
	float GetHealth(int32 PlayerId) const;

	UFUNCTION(BlueprintPure, Category = "CS|Combat")
	float GetArmor(int32 PlayerId) const;

	UFUNCTION(BlueprintPure, Category = "CS|Combat")
	int32 GetStarterRoundsInMag(int32 PlayerId) const;

	UFUNCTION(BlueprintPure, Category = "CS|Combat")
	const TArray<FCSPlayerCombatRecord>& GetAllRecords() const { return Records; }

	// --- Authority-only writes ---------------------------------------------

	/** New round: every player's kills and deaths back to zero. */
	void ResetScores();

	/** Creates or resets a record. Safe to call repeatedly. */
	void EnsurePlayer(int32 PlayerId);

	/**
	 * A player left the room - normally or by losing connection. Drops their
	 * whole inventory as loot at their last known position, then forgets them.
	 *
	 * Idempotent by design, because it IS called more than once: Fusion's
	 * leave notification and the missing-pawn sweep can both fire for the same
	 * departure. The first call takes the inventory and removes the record;
	 * any later call finds the player already handled and creates nothing.
	 */
	void RemovePlayer(int32 PlayerId, ECSDeathReason Reason);

	/**
	 * Turns every item in a player's inventory into world pickups scattered
	 * around Where, and empties the inventory. The starter pistol is never
	 * involved - it is not in the inventory. Returns the number of pickups.
	 */
	int32 DropInventoryAsLoot(int32 PlayerId, const FVector& Where, ECSDeathReason Reason);

	/** The pawn of a player, by Fusion ownership. Null if it does not exist. */
	static class ACSCharacter* FindPawnForPlayer(const UObject* WorldContextObject, int32 PlayerId);

	/** Where the authority last saw this player. Survives the pawn's destruction. */
	bool GetLastKnownLocation(int32 PlayerId, FVector& OutLocation) const;

	/** Authority: record a position for a player explicitly (tests, spawn). */
	void NoteLocation(int32 PlayerId, const FVector& Location) { LastKnownLocation.Add(PlayerId, Location); }

	/**
	 * Full authority-side validation of a fire request.
	 * Returns Accepted only when the shot may proceed.
	 */
	ECSFireRejection ValidateFire(int32 PlayerId, const FCSLoadoutView& Loadout,
		const FVector& ClaimedOrigin, const FVector& ClaimedDirection,
		const FVector& AuthoritativePawnLocation) const;

	/** Consumes one round from the weapon in hand and stamps the fire time. Call only after Accepted. */
	void CommitFire(int32 PlayerId);

	/** Begins a reload of the weapon in hand, if one is warranted and ammo exists. */
	bool BeginReload(int32 PlayerId);

	/** Aborts a running reload, e.g. when the player switches weapons. */
	void CancelReload(int32 PlayerId);

	/** Health restored, capped at max. Returns what was actually applied. */
	float Heal(int32 PlayerId, float Amount);

	/** Armor added, capped at max. Returns what was actually applied. */
	float AddArmor(int32 PlayerId, float Amount);

	/** What the player holds and how much it can shoot. Any peer. */
	FCSLoadoutView GetLoadout(int32 PlayerId) const;

	/**
	 * Applies damage with armor absorption, and kills the victim if health
	 * reaches zero. Returns the health actually removed.
	 */
	float ApplyDamage(int32 VictimId, int32 InstigatorId, float Damage, ECSHitZone Zone);

	/** Marks the player alive again with full health and a fresh magazine. */
	void RespawnPlayer(int32 PlayerId, int32 SpawnPointIndex);

	// --- Events (fire on every peer that sees the state change) ------------

	UPROPERTY(BlueprintAssignable, Category = "CS|Combat")
	FCSPlayerKilled OnPlayerKilled;

	UPROPERTY(BlueprintAssignable, Category = "CS|Combat")
	FCSRecordsChanged OnRecordsChanged;

	/** Every peer, including the authority. */
	FCSCombatEventSignature OnCombatEvent;

	/**
	 * One per victim and shooter per frame: pellets of a shotgun blast are
	 * summed by the authority before sending (see FlushCombatEvents).
	 */
	SEND_FUSIONRPC(TargetAllClients)
	void RpcCombatEvent(int32 VictimId, int32 InstigatorId, float Damage, bool bKilled, int32 Zone, FString& WeaponName, FVector FromLocation);
	void RpcCombatEvent_Receive(int32 VictimId, int32 InstigatorId, float Damage, bool bKilled, int32 Zone, FString& WeaponName, FVector FromLocation);

protected:
	/** Authority-side respawn timer and reload completion. */
	void TickAuthority();

	/** Refills the magazine the reload was started for. */
	void CompleteReload(FCSPlayerCombatRecord& Record);

	int32 FindRecordIndex(int32 PlayerId) const;
	FCSPlayerCombatRecord* FindRecordMutable(int32 PlayerId);

	UFUNCTION()
	void OnRep_Records();

	/**
	 * Fusion pre-allocates array capacity, so this must be sized explicitly.
	 * 16 matches UCSCombatSettings::MaxTrackedPlayers; the SDK hard cap is 64.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Records, meta = (FusionArraySize = 16))
	TArray<FCSPlayerCombatRecord> Records;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CS|Components")
	TObjectPtr<UFusionActorComponent> FusionActor;

public:
	// --- Anti-cheat (Stage 8) -------------------------------------------------

	/** Authority: rate limit + suspension check for one player request. */
	bool GuardRequest(int32 PlayerId, ECSRequestKind Kind);

	/** Authority-local cheat state, for logs and tests. */
	const FCSCheatGuard& GetCheatGuard() const { return CheatGuard; }

private:
	/** Snapshot of alive flags, so every peer can raise death events locally. */
	TMap<int32, bool> LastKnownAlive;

	/** Authority only; not replicated. Rebuilt from scratch by a new master. */
	FCSCheatGuard CheatGuard;

	// --- Authority-local bookkeeping (not replicated) ------------------------

	/**
	 * Last position of every tracked player. Needed because under Fusion a
	 * leaving player's pawn is PlayerAttached and may already be destroyed by
	 * the time the leave notification arrives.
	 */
	TMap<int32, FVector> LastKnownLocation;

	/** When a tracked player's pawn was first seen missing, for the sweep. */
	TMap<int32, double> PawnMissingSince;

	/** Players whose departure has been processed. Guards against double drops. */
	TSet<int32> HandledDepartures;

	/** Last heartbeat value seen per player, and when it last changed. */
	TMap<int32, TPair<int32, double>> HeartbeatSeen;

	/**
	 * How long a heartbeat may stay frozen before the player is treated as
	 * disconnected. Long enough to ride out a hitch or a slow frame on a
	 * background window, short enough that loot appears promptly.
	 */
	static constexpr double HeartbeatTimeoutSeconds = 10.0;

	/** Authority: hits collected this frame, sent at the end of it. */
	TArray<FCSCombatEvent> PendingCombatEvents;
	void FlushCombatEvents();
	void QueueCombatEvent(int32 VictimId, int32 InstigatorId, float Damage, bool bKilled, ECSHitZone Zone);
};
