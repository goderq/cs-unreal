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
#include "Items/CSShopSettings.h"
#include "Weapons/CSShotModel.h"
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

	/** Which loadout slot the running reload belongs to (INDEX_NONE = none). */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Combat")
	int32 ReloadSlot = INDEX_NONE;

	// --- v1.1 game modes ---

	/** ECSTeam as a byte: 0 none (free for all), 1 Alpha, 2 Bravo. */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Mode")
	uint8 Team = 0;

	/** In-match currency, spent in the shop. */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Mode")
	int32 Money = 0;

	/** v1.2 stats: kills that were headshots, and total damage dealt. */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Mode")
	int32 Headshots = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CS|Mode")
	int32 DamageDealt = 0;

	/**
	 * Spawn protection until this network time (0 = none). No damage is taken
	 * while it lasts; in Deathmatch modes the shop is open exactly as long.
	 * Moving, jumping, crouching or firing ends it early.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Mode")
	double ProtectedUntil = 0.0;

	ECSTeam GetTeam() const { return static_cast<ECSTeam>(Team); }

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

	/** Loadout slot in hand (CSLoadout::Primary..Flash), INDEX_NONE if nothing. */
	int32 Slot = INDEX_NONE;

	/** A grenade stack is in hand (Weapon is its presentation stand-in; RoundsInMag = how many). */
	bool bGrenade = false;

	/** The knife is in hand: no ammunition, melee instead of shots. */
	bool bKnife = false;

	int32 RoundsInMag = 0;

	/** Spare rounds for the weapon in hand. */
	int32 Reserve = 0;

	bool bReloading = false;

	bool IsFirearm() const { return Weapon && !bGrenade && !bKnife; }
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

/** Fire-rate budget of one player (B15), authority-local. See UCSCombatSettings::FireJitterSeconds. */
struct FCSFireBudget
{
	/** Shots available at Time. */
	double Shots = 0.0;
	double Time = 0.0;

	/** Shots available at Now for a weapon firing every Interval seconds. */
	static double Available(const FCSFireBudget* Budget, double Interval, double JitterSeconds, double Now)
	{
		const double SafeInterval = FMath::Max(Interval, 0.01);
		const double Cap = FMath::Min(2.0, 1.0 + JitterSeconds / SafeInterval);
		return Budget ? FMath::Min(Cap, Budget->Shots + (Now - Budget->Time) / SafeInterval) : Cap;
	}
};

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
	const TArray<FCSPlayerCombatRecord>& GetAllRecords() const { return Records; }

	// --- v1.1 modes: reads (every peer) --------------------------------------

	ECSTeam GetTeam(int32 PlayerId) const;
	int32 GetMoney(int32 PlayerId) const;
	/** Spawn protection active right now. */
	bool IsProtected(int32 PlayerId) const;
	float GetProtectionRemaining(int32 PlayerId) const;
	/** Shop open for this player right now (mode rule, see CSShopSettings.h). */
	bool CanBuy(int32 PlayerId) const;
	/** Seconds the shop stays open for this player (0 = closed). */
	float GetBuyTimeRemaining(int32 PlayerId) const;
	/** Two players on the same team (never true in free for all). */
	bool AreTeammates(int32 A, int32 B) const;
	int32 CountAlive(ECSTeam Team) const;
	int32 CountMembers(ECSTeam Team) const;

	// --- v1.1 modes: authority writes ----------------------------------------

	void AddMoney(int32 PlayerId, int32 Delta);
	void CancelProtection(int32 PlayerId, const TCHAR* Why);

	/**
	 * v2.0 match records: a player's participation ticket for the backend
	 * match (functions/match). The player asked the backend for it with their
	 * own login; the authority only carries it into the report. Since v2.0 the
	 * authority no longer takes a profile id from anybody's say-so.
	 */
	void NoteTicket(int32 PlayerId, const FString& Ticket);
	FString GetTicketFor(int32 PlayerId) const;
	void ClearTickets() { Tickets.Reset(); }
	/** Bots took part in the running match (it is then practice, not ranked). */
	bool HadBotsThisMatch() const { return bMatchHadBots; }
	/** Validates everything (shop open, money) and delivers the item. */
	ECSBuyResult TryBuy(int32 PlayerId, int32 ShopIndex);
	/** v2.0 ammo machine: tops up the reserves of both guns for AmmoMachinePrice. */
	ECSBuyResult TryBuyAmmo(int32 PlayerId, const class ACSAmmoMachine* Machine, const FVector& PawnLocation);
	/**
	 * v2.0: puts an item into its loadout slot. Whatever gun occupied that slot
	 * is dropped at the player's feet first; knives and grenades just fill up.
	 * Firearms come with the given rounds (INDEX_NONE = full magazine / full reserve).
	 */
	bool GiveItem(int32 PlayerId, int32 ItemIndex, bool bEquip, int32 AmmoInMag = INDEX_NONE, int32 Reserve = INDEX_NONE);
	/**
	 * v2.0 knife: validates a swing (knife in hand, alive, swing rate, origin
	 * near the pawn) and stamps its time. The pawn then resolves what the blade
	 * hits and calls ApplyDamage - see ACSCharacter::RpcRequestMelee_Receive.
	 */
	bool AcceptMelee(int32 PlayerId, bool bHeavy, const FVector& ClaimedOrigin, const FVector& AuthoritativeOrigin);
	/** Rounds modes: everyone back to life at their team's spawn, buy window open. */
	void StartNewRound();
	/** Match restart: scores, money, inventories, respawn everyone. */
	void ResetForNewMatch();
	/** Picks a spawn point for a player (team spawns, away from enemies). */
	int32 PickSpawnPoint(int32 PlayerId) const;

	// --- v1.1 grenades (authority) ---
	/** Validates and consumes a grenade, then launches it on every peer. */
	bool TryThrowGrenade(int32 PlayerId, const FVector& Origin, const FVector& Direction, const FVector& PawnLocation, const FVector& PawnVelocity);
	/** The authority's copy of a grenade reached its fuse: damage, then tell everyone. */
	void ExplodeGrenade(class ACSGrenade* Grenade);

	/**
	 * v2.0 (B11): the Master Client removed a player from the match (Reason 1 =
	 * anti-cheat). That player's own peer leaves for the menu with the reason;
	 * everyone else just logs it.
	 */
	SEND_FUSIONRPC(TargetAllClients)
	void RpcPlayerRemoved(int32 PlayerId, int32 Reason);
	void RpcPlayerRemoved_Receive(int32 PlayerId, int32 Reason);

	SEND_FUSIONRPC(TargetAllClients)
	void RpcGrenadeThrown(int32 Serial, int32 ThrowerId, int32 Type, FVector Origin, FVector Velocity);
	void RpcGrenadeThrown_Receive(int32 Serial, int32 ThrowerId, int32 Type, FVector Origin, FVector Velocity);

	SEND_FUSIONRPC(TargetAllClients)
	void RpcGrenadeExploded(int32 Serial, int32 Type, FVector Location);
	void RpcGrenadeExploded_Receive(int32 Serial, int32 Type, FVector Location);

	/** v2.0 flashbang: how blinded a viewer at Eye looking along Forward is (0..1), and for how long. */
	static float ComputeFlashStrength(const UWorld* World, const FVector& Center, const FVector& Eye, const FVector& Forward, const AActor* Ignore, float& OutSeconds);
	/** Authority: bots cannot see or shoot until this network time. */
	bool IsBlinded(int32 PlayerId) const;

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
	 * Empties a player's loadout and turns the guns in it (primary and pistol,
	 * with their rounds) into world pickups around Where. Grenades and the
	 * knife are not dropped. Returns the number of pickups.
	 */
	int32 DropInventoryAsLoot(int32 PlayerId, const FVector& Where, ECSDeathReason Reason);

	/** Spawns one dropped weapon (authority). */
	class ACSWorldPickup* SpawnDroppedItem(const struct FCSInventorySlot& Item, const FVector& Where, const FVector& FlyFrom);

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

	/** Authority: a violation found by a handler (forged RPC, bad input) - a strike against PlayerId. */
	void ReportViolation(int32 PlayerId, ECSCheatReason Reason, float Weight, const FString& Detail);

	// --- v2.0 (B8): shots decided by the authority --------------------------------

	/** Authority: the player's aim-down-sights request. */
	void SetAiming(int32 PlayerId, bool bAiming);

	/** Authority: aim, speed and height of the shooter right now, from the authority's own state. */
	FCSShooterState GetShooterState(int32 PlayerId, const class ACSCharacter* Pawn) const;

	/** Authority: the player's series before the next shot (recoil pattern, bloom). */
	float GetShotSeries(int32 PlayerId, const UCSWeaponDefinition& Weapon) const;

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

	// v1.1 spawn protection, authority-local: where each protected player must
	// appear, and whether its pawn has arrived there yet (the owning client
	// moves itself, so until it arrives it is still at its death spot).
	TMap<int32, FVector> ProtectionSpawn;
	TSet<int32> ProtectionArrived;

	void BeginProtection(FCSPlayerCombatRecord& Record);
	/** Knife always, and the spawn pistol if the pistol slot is empty; the pistol goes to the hands. */
	void GiveSpawnLoadout(int32 PlayerId);
	/** Full health, spawn loadout, alive at a start; bumps RespawnCounter so the owner teleports. */
	void ResetLife(FCSPlayerCombatRecord& Record, int32 SpawnPointIndex);
	void WatchProtection(FCSPlayerCombatRecord& Record, const class ACSCharacter* Pawn);

	/** Last heartbeat value seen per player, and when it last changed. */
	TMap<int32, TPair<int32, double>> HeartbeatSeen;

	/**
	 * How long a heartbeat may stay frozen before the player is marked
	 * inactive. v2.0 (B11): only marked - a game frozen by shader compilation
	 * or a minimised window comes back. Whether a player LEFT is decided by
	 * the room list the Photon server keeps.
	 */
	static constexpr double HeartbeatTimeoutSeconds = 10.0;

	/** Players whose heartbeat is frozen right now (B11). */
	TSet<int32> Stalled;

	/** Players the anti-cheat removed from this match; they are not registered again (B11). */
	TSet<int32> RemovedByAntiCheat;

	/** Last observed position of each player that was not inside static geometry, and when it was reached (B10). */
	TMap<int32, TPair<FVector, double>> LastClearPosition;

	/** Authority: what the cheat guard is fed for one pawn this tick (floor, walls, spawn point). */
	FCSMoveSample MakeMoveSample(const FCSPlayerCombatRecord& Record, const class ACSCharacter* Pawn, const FVector* Previous, double Now);

	/** Authority: log the guard's suspensions and removals, report them, remove who must go. */
	void ProcessCheatIncidents();

	/** Authority: tell the backend about a suspension or removal (online matches with a backend id). */
	void ReportIncidentToBackend(const FCSCheatIncident& Incident);

	/** Authority: hits collected this frame, sent at the end of it. */
	TArray<FCSCombatEvent> PendingCombatEvents;
	void FlushCombatEvents();
	void QueueCombatEvent(int32 VictimId, int32 InstigatorId, float Damage, bool bKilled, ECSHitZone Zone);

	/** Kill feed name while a grenade blast is being applied (else the thrower's weapon in hand). */
	FString CombatWeaponOverride;
	int32 NextGrenadeSerial = 1;
	TMap<int32, double> LastThrowTime;
	/**
	 * Serials of the grenades this authority launched itself (A1). Only these
	 * explode with effect here; a grenade that reached this peer any other way
	 * is only a picture. A new Master Client starts empty, so a grenade in the
	 * air during a host migration goes off harmlessly.
	 */
	TSet<int32> LaunchedGrenades;

	/** Fire-rate budgets (B15). Not replicated; a new Master Client starts everyone full. */
	TMap<int32, FCSFireBudget> FireBudgets;

	// --- v2.0 (B8): what the authority knows about each shooter -----------------
	/** Series of shots per player (recoil pattern and bloom). */
	TMap<int32, FCSSprayState> SprayStates;
	/** Aim down sights per player: on / off, and since when (network time). */
	TMap<int32, TPair<bool, double>> AimStates;
	/** Speed and height as the authority observes them (humans; bots use their own movement). */
	struct FObservedMotion
	{
		FVector LastLocation = FVector::ZeroVector;
		double LastTime = -1.0;
		float Speed = 0.f;
		bool bAirborne = false;
	};
	TMap<int32, FObservedMotion> ObservedMotion;
	void ObserveMotion(int32 PlayerId, const FVector& Location, float HeightAboveFloor, double Now);
	TMap<int32, double> LastMeleeTime;
	TMap<int32, double> LastAmmoBuyTime;
	/** Authority: bots blinded by a flashbang, until this network time. */
	TMap<int32, double> BlindedUntil;
	/** Authority only: Photon player id -> backend match ticket (none for bots and guests). */
	TMap<int32, FString> Tickets;
	/** Authority only: a bot record existed while the current match ran. */
	bool bMatchHadBots = false;
};
