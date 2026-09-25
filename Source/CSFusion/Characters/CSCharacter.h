// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// First-person player character.
//
// Mesh split, as required by the brief:
//   FirstPersonMesh - arms, attached to the camera, visible ONLY to the owner.
//   GetMesh()       - full third-person body, visible to everyone EXCEPT the
//                     owner, but still casting a shadow for the owner so the
//                     player sees their own silhouette.
//
// Networking: the pawn carries a UFusionActorComponent with PlayerAttached
// ownership, so the controlling client owns it (responsive input, local
// prediction through Unreal's built-in character networking, which Fusion
// relies on for ACharacter-derived actors) and Fusion destroys it when that
// player leaves the room.
//
// Because the pawn is client-owned, NOTHING a cheater must not control lives
// on it: no health, no ammo, no inventory. Those live on the
// Master-Client-owned ACSMatchDirector. This class only carries presentation
// state that is harmless to forge - stance and view pitch - and owns the wire
// contract for combat requests.
//
// Trust boundary, in one place:
//   RequestFire()/RequestReload()  client asks
//   Rpc*_Receive()                 authority decides, using its own world
//   ACSMatchDirector records       result, replicated back to everyone

#pragma once

#include "CoreMinimal.h"
#include "Core/CSCoreTypes.h"
#include "Core/CSFusionCompat.h"
#include "GameFramework/Character.h"

// Must precede the .generated.h include. FUSION_BODY() expands into this.
#include "CSCharacter.fusion.h"
#include "CSCharacter.generated.h"

class ACSMatchDirector;
enum class ECSRequestKind : uint8;
class UCameraComponent;
class UCSCharacterMovementComponent;
class UCSInputConfig;
class UCSWeaponComponent;
class UCSWeaponDefinition;
class UFusionActorComponent;
class UInputComponent;
class UInputMappingContext;
class USkeletalMeshComponent;
class UStaticMeshComponent;
struct FInputActionValue;

UCLASS(Config = Game)
class CSFUSION_API ACSCharacter : public ACharacter
{
	GENERATED_BODY()
	FUSION_BODY();

public:
	explicit ACSCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// v1.1 movement feel: camera follows crouch smoothly, dips on landing; short jump cooldown.
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void Landed(const FHitResult& Hit) override;
	virtual bool CanJumpInternal_Implementation() const override;

	/** Network time the pawn last landed; the movement component slows the first steps after. */
	double GetLastLandedTime() const { return LastLandedTime; }
	float GetLastLandingImpact() const { return LastLandingImpact; }

	/** Arms rig. Owner-only visibility. */
	UFUNCTION(BlueprintPure, Category = "CS|Character")
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }

	UFUNCTION(BlueprintPure, Category = "CS|Character")
	UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }

	UFUNCTION(BlueprintPure, Category = "CS|Character")
	UCSWeaponComponent* GetWeaponComponent() const { return WeaponComponent; }

	UFUNCTION(BlueprintPure, Category = "CS|Character")
	UCSCharacterMovementComponent* GetCSMovement() const;

	UFUNCTION(BlueprintPure, Category = "CS|Character")
	ECSStanceState GetStance() const { return Stance; }

	/** Replicated view pitch in degrees, for aiming the third-person body. */
	UFUNCTION(BlueprintPure, Category = "CS|Character")
	float GetRemoteViewPitchDegrees() const;

	/** True when this pawn is the one the local player is looking through. */
	UFUNCTION(BlueprintPure, Category = "CS|Character")
	bool IsLocalFirstPersonView() const;

	/** This pawn's Photon player id, resolved from Fusion ownership. */
	UFUNCTION(BlueprintPure, Category = "CS|Character")
	int32 GetOwningPlayerId() const;

	/** Alive per the authority. False also when there is no record yet. */
	UFUNCTION(BlueprintPure, Category = "CS|Character")
	bool IsAliveAuthoritative() const;

	/** Camera-centre ray used for aiming and for the authority's trace. */
	UFUNCTION(BlueprintPure, Category = "CS|Character")
	void GetAimRay(FVector& OutOrigin, FVector& OutDirection) const;

	/** Immediate local feedback for a shot: recoil kick and (Stage 6) FX. */
	void PlayLocalFireEffects(const UCSWeaponDefinition* Weapon);

	// --- Combat wire contract ----------------------------------------------

	/** Client entry point. Sends the request, or runs it locally when offline. */
	void RequestFire(const FVector& Origin, const FVector& Direction, bool bAiming);

	/** Client entry point for reloading. */
	void RequestReload();

	/**
	 * Fire request. Generated body - never define it.
	 *
	 * Delivered on this same networked object on the Master Client, which is
	 * what lets the authority identify the sender through Fusion ownership
	 * rather than trusting a player id in the payload.
	 *
	 * bAiming is the one client claim the authority accepts unchecked: it can
	 * only tighten the spread cone the authority applies, and aiming is a
	 * legitimate player choice.
	 */
	SEND_FUSIONRPC(TargetMasterClient)
	void RpcRequestFire(FVector Origin, FVector Direction, bool bAiming);
	void RpcRequestFire_Receive(FVector Origin, FVector Direction, bool bAiming);

	// --- Inventory wire contract -------------------------------------------

	/** Picks up whatever the player is looking at, if anything. */
	void RequestPickupFocused();

	/** Slot key pressed (0..4 for keys 1..5, see CSLoadout). Empty slots are ignored. */
	void RequestSlot(int32 Slot);

	/** Drops the gun in hand (primary or pistol; the knife and grenades stay). */
	void RequestDropEquipped();

	/** v2.0 knife: owning client swings (light slash or heavy stab). */
	void RequestMelee(bool bHeavy);

	SEND_FUSIONRPC(TargetMasterClient)
	void RpcRequestMelee(FVector Origin, FVector Direction, bool bHeavy);
	void RpcRequestMelee_Receive(FVector Origin, FVector Direction, bool bHeavy);

	/** Cosmetic: the swing, and what it hit, on every peer. */
	SEND_FUSIONRPC(TargetAllClients)
	void RpcMeleeSwing(bool bHeavy, FVector Impact, int32 HitKind);
	void RpcMeleeSwing_Receive(bool bHeavy, FVector Impact, int32 HitKind);

	/** v2.0 ammo machines: the one under the crosshair and within reach (local). */
	class ACSAmmoMachine* GetFocusedMachine() const { return FocusedMachine.Get(); }

	SEND_FUSIONRPC(TargetMasterClient)
	void RpcRequestAmmo(int32 MachineIndex);
	void RpcRequestAmmo_Receive(int32 MachineIndex);

	/** v2.0 flashbang: local blindness, Strength 0..1 fading over Seconds. */
	void ApplyFlash(float Strength, float Seconds);
	/** How white the screen is right now (0..1), for the HUD. */
	float GetFlashAmount() const;

	const UCSInputConfig* GetInputConfig() const { return InputConfig; }

	/** Owning client: re-read FOV and sensitivity, and rebuild key mappings. */
	void ApplyLocalPreferences();

	/**
	 * Pickup request. The pickup travels as an actor reference; the
	 * authority re-validates everything about it - that it still exists, is
	 * unclaimed, is within reach of where IT believes this pawn is, and that
	 * the inventory has room.
	 */
	SEND_FUSIONRPC(TargetMasterClient)
	void RpcRequestPickup(AActor* Pickup);
	void RpcRequestPickup_Receive(AActor* Pickup);

	SEND_FUSIONRPC(TargetMasterClient)
	void RpcRequestSlot(int32 Slot);
	void RpcRequestSlot_Receive(int32 Slot);

	SEND_FUSIONRPC(TargetMasterClient)
	void RpcRequestDrop(int32 Slot);
	void RpcRequestDrop_Receive(int32 Slot);

	/** v1.1 grenade: owning client throws the grenade in hand along the view. */
	void RequestThrowGrenade();

	SEND_FUSIONRPC(TargetMasterClient)
	void RpcRequestThrow(FVector Origin, FVector Direction);
	void RpcRequestThrow_Receive(FVector Origin, FVector Direction);

	/** Throwing motion on arms and body (cosmetic, every peer). */
	void PlayThrowPresentation(bool bFromRelease = false);

	/** Camera shake from a nearby blast, 0..1 (local view). */
	void AddExplosionShake(float Strength) { ExplosionShake = FMath::Max(ExplosionShake, Strength); }

	/**
	 * v1.2 accounts: the owning client tells everyone its nickname, for the
	 * HUD. Cosmetic only. v2.0: the profile id is no longer sent - who played
	 * is proven to the backend by participation tickets (RpcMatchTicket).
	 */
	void BroadcastIdentity();

	SEND_FUSIONRPC(TargetAllClients)
	void RpcIdentify(FString& Nickname);
	void RpcIdentify_Receive(FString& Nickname);

	/**
	 * v2.0 match records: the owning client hands its own participation ticket
	 * for the backend match to the Master Client, and to it only. Re-sent now
	 * and then so a new Master Client after a migration has it too.
	 */
	void UpdateMatchTicket();

	SEND_FUSIONRPC(TargetMasterClient)
	void RpcMatchTicket(FString& MatchId, FString& PlayerTicket);
	void RpcMatchTicket_Receive(FString& MatchId, FString& PlayerTicket);

	/** Name shown for this player, or an empty string when they have not said. */
	const FString& GetDisplayNickname() const { return DisplayNickname; }

	/** v1.1 shop: owning client asks to buy shop entry ShopIndex. The authority re-checks everything. */
	void RequestBuy(int32 ShopIndex);

	SEND_FUSIONRPC(TargetMasterClient)
	void RpcRequestBuy(int32 ShopIndex);
	void RpcRequestBuy_Receive(int32 ShopIndex);

	/** The pickup under the crosshair within reach, for the HUD prompt. Local. */
	UFUNCTION(BlueprintPure, Category = "CS|Character")
	class ACSWorldPickup* GetFocusedPickup() const { return FocusedPickup.Get(); }

	SEND_FUSIONRPC(TargetMasterClient)
	void RpcRequestReload();
	void RpcRequestReload_Receive();

	/** v2.0 (B8): aim down sights on / off. The authority keeps and times it; the spread follows its record. */
	void RequestSetAiming(bool bAiming);

	SEND_FUSIONRPC(TargetMasterClient)
	void RpcSetAiming(bool bAiming);
	void RpcSetAiming_Receive(bool bAiming);

	/** Cosmetic confirmation of a shot the authority accepted. */
	SEND_FUSIONRPC(TargetAllClients)
	void RpcConfirmShot(FVector Origin, FVector Impact, bool bHitPlayer);
	void RpcConfirmShot_Receive(FVector Origin, FVector Impact, bool bHitPlayer);

protected:
	// --- Input handlers ----------------------------------------------------
	void Input_Move(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_JumpStart(const FInputActionValue& Value);
	void Input_JumpStop(const FInputActionValue& Value);
	void Input_SprintStart(const FInputActionValue& Value);
	void Input_SprintStop(const FInputActionValue& Value);
	void Input_CrouchToggle(const FInputActionValue& Value);
	void Input_CrouchRelease(const FInputActionValue& Value);
	void Input_FireStart(const FInputActionValue& Value);
	void Input_FireStop(const FInputActionValue& Value);
	void Input_AimStart(const FInputActionValue& Value);
	void Input_AimStop(const FInputActionValue& Value);
	void Input_Reload(const FInputActionValue& Value);
	void Input_Interact(const FInputActionValue& Value);
	void Input_EquipSlot(const FInputActionValue& Value);
	void Input_Drop(const FInputActionValue& Value);
	void Input_PauseMenu(const FInputActionValue& Value);
	void Input_ScoreboardStart(const FInputActionValue& Value);
	void Input_ScoreboardStop(const FInputActionValue& Value);
	void Input_BuyMenu(const FInputActionValue& Value);

	/** Local: choose the pickup nearest the crosshair within reach and in sight. */
	void UpdateFocusedPickup();

	/** Push the mapping context onto the local player. Safe to call twice. */
	void ApplyInputMappings();

	/** Owner-only vs remote mesh visibility. Re-run whenever possession changes. */
	void RefreshMeshVisibility();

	/** Recompute Stance from the movement component (owning client only). */
	void UpdateStance();

	/**
	 * Mirrors the authority's alive flag into local presentation and collision.
	 *
	 * Driven by the replicated director record, NOT by anything this client
	 * decides, so a client that refuses to die still appears dead - and is
	 * still refused shots - on every other peer.
	 */
	void ApplyAliveState(bool bNewAlive);

	/** Owning client moves itself to the authority's chosen respawn point. */
	void HandleRespawn(int32 SpawnPointIndex);

	/** Polls the director for alive/respawn transitions. */
	void SyncWithDirector();

	UFUNCTION()
	void HandleFusionObjectReady();

	UFUNCTION()
	void HandleFusionOwnerChanged();

	// --- Components --------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CS|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CS|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> FirstPersonMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CS|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCSWeaponComponent> WeaponComponent;

	/** Weapon in the first-person hands. Owner only. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CS|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> FirstPersonWeapon;

	/** Weapon in the third-person hands. Everyone except the owner (shadow for the owner). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CS|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> ThirdPersonWeapon;

	/**
	 * v1.0 static weapon models (UCSWeaponPresentationSettings), in the same
	 * hand sockets. Used instead of the skeletal pair above whenever the
	 * equipped weapon has a model entry - which all shipped weapons do.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CS|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> FirstPersonWeaponModel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CS|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> ThirdPersonWeaponModel;

	/** Bridge to UFusionClient. Unguarded so UHT keeps it GC-tracked. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CS|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UFusionActorComponent> FusionActor;

	// --- Tuning ------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Input")
	TObjectPtr<UCSInputConfig> InputConfig;

	/** Eye height above the capsule centre. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Camera")
	float CameraHeight = 64.f;

	/** Eye height above the (smaller) capsule centre while crouched. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Camera")
	float CrouchedCameraHeight = 46.f;

	/** Aiming: the grip stays at least this far in front of the eye, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Camera")
	float MinAdsGripDistance = 22.f;

	// v1.1 camera height state (local view)
	float CameraZ = 64.f;
	float LandDip = 0.f;
	float LandDipVelocity = 0.f;
	double LastLandedTime = -100.0;
	float LastLandingImpact = 0.f;
	void UpdateCameraHeight(float DeltaSeconds);
	float ExplosionShake = 0.f;
	FTimerHandle ThrowReleaseTimer;
	FString DisplayNickname;
	double NextIdentityBroadcast = 0.0;
	/** v2.0 match records (owning client): the match the ticket is for, and the ticket. */
	FString TicketMatchId;
	FString Ticket;
	bool bTicketRequested = false;
	double NextTicketSend = 0.0;
	/** First-person throw clock (< 0 idle). */
	float ViewThrowTime = -1.f;
	/** v2.0 knife swing in the local view (< 0 = none). */
	float ViewMeleeTime = -1.f;
	bool bViewMeleeHeavy = false;

	/** v1.1: death ragdoll on the body mesh (every peer, cosmetic). */
	void SetRagdoll(bool bEnable);
	bool bRagdoll = false;

	/** v1.1: translucent look while spawn-protected. */
	void UpdateProtectionLook(float DeltaSeconds);
	float GhostAlpha = 0.f;
	bool bGhostMaterials = false;
	UPROPERTY(Transient)
	TArray<TObjectPtr<class UMaterialInstanceDynamic>> GhostMaterials;
	/** Materials each mesh had before the ghost look, per slot. */
	TMap<TWeakObjectPtr<UMeshComponent>, TArray<TObjectPtr<UMaterialInterface>>> GhostOriginals;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> GhostKeepAlive;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Camera", meta = (ClampMin = "60.0", ClampMax = "130.0"))
	float DefaultFieldOfView = 100.f;

	/** First-person Mannequin offset from the camera: puts its eyes at the camera. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Camera")
	FVector FirstPersonMeshOffset = FVector(6.f, 2.f, -161.f);

	/** Hand socket on the Mannequin that holds the weapon. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Weapon")
	FName WeaponSocket = TEXT("HandGrip_R");

	/** Multiplier applied to raw look input before sensitivity settings. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Camera", meta = (ClampMin = "0.01"))
	float BaseLookScale = 1.f;

	/** Crouch is hold-to-crouch when false, toggle when true. */
	UPROPERTY(Config, EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Input")
	bool bToggleCrouch = true;

	// --- Replicated presentation state -------------------------------------

	UPROPERTY(ReplicatedUsing = OnRep_Stance, BlueprintReadOnly, Category = "CS|Character")
	ECSStanceState Stance = ECSStanceState::Standing;

	/**
	 * Liveness beacon, incremented by the owning client once a second.
	 *
	 * An application-level liveness signal that does not depend on how quickly
	 * the Photon cloud notices a client that vanished without saying goodbye
	 * (crash, killed process, pulled cable). This pawn is owned by the player,
	 * so Fusion replicates the counter to the authority; when the client dies
	 * the value freezes, and ACSMatchDirector treats a counter that has not
	 * moved for HeartbeatTimeoutSeconds as a disconnect. It bounds detection
	 * time from our side, whatever the server-side timeout turns out to be.
	 */
	UPROPERTY(Replicated)
	int32 Heartbeat = 0;

	/** Set once by the authority when the pawn is spawned as a bot. */
	UPROPERTY(Replicated)
	bool bIsBot = false;

	UPROPERTY(Replicated)
	int32 BotId = 0;

	/** True only while a Bot* function is running a handler for the bot. */
	bool bBotAuthorityCall = false;

	/** Handlers refuse bots unless the call came through a Bot* function. */
	bool RefuseBotRpc() const { return IsBot() && !bBotAuthorityCall; }

	/** Authority (B9): true = the request's origin or direction is NaN, infinite or absurd; a strike. */
	bool RefuseBadAim(const TCHAR* RpcName, const FVector& Origin, const FVector& Direction) const;

	/** Authority: rate limit and suspension check for a player's request (Stage 8). */
	bool PassesCheatGuard(ECSRequestKind Kind) const;

public:
	int32 GetHeartbeat() const { return Heartbeat; }

protected:

	UFUNCTION()
	void OnRep_Stance();

private:
	/** Set once the Fusion handshake completes; replicated reads are safe after. */
	bool bNetworkReady = false;

	/** Local mirror of the authority's alive flag, to detect transitions. */
	bool bLocalAliveState = true;

	/** Last respawn counter seen, so one respawn teleports exactly once. */
	int32 LastRespawnCounter = 0;

	/** Set once this pawn's player has had a record, to spot their departure. */
	bool bHadRecord = false;
	bool bDepartedHidden = false;

	/** Owner-side accumulator for the heartbeat. */
	float HeartbeatAccumulator = 0.f;

	/** Local only; recomputed every frame for the owning client. */
	TWeakObjectPtr<class ACSWorldPickup> FocusedPickup;
	TWeakObjectPtr<class ACSAmmoMachine> FocusedMachine;

	// v2.0 flashbang (local view): peak strength, when it started and how long it lasts.
	float FlashStrength = 0.f;
	double FlashStart = 0.0;
	float FlashSeconds = 0.f;
	TWeakObjectPtr<class UAudioComponent> FlashRing;

	/** Mapping context built in C++ by UCSInputConfig, cached per pawn. */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> RuntimeMappingContext;

	/**
	 * Where RuntimeMappingContext was added. The context is outered to this
	 * pawn, so EndPlay must remove it: otherwise the subsystem keeps a pointer
	 * to a collected object and ensures (IsValid(MappingContext)) on its next
	 * rebuild - which happened when Fusion reloads the map on joining a room.
	 */
	TWeakObjectPtr<class UEnhancedInputLocalPlayerSubsystem> MappedInputSubsystem;

	/** Subscription to UCSSettingsSubsystem::OnPreferencesChanged. */
	FDelegateHandle PreferencesChangedHandle;

	// --- Presentation (Stage 6): animation, sound, effects -------------------
	//
	// Everything below is cosmetic and runs on every peer from replicated
	// state: the loadout in the director record drives the weapon mesh and
	// stance, the record's reload state drives the reload animation, shot
	// confirmations drive fire effects, combat events drive hit reactions.

public:
	// --- Bots (Stage 7) -----------------------------------------------------
	//
	// A bot pawn is the same character, owned by the Master Client (Fusion
	// MasterClient ownership instead of PlayerAttached) and possessed by an
	// ACSBotController there. Its combat identity is BotId, not the Fusion
	// owner, so the director keeps a separate record for every bot.
	//
	// Bots act through the same authority handlers as players (fire, reload,
	// pickup, slot), called directly on the Master Client via the Bot*
	// functions. The RPC versions of those handlers refuse bot pawns, so a
	// client cannot puppet a bot by sending RPCs to it.

	/** Authority, before FinishSpawning: turn this pawn into bot BotId. */
	void InitAsBot(int32 InBotId);

	/**
	 * Is this pawn a bot? bIsBot and BotId are replicated fields written by
	 * the pawn's owner (A2), so for the authority a pawn is a bot only if the
	 * authority itself owns it: bots belong to the Master Client, a player's
	 * pawn to that player. A client that flags its own pawn as a bot is still
	 * that player to the authority. Other peers use the flag for display.
	 */
	bool IsBot() const;

	/** The replicated flag as written by the pawn's owner - display and tests only. */
	bool ClaimsToBeBot() const { return bIsBot; }

#if !UE_BUILD_SHIPPING
	/** Self-test only (-cstestbotflag): flag my own pawn as a bot, as a modified client would (A2). */
	void DebugForgeBotFlags(int32 FakeBotId) { bIsBot = true; BotId = FakeBotId; }
	/** Self-test only (-cstestfreeze): stop the heartbeat for a while, like a frozen game (B11). */
	void DebugPauseHeartbeat(float Seconds) { DebugHeartbeatPauseUntil = FPlatformTime::Seconds() + Seconds; }
	double DebugHeartbeatPauseUntil = 0.0;
#endif

	/** Local human view: the pawn this machine's player looks through. False for bots. */
	bool IsLocalPlayerView() const { return IsLocallyControlled() && IsPlayerControlled(); }

	void BotFire(const FVector& Origin, const FVector& Direction);
	void BotReload();
	void BotPickup(class ACSWorldPickup* Pickup);
	void BotSelectSlot(int32 Slot);
	/** v2.0: a bot swings its knife (authority, no RPC). */
	void BotMelee(const FVector& Origin, const FVector& Direction, bool bHeavy);
	/** v2.0: a bot tops up its reserves at an ammo machine (authority, no RPC). */
	void BotBuyAmmo(class ACSAmmoMachine* Machine);

	/** Authority: validates a knife swing and applies what it hits (player RPC and bots). */
	void ResolveMeleeOnAuthority(const FVector& Origin, const FVector& Direction, bool bHeavy);

	class UCSAnimInstance* GetBodyAnim() const;
	class UCSAnimInstance* GetArmsAnim() const;

	/** Weapon mesh currently shown in the local view (tests read this). */
	const UCSWeaponDefinition* GetDisplayedWeapon() const { return DisplayedWeapon.Get(); }

	/** Velocity the animation uses: smoothed for remote players, real for the local one. */
	FVector GetAnimationVelocity() const;

	/** 0 = hip, 1 = fully aimed down sights. Local view only. */
	float GetAimAlpha() const { return AimAlpha; }

	/** Aimed through a scope: the HUD draws the scope, the model is hidden. */
	bool IsScopedView() const { return bScopedView; }

	/** The static model entry of the displayed weapon, if it has one. */
	const struct FCSWeaponModel* GetDisplayedModel() const;

	/** First-person weapon model component (tests and HUD read its transform). */
	UStaticMeshComponent* GetFirstPersonWeaponModel() const { return FirstPersonWeaponModel; }

private:
	/**
	 * First-person view, every frame for the local player: aim-down-sights
	 * blend (arms move so the sight lands on the screen centre, FOV zooms),
	 * hip framing, walk bob, look sway and fire kick.
	 */
	void UpdateFirstPersonView(float DeltaSeconds);

	/**
	 * Remote players: the replicated transform arrives in steps. The body mesh
	 * follows a smoothed, velocity-predicted position and yaw instead, so
	 * other players move fluidly. The capsule (what bullets hit) is untouched.
	 */
	void UpdateRemoteSmoothing(float DeltaSeconds);

	/** Left-hand IK target for both anim instances, from the displayed model. */
	void UpdateHandTargets();
	/** Sets Manny/Quinn and the C++ anim instance on both meshes. */
	void SetupCharacterMeshes();

	/** Follows the authoritative loadout: weapon meshes, stance, equip, reload. */
	void UpdateWeaponPresentation();

	void UpdateFootsteps(float DeltaSeconds);

	/** Fire animation, muzzle flash, sound and tracer from this pawn's gun. */
	void PlayShotPresentation(const FVector& TracerEnd, bool bLocalPrediction);

	/** World transform of the muzzle of the weapon mesh the viewer sees. */
	FTransform GetMuzzleTransform(bool bFirstPersonView) const;

	void BindCombatEvents();
	void UnbindCombatEvents();
	void HandleCombatEvent(const struct FCSCombatEvent& Event);

	TWeakObjectPtr<const UCSWeaponDefinition> DisplayedWeapon;
	bool bShownReloading = false;
	/** Total items in this player's inventory last frame; a rise means a pickup. */
	int32 LastInventoryItems = -1;
	bool bWasFalling = false;
	float StepAccumulator = 0.f;
	FVector LastHitFrom = FVector::ZeroVector;
	bool bHasLastHitFrom = false;
	TWeakObjectPtr<ACSMatchDirector> BoundDirector;
	FDelegateHandle CombatEventHandle;

	// --- v1.0 first-person view state ---
	float AimAlpha = 0.f;
	bool bScopedView = false;
	/** Procedural first-person equip / reload motion: seconds since start, < 0 = idle. */
	float ViewEquipTime = -1.f;
	float ViewReloadTime = -1.f;
	float ViewReloadDuration = 1.f;
	float BobPhase = 0.f;
	FVector2D LookSway = FVector2D::ZeroVector;
	FVector2D LookSwayNow = FVector2D::ZeroVector;
	float FireKick = 0.f;

	// --- v1.0 remote smoothing state ---
	FVector SmoothedLocation = FVector::ZeroVector;
	FVector SmoothedVelocity = FVector::ZeroVector;
	FVector LastReplicatedLocation = FVector::ZeroVector;
	float SmoothedYaw = 0.f;
	bool bSmoothingInit = false;
	FVector BodyMeshBaseLocation = FVector::ZeroVector;
	FRotator BodyMeshBaseRotation = FRotator::ZeroRotator;

public:
	/** Look input this frame (sway), fed by Input_Look. */
	void AddLookSway(const FVector2D& Delta) { LookSway += Delta; }
};
