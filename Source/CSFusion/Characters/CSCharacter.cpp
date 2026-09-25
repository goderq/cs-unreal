// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Characters/CSCharacter.h"

// Required from UE 5.8: generated .gen.cpp files are no longer auto-scanned.
#include UE_INLINE_GENERATED_CPP_BY_NAME(CSCharacter.fusion)

#include "Camera/CameraComponent.h"
#include "Characters/CSCharacterMovementComponent.h"
#include "Combat/CSCheatGuard.h"
#include "Combat/CSMatchDirector.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Core/CSAuthority.h"
#include "Core/CSRpcGuard.h"
#include "Core/CSValidate.h"
#include "Core/CSCombatSettings.h"
#include "Core/CSLog.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/CSGameMode.h"
#include "GameModes/CSGameState.h"
#include "Input/CSInputConfig.h"
#include "Inventory/CSPlayerInventory.h"
#include "Items/CSItemDefinition.h"
#include "Items/CSItemSettings.h"
#include "Pickups/CSAmmoMachine.h"
#include "Pickups/CSWorldPickup.h"
#include "Items/CSShopSettings.h"
#include "Components/AudioComponent.h"
#include "EngineUtils.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Net/UnrealNetwork.h"
#include "Perception/AISense_Hearing.h"
#include "Player/CSPlayerController.h"
#include "Settings/CSSettingsSubsystem.h"
#include "Weapons/CSWeaponComponent.h"
#include "Weapons/CSWeaponDefinition.h"
#include "Weapons/CSWeaponPresentation.h"
#include "Animation/CSAnimInstance.h"
#include "Account/CSAccountSubsystem.h"
#include "Audio/CSAudio.h"
#include "Audio/CSAudioSettings.h"
#include "Engine/SkeletalMesh.h"
#include "FX/CSEffects.h"

ACSCharacter::ACSCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCSCharacterMovementComponent>(
		ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;

	bReplicates = true;
	SetReplicateMovement(true);

	// Mouse drives yaw directly; the body does not chase the camera.
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	UCapsuleComponent* Capsule = GetCapsuleComponent();
	Capsule->InitCapsuleSize(34.f, 88.f);

	// The capsule stays the hit volume: the authority's hitscan traces on
	// ECC_Visibility and resolves head / torso / limb from the impact height
	// on the capsule. The skinned meshes are visual only, so an animation can
	// never move a hitbox and make two peers disagree about a hit.
	Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// --- First person camera ----------------------------------------------
	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(Capsule);
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, CameraHeight));
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->SetFieldOfView(DefaultFieldOfView);

	// --- First person arms -------------------------------------------------
	// The same Mannequin as the body, attached to the camera so it pitches with
	// the view, with head and legs hidden (see SetupCharacterMeshes). The
	// offset puts the Mannequin's eyes at the camera.
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonMesh"));
	FirstPersonMesh->SetupAttachment(FirstPersonCamera);
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->bCastDynamicShadow = false;
	FirstPersonMesh->CastShadow = false;
	FirstPersonMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonMesh->SetRelativeLocation(FirstPersonMeshOffset);
	FirstPersonMesh->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	FirstPersonMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
	FirstPersonMesh->SetAnimInstanceClass(UCSAnimInstance::StaticClass());

	// --- Third person body -------------------------------------------------
	USkeletalMeshComponent* Body = GetMesh();
	Body->SetOwnerNoSee(true);
	Body->bCastHiddenShadow = true;           // owner still sees their shadow
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetRelativeLocation(FVector(0.f, 0.f, -88.f));
	Body->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	// Keep animating while unseen so the owner's shadow and remote players
	// behind the camera stay in sync.
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
	Body->SetAnimInstanceClass(UCSAnimInstance::StaticClass());

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MannyMesh(
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	if (MannyMesh.Succeeded())
	{
		Body->SetSkeletalMesh(MannyMesh.Object);
		FirstPersonMesh->SetSkeletalMesh(MannyMesh.Object);
	}

	// --- Weapons in the hands ------------------------------------------------
	auto MakeWeaponMesh = [this](const TCHAR* Name, USkeletalMeshComponent* Parent) -> USkeletalMeshComponent*
	{
		USkeletalMeshComponent* Weapon = CreateDefaultSubobject<USkeletalMeshComponent>(Name);
		Weapon->SetupAttachment(Parent, WeaponSocket);
		Weapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Weapon->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		return Weapon;
	};
	FirstPersonWeapon = MakeWeaponMesh(TEXT("FirstPersonWeapon"), FirstPersonMesh);
	FirstPersonWeapon->SetOnlyOwnerSee(true);
	FirstPersonWeapon->CastShadow = false;
	ThirdPersonWeapon = MakeWeaponMesh(TEXT("ThirdPersonWeapon"), Body);
	ThirdPersonWeapon->SetOwnerNoSee(true);
	ThirdPersonWeapon->bCastHiddenShadow = true;

	auto MakeWeaponModel = [this](const TCHAR* Name, USkeletalMeshComponent* Parent) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Model = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Model->SetupAttachment(Parent, WeaponSocket);
		Model->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Model->SetGenerateOverlapEvents(false);
		return Model;
	};
	// First person: the model hangs off the camera and both hands reach for it
	// by IK (ACSCharacter::UpdateFirstPersonView); third person: in the hand.
	FirstPersonWeaponModel = MakeWeaponModel(TEXT("FirstPersonWeaponModel"), FirstPersonMesh);
	FirstPersonWeaponModel->SetupAttachment(FirstPersonCamera);
	FirstPersonWeaponModel->SetOnlyOwnerSee(true);
	FirstPersonWeaponModel->CastShadow = false;
	ThirdPersonWeaponModel = MakeWeaponModel(TEXT("ThirdPersonWeaponModel"), Body);
	ThirdPersonWeaponModel->SetOwnerNoSee(true);
	ThirdPersonWeaponModel->bCastHiddenShadow = true;

	WeaponComponent = CreateDefaultSubobject<UCSWeaponComponent>(TEXT("WeaponComponent"));

	// Fusion adopts an actor only when it has BOTH a UFusionActorComponent and
	// bReplicates. PlayerAttached means: owned by the controlling client, and
	// destroyed automatically when that player leaves the room.
	FusionActor = CreateDefaultSubobject<UFusionActorComponent>(TEXT("FusionActor"));
	FusionActor->Ownership = EFusionObjectOwnerFlags::PlayerAttached;
}

UCSCharacterMovementComponent* ACSCharacter::GetCSMovement() const
{
	return Cast<UCSCharacterMovementComponent>(GetCharacterMovement());
}

void ACSCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (FusionActor)
	{
		// Bind to OnObjectReady rather than trusting BeginPlay: before the
		// Fusion handshake completes, replicated values are still defaults and
		// HasAuthority() reads true on every peer.
		FusionActor->OnObjectReady.AddDynamic(this, &ACSCharacter::HandleFusionObjectReady);
		FusionActor->OnOwnerChanged.AddDynamic(this, &ACSCharacter::HandleFusionOwnerChanged);
	}

	SetupCharacterMeshes();
	RefreshMeshVisibility();
	ApplyInputMappings();

	if (UCSSettingsSubsystem* Settings = UCSSettingsSubsystem::Get(this))
	{
		PreferencesChangedHandle = Settings->OnPreferencesChanged.AddUObject(this, &ACSCharacter::ApplyLocalPreferences);
		FirstPersonCamera->SetFieldOfView(Settings->GetPreferences().FieldOfView);
	}
}

void ACSCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindCombatEvents();
	if (UWorld* World = GetWorld())
	{
		// The throw release is a lambda on the (game-instance owned) timer manager.
		World->GetTimerManager().ClearTimer(ThrowReleaseTimer);
	}
	if (UCSSettingsSubsystem* Settings = UCSSettingsSubsystem::Get(this))
	{
		Settings->OnPreferencesChanged.Remove(PreferencesChangedHandle);
	}
	if (UEnhancedInputLocalPlayerSubsystem* Input = MappedInputSubsystem.Get())
	{
		if (RuntimeMappingContext)
		{
			Input->RemoveMappingContext(RuntimeMappingContext);
		}
	}
	MappedInputSubsystem.Reset();
	Super::EndPlay(EndPlayReason);
}

void ACSCharacter::ApplyLocalPreferences()
{
	const UCSSettingsSubsystem* Settings = UCSSettingsSubsystem::Get(this);
	if (!Settings)
	{
		return;
	}

	// FOV is purely a view setting; applying it on every peer's copy of the
	// pawn is harmless because only the local player views through this camera.
	FirstPersonCamera->SetFieldOfView(Settings->GetPreferences().FieldOfView);

	// Rebuild the mapping so rebound keys take effect immediately.
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (PC && PC->IsLocalController() && PC->GetLocalPlayer() && RuntimeMappingContext)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Input =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Input->RemoveMappingContext(RuntimeMappingContext);
		}
		RuntimeMappingContext = nullptr;
		ApplyInputMappings();
	}
}

void ACSCharacter::HandleFusionObjectReady()
{
	bNetworkReady = true;
	// The owning player id is known only now: pick Manny or Quinn by it.
	SetupCharacterMeshes();
	RefreshMeshVisibility();

	// Ownership is only meaningful now, and the authority needs a record for
	// this player before it will accept anything from them.
	if (UCSAuthority::IsGameAuthority(this))
	{
		if (ACSMatchDirector* Director = ACSMatchDirector::Get(this))
		{
			Director->EnsurePlayer(GetOwningPlayerId());
		}
	}

	UE_LOG(LogCSNet, Log, TEXT("%s: Fusion object ready. CanWrite=%s owner=%d"),
		*GetName(), UCSAuthority::CanWrite(this) ? TEXT("yes") : TEXT("no"),
		GetOwningPlayerId());
}

void ACSCharacter::HandleFusionOwnerChanged()
{
	RefreshMeshVisibility();
}

void ACSCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	RefreshMeshVisibility();
	ApplyInputMappings();
}

void ACSCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	RefreshMeshVisibility();
}

bool ACSCharacter::IsLocalFirstPersonView() const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	return PC && PC->IsLocalPlayerController();
}

int32 ACSCharacter::GetOwningPlayerId() const
{
	return IsBot() ? BotId : UCSAuthority::GetOwningPlayerId(this);
}

bool ACSCharacter::RefuseBadAim(const TCHAR* RpcName, const FVector& Origin, const FVector& Direction) const
{
	if (CSValidate::IsSaneLocation(Origin) && CSValidate::IsSaneDirection(Direction))
	{
		return false;
	}
	const int32 PlayerId = GetOwningPlayerId();
	UE_LOG(LogCSSecurity, Warning, TEXT("%s from player %d refused: origin %s / direction %s are not usable numbers."),
		RpcName, PlayerId, *Origin.ToString(), *Direction.ToString());
	if (ACSMatchDirector* Director = ACSMatchDirector::Get(this))
	{
		Director->ReportViolation(PlayerId, ECSCheatReason::BadInput, /*Weight*/ 3.f,
			FString::Printf(TEXT("%s with NaN or out-of-range numbers"), RpcName));
	}
	return true;
}

bool ACSCharacter::IsBot() const
{
	if (bIsBot && UCSAuthority::IsGameAuthority(this))
	{
		// Offline everything is ours; in a room the bots are Master-Client-owned.
		return UCSAuthority::GetOwningPlayerId(this) == UCSAuthority::GetLocalPlayerId(this);
	}
	return bIsBot;
}

bool ACSCharacter::IsAliveAuthoritative() const
{
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	return Director && Director->IsPlayerAlive(GetOwningPlayerId());
}

void ACSCharacter::RefreshMeshVisibility()
{
	const bool bFirstPerson = IsLocalPlayerView();

	if (FirstPersonMesh)
	{
		FirstPersonMesh->SetVisibility(bFirstPerson, /*bPropagateToChildren*/ true);
		// Stage 8: nobody ever sees another pawn's arms rig, so it does not tick at all.
		FirstPersonMesh->SetComponentTickEnabled(bFirstPerson);
	}
	if (FirstPersonCamera)
	{
		FirstPersonCamera->SetActive(bFirstPerson);
	}
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		// SetOwnerNoSee already hides the body for the owner; this keeps the
		// shadow so the local player still sees their own silhouette.
		Body->SetVisibility(true, true);
		Body->bCastHiddenShadow = true;

		// Stage 8 optimisation. Hits are traced against the capsule (the body
		// has no collision), so a remote body off screen needs no pose - only
		// montages keep running so a death plays out correctly. Update rate
		// optimisation lowers the anim rate of small, distant bodies. The local
		// body keeps ticking always, because its hidden shadow must follow it.
		Body->VisibilityBasedAnimTickOption = bFirstPerson
			? EVisibilityBasedAnimTickOption::AlwaysTickPose
			: EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered;
		Body->bEnableUpdateRateOptimizations = !bFirstPerson;
	}
}

void ACSCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACSCharacter, Stance);
	DOREPLIFETIME(ACSCharacter, Heartbeat);
	DOREPLIFETIME(ACSCharacter, bIsBot);
	DOREPLIFETIME(ACSCharacter, BotId);
}

void ACSCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsLocallyControlled())
	{
		UpdateStance();
		if (!bIsBot)
		{
			UpdateFocusedPickup();
		}

		// Liveness beacon: see Heartbeat in the header. Only the owner writes it.
		HeartbeatAccumulator += DeltaSeconds;
		if (HeartbeatAccumulator >= 1.f)
		{
			HeartbeatAccumulator = 0.f;
			++Heartbeat;
		}
	}

	SyncWithDirector();
	BroadcastIdentity();
	UpdateMatchTicket();

	BindCombatEvents();
	UpdateWeaponPresentation();
	UpdateFootsteps(DeltaSeconds);
	UpdateCameraHeight(DeltaSeconds);
	UpdateProtectionLook(DeltaSeconds);
	UpdateFirstPersonView(DeltaSeconds);
	UpdateRemoteSmoothing(DeltaSeconds);
}

void ACSCharacter::UpdateStance()
{
	const UCSCharacterMovementComponent* Movement = GetCSMovement();
	if (!Movement)
	{
		return;
	}

	ECSStanceState NewStance = ECSStanceState::Standing;
	if (Movement->IsCrouching())
	{
		NewStance = ECSStanceState::Crouching;
	}
	else if (Movement->IsSprinting())
	{
		NewStance = ECSStanceState::Sprinting;
	}

	if (NewStance != Stance)
	{
		Stance = NewStance;
		OnRep_Stance();
	}
}

void ACSCharacter::OnRep_Stance()
{
	// Animation Blueprint (Stage 6) reads Stance; nothing to do here yet.
	UE_LOG(LogCS, VeryVerbose, TEXT("%s stance -> %s"),
		*GetName(), *UEnum::GetValueAsString(Stance));
}

float ACSCharacter::GetRemoteViewPitchDegrees() const
{
	if (IsLocallyControlled())
	{
		return GetControlRotation().Pitch;
	}

	// APawn packs the replicated view pitch into a uint16 (RemoteViewPitch16).
	// The uint8 RemoteViewPitch field it replaced is deprecated since 5.6.
	return (static_cast<float>(GetRemoteViewPitch()) * 360.f) / 65535.f;
}

void ACSCharacter::GetAimRay(FVector& OutOrigin, FVector& OutDirection) const
{
	if (FirstPersonCamera)
	{
		OutOrigin = FirstPersonCamera->GetComponentLocation();
		OutDirection = FirstPersonCamera->GetForwardVector();
		return;
	}

	OutOrigin = GetActorLocation();
	OutDirection = GetActorForwardVector();
}

void ACSCharacter::PlayLocalFireEffects(const UCSWeaponDefinition* Weapon)
{
	if (!Weapon || !IsLocallyControlled())
	{
		return;
	}

	// Purely local prediction: the shot must feel instant even though the
	// authority has not confirmed anything yet. Nothing here affects gameplay.
	// The tracer goes to what the crosshair is on right now; the authority's
	// impact arrives with RpcConfirmShot and gets the impact effect.
	FVector Origin;
	FVector Direction;
	GetAimRay(Origin, Direction);
	FVector End = Origin + Direction * Weapon->Range;
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(CSLocalTracer), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Origin, End, ECC_Visibility, Query))
	{
		End = Hit.ImpactPoint;
	}
	PlayShotPresentation(End, /*bLocalPrediction*/ true);

	AddControllerPitchInput(-Weapon->RecoilPitch);
	AddControllerYawInput(FMath::RandRange(-Weapon->RecoilYaw, Weapon->RecoilYaw));
}

// ---------------------------------------------------------------------------
// Combat wire contract
// ---------------------------------------------------------------------------
void ACSCharacter::RequestFire(const FVector& Origin, const FVector& Direction, bool bAiming)
{
	if (UCSAuthority::IsSessionActive(this))
	{
		RpcRequestFire(Origin, Direction, bAiming);
		return;
	}

	// Offline: same handler, no wire.
	RpcRequestFire_Receive(Origin, Direction, bAiming);
}

void ACSCharacter::RpcRequestFire_Receive(FVector Origin, FVector Direction, bool bAiming)
{
	// Runs on the Master Client. `this` is the shooter's pawn: the shooter id
	// comes from Fusion ownership, not the payload, and CSRpcGuard makes sure
	// the RPC was sent by that owner (any peer can send an RPC to any object).
	CS_AUTHORITY_ONLY(this);
	if (!CSRpcGuard::FromOwner(this, TEXT("RpcRequestFire")) || RefuseBotRpc() || RefuseBadAim(TEXT("RpcRequestFire"), Origin, Direction) || !PassesCheatGuard(ECSRequestKind::Fire))
	{
		return;
	}

	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	if (!Director || !WeaponComponent)
	{
		return;
	}

	const int32 ShooterId = GetOwningPlayerId();

	// What is in hand is decided by the authority's own state (director record
	// plus the Master-Client-owned inventory), never by the client.
	const FCSLoadoutView Loadout = Director->GetLoadout(ShooterId);
	const UCSWeaponDefinition* Weapon = Loadout.Weapon;

	// The authority traces from where IT believes the pawn is, never from the
	// client's claimed origin. The claim is only checked for plausibility.
	FVector AuthoritativeOrigin;
	FVector IgnoredDirection;
	GetAimRay(AuthoritativeOrigin, IgnoredDirection);

	const ECSFireRejection Verdict =
		Director->ValidateFire(ShooterId, Loadout, Origin, Direction, AuthoritativeOrigin);

	if (Verdict != ECSFireRejection::Accepted)
	{
		// Post-match refusals are expected (bots keep pulling the trigger), not suspicious.
		if (UCSCombatSettings::Get()->bLogRejections && Verdict != ECSFireRejection::MatchOver)
		{
			UE_LOG(LogCSAuth, Warning, TEXT("Fire from player %d rejected: %s"),
				ShooterId, *UEnum::GetValueAsString(Verdict));
		}
		return;
	}

	Director->CommitFire(ShooterId);

	// Spread is applied HERE, by the authority, so a client cannot shoot a
	// perfectly accurate shotgun by sending a clean direction. Each pellet of
	// a multi-pellet weapon gets its own random offset inside the cone.
	const float SpreadDeg = bAiming ? Weapon->AimSpreadDegrees : Weapon->HipSpreadDegrees;
	const float SpreadRad = FMath::DegreesToRadians(SpreadDeg);
	const FVector AimDir = Direction.GetSafeNormal();

	FVector FirstImpact = AuthoritativeOrigin + AimDir * Weapon->Range;
	bool bAnyPlayerHit = false;

	for (int32 Pellet = 0; Pellet < FMath::Max(1, Weapon->PelletsPerShot); ++Pellet)
	{
		const FVector PelletDir = SpreadRad > 0.f ? FMath::VRandCone(AimDir, SpreadRad) : AimDir;
		const FCSShotResolution Shot =
			WeaponComponent->ResolveShotOnAuthority(AuthoritativeOrigin, PelletDir, Weapon);

		if (Pellet == 0)
		{
			FirstImpact = Shot.ImpactPoint;
		}
		UE_LOG(LogCSCombat, Verbose, TEXT("Shot by %d from %s dir %s -> impact %s, victim %d"),
			ShooterId, *AuthoritativeOrigin.ToCompactString(), *PelletDir.ToCompactString(),
			*Shot.ImpactPoint.ToCompactString(), Shot.VictimPlayerId);

		if (Shot.VictimPlayerId != 0 && Shot.VictimPlayerId != ShooterId)
		{
			Director->ApplyDamage(Shot.VictimPlayerId, ShooterId, Shot.Damage, Shot.Zone);
			bAnyPlayerHit = true;
		}
	}

	// Cosmetic only; the damage already happened in replicated state.
	if (UCSAuthority::IsSessionActive(this))
	{
		RpcConfirmShot(AuthoritativeOrigin, FirstImpact, bAnyPlayerHit);
	}
	else
	{
		RpcConfirmShot_Receive(AuthoritativeOrigin, FirstImpact, bAnyPlayerHit);
	}
}

void ACSCharacter::RpcConfirmShot_Receive(FVector Origin, FVector Impact, bool bHitPlayer)
{
	// Runs on every peer. The shooter already showed flash, sound and tracer
	// when they clicked (PlayLocalFireEffects); everyone else shows them now.
	// The impact is shown by all, at the point the authority decided.
	if (!CSRpcGuard::FromMasterClient(this, TEXT("RpcConfirmShot"))
		|| !CSValidate::IsSaneLocation(Origin) || !CSValidate::IsSaneLocation(Impact))
	{
		return;
	}
	if (!IsLocalPlayerView())
	{
		PlayShotPresentation(Impact, /*bLocalPrediction*/ false);
	}
	CSEffects::Impact(GetWorld(), Impact, (Origin - Impact).GetSafeNormal(), bHitPlayer);

	// Bots hear gunshots (Stage 7). Only the authority runs bot perception.
	if (UCSAuthority::IsGameAuthority(this))
	{
		UAISense_Hearing::ReportNoiseEvent(GetWorld(), Origin, 1.f, this, 3000.f);
	}

	UE_LOG(LogCSCombat, VeryVerbose, TEXT("%s shot confirmed, hit player: %s"),
		*GetName(), bHitPlayer ? TEXT("yes") : TEXT("no"));
}

void ACSCharacter::RequestReload()
{
	if (UCSAuthority::IsSessionActive(this))
	{
		RpcRequestReload();
		return;
	}
	RpcRequestReload_Receive();
}

void ACSCharacter::RpcRequestReload_Receive()
{
	CS_AUTHORITY_ONLY(this);
	if (!CSRpcGuard::FromOwner(this, TEXT("RpcRequestReload")) || RefuseBotRpc() || !PassesCheatGuard(ECSRequestKind::Reload))
	{
		return;
	}

	if (ACSMatchDirector* Director = ACSMatchDirector::Get(this))
	{
		Director->BeginReload(GetOwningPlayerId());
	}
}

// ---------------------------------------------------------------------------
// Inventory wire contract
// ---------------------------------------------------------------------------

void ACSCharacter::UpdateFocusedPickup()
{
	FocusedPickup = nullptr;
	FocusedMachine = nullptr;

	if (!IsAliveAuthoritative())
	{
		return;
	}

	FVector Eye;
	FVector Forward;
	GetAimRay(Eye, Forward);

	// An ammo machine in front of the player wins over a gun on the floor.
	{
		const float MachineReach = UCSShopSettings::Get()->AmmoMachineReach * 0.9f;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CSMachineSight), false, this);
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + Forward * MachineReach, ECC_Visibility, Params))
		{
			if (ACSAmmoMachine* Machine = Cast<ACSAmmoMachine>(Hit.GetActor()))
			{
				FocusedMachine = Machine;
				return;
			}
		}
	}

	const float Reach = UCSItemSettings::Get()->InteractReach;
	const float ReachSq = FMath::Square(Reach);

	// Choose the pickup closest to the crosshair among those within reach.
	// Pickups have no collision on purpose (they must never stop a bullet),
	// so this is a proximity + view-cone query, followed by a line-of-sight
	// check so items behind walls are not offered.
	ACSWorldPickup* Best = nullptr;
	float BestDot = 0.93f; // ~21 degree cone

	for (TActorIterator<ACSWorldPickup> It(GetWorld()); It; ++It)
	{
		ACSWorldPickup* Pickup = *It;
		if (!Pickup->IsAvailable())
		{
			continue;
		}

		const FVector ToPickup = Pickup->GetActorLocation() - Eye;
		if (ToPickup.SizeSquared() > ReachSq)
		{
			continue;
		}

		const float Dot = FVector::DotProduct(Forward, ToPickup.GetSafeNormal());
		if (Dot > BestDot)
		{
			BestDot = Dot;
			Best = Pickup;
		}
	}

	if (Best)
	{
		// Walls only: another player standing over the item (typically the
		// victim respawning, whose collision is back on before its new
		// position has arrived) must not hide it.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CSPickupSight), false, this);
		FHitResult Hit;
		const bool bBlocked = GetWorld()->LineTraceSingleByObjectType(
			Hit, Eye, Best->GetActorLocation(), FCollisionObjectQueryParams(ECC_WorldStatic), Params);
		if (!bBlocked)
		{
			FocusedPickup = Best;
		}
	}
}

void ACSCharacter::RequestPickupFocused()
{
	// E at an ammo machine buys ammo; otherwise it picks up the gun in view.
	if (const ACSAmmoMachine* Machine = FocusedMachine.Get())
	{
		const int32 MachineIndex = Machine->GetSortedIndex();
		if (UCSAuthority::IsSessionActive(this))
		{
			RpcRequestAmmo(MachineIndex);
		}
		else
		{
			RpcRequestAmmo_Receive(MachineIndex);
		}
		return;
	}

	ACSWorldPickup* Pickup = FocusedPickup.Get();
	if (!Pickup)
	{
		return;
	}

	if (UCSAuthority::IsSessionActive(this))
	{
		RpcRequestPickup(Pickup);
		return;
	}
	RpcRequestPickup_Receive(Pickup);
}

void ACSCharacter::RpcRequestPickup_Receive(AActor* PickupActor)
{
	CS_AUTHORITY_ONLY(this);
	if (!CSRpcGuard::FromOwner(this, TEXT("RpcRequestPickup")) || RefuseBotRpc() || !PassesCheatGuard(ECSRequestKind::Pickup))
	{
		return;
	}

	const int32 PlayerId = GetOwningPlayerId();
	ACSWorldPickup* Pickup = Cast<ACSWorldPickup>(PickupActor);
	ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(this, PlayerId);
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);

	auto Reject = [PlayerId](const TCHAR* Reason)
	{
		UE_LOG(LogCSAuth, Warning, TEXT("Pickup by player %d rejected: %s"), PlayerId, Reason);
	};

	if (!Pickup || !Inventory || !Director)
	{
		return Reject(TEXT("pickup or inventory missing"));
	}
	if (!Director->IsPlayerAlive(PlayerId))
	{
		return Reject(TEXT("player is dead"));
	}
	// Already claimed means another player's request arrived first: the
	// authority handles requests one at a time, so exactly one wins.
	if (!Pickup->IsAvailable())
	{
		return Reject(TEXT("already taken"));
	}

	// Distance from where the AUTHORITY believes this pawn is.
	const float MaxDistance = UCSItemSettings::Get()->MaxPickupDistance;
	if (FVector::DistSquared(GetActorLocation(), Pickup->GetActorLocation()) > FMath::Square(MaxDistance))
	{
		return Reject(TEXT("too far"));
	}

	// Claim first, so the gun cannot be taken twice; a gun already in that slot
	// is dropped in its place (a swap), and the new one comes up in the hands.
	const int32 ItemIndex = Pickup->GetItemIndex();
	const int32 Mag = Pickup->GetAmmoInMag();
	const int32 Spare = Pickup->GetReserve();
	if (ACSPlayerInventory::SlotForItem(UCSItemSettings::Get()->GetItem(ItemIndex)) == INDEX_NONE || !Pickup->Claim())
	{
		return Reject(TEXT("not a carried item"));
	}
	if (!Director->GiveItem(PlayerId, ItemIndex, /*bEquip*/ true, Mag, Spare))
	{
		return Reject(TEXT("slot refused"));
	}
	UE_LOG(LogCSInventory, Log, TEXT("Player %d picked up item %d (%d / %d)"), PlayerId, ItemIndex, Mag, Spare);
}

void ACSCharacter::RequestSlot(int32 Slot)
{
	if (UCSAuthority::IsSessionActive(this))
	{
		RpcRequestSlot(Slot);
		return;
	}
	RpcRequestSlot_Receive(Slot);
}

void ACSCharacter::RpcRequestSlot_Receive(int32 Slot)
{
	CS_AUTHORITY_ONLY(this);
	if (!CSRpcGuard::FromOwner(this, TEXT("RpcRequestSlot")) || RefuseBotRpc() || !PassesCheatGuard(ECSRequestKind::Slot))
	{
		return;
	}

	const int32 PlayerId = GetOwningPlayerId();
	ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(this, PlayerId);
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	if (!Inventory || !Director || !Director->IsPlayerAlive(PlayerId))
	{
		return;
	}

	// An empty slot key does nothing; the same slot again does nothing.
	if (Inventory->HasItemInSlot(Slot) && Inventory->GetEquippedSlot() != Slot)
	{
		Director->CancelReload(PlayerId);
		Inventory->SetEquippedSlot(Slot);
	}
}

void ACSCharacter::RequestDropEquipped()
{
	const ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(this, GetOwningPlayerId());
	const int32 Slot = Inventory ? Inventory->GetEquippedSlot() : INDEX_NONE;
	if (!CSLoadout::IsDroppable(Slot))
	{
		// The knife and grenades never leave the hands this way.
		return;
	}

	if (UCSAuthority::IsSessionActive(this))
	{
		RpcRequestDrop(Slot);
		return;
	}
	RpcRequestDrop_Receive(Slot);
}

void ACSCharacter::RpcRequestDrop_Receive(int32 Slot)
{
	CS_AUTHORITY_ONLY(this);
	if (!CSRpcGuard::FromOwner(this, TEXT("RpcRequestDrop")) || RefuseBotRpc() || !PassesCheatGuard(ECSRequestKind::Drop))
	{
		return;
	}

	const int32 PlayerId = GetOwningPlayerId();
	ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(this, PlayerId);
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	if (!Inventory || !Director || !Director->IsPlayerAlive(PlayerId) || !CSLoadout::IsDroppable(Slot))
	{
		return;
	}

	FCSInventorySlot SlotData;
	if (!Inventory->GetSlot(Slot, SlotData) || SlotData.IsEmpty())
	{
		return;
	}

	// The gun keeps the rounds it was dropped with, loaded and spare.
	Director->CancelReload(PlayerId);
	const FCSInventorySlot Removed = Inventory->RemoveFromSlot(Slot, 1);
	if (Removed.IsEmpty())
	{
		return;
	}
	ACSWorldPickup::MakeRoomForDrops(this, 1);
	Director->SpawnDroppedItem(Removed,
		GetActorLocation() + GetActorForwardVector() * 110.f - FVector(0.f, 0.f, 60.f),
		GetActorLocation() + FVector(0.f, 0.f, 30.f));

	UE_LOG(LogCSInventory, Log, TEXT("Player %d dropped item %d (%d / %d)"), PlayerId, Removed.ItemIndex, Removed.AmmoInMag, Removed.Reserve);
}

void ACSCharacter::RpcRequestAmmo_Receive(int32 MachineIndex)
{
	CS_AUTHORITY_ONLY(this);
	if (!CSRpcGuard::FromOwner(this, TEXT("RpcRequestAmmo")) || RefuseBotRpc() || !PassesCheatGuard(ECSRequestKind::Buy))
	{
		return;
	}
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	const ACSAmmoMachine* Machine = ACSAmmoMachine::FindByIndex(this, MachineIndex);
	if (!Director || !Machine)
	{
		return;
	}
	const ECSBuyResult Result = Director->TryBuyAmmo(GetOwningPlayerId(), Machine, GetActorLocation());
	UE_LOG(LogCSInventory, Log, TEXT("Ammo machine %d for player %d: %s"), MachineIndex, GetOwningPlayerId(), *UEnum::GetValueAsString(Result));
}

// ---------------------------------------------------------------------------
// v2.0 knife
// ---------------------------------------------------------------------------

void ACSCharacter::RequestMelee(bool bHeavy)
{
	FVector Origin;
	FVector Direction;
	GetAimRay(Origin, Direction);

	// Local swing right away (arms, whoosh); the authority decides the hit.
	for (UCSAnimInstance* Anim : { GetBodyAnim(), GetArmsAnim() })
	{
		if (Anim)
		{
			Anim->PlayMelee(bHeavy);
		}
	}
	if (IsLocalPlayerView())
	{
		ViewMeleeTime = 0.f;
		bViewMeleeHeavy = bHeavy;
	}
	CSAudio::Play2D(this, UCSAudioSettings::Get()->KnifeSwing, bHeavy ? 0.9f : 0.75f, bHeavy ? 0.85f : FMath::FRandRange(1.f, 1.12f));

	if (UCSAuthority::IsSessionActive(this))
	{
		RpcRequestMelee(Origin, Direction, bHeavy);
		return;
	}
	RpcRequestMelee_Receive(Origin, Direction, bHeavy);
}

void ACSCharacter::RpcRequestMelee_Receive(FVector Origin, FVector Direction, bool bHeavy)
{
	CS_AUTHORITY_ONLY(this);
	if (!CSRpcGuard::FromOwner(this, TEXT("RpcRequestMelee")) || RefuseBotRpc() || RefuseBadAim(TEXT("RpcRequestMelee"), Origin, Direction) || !PassesCheatGuard(ECSRequestKind::Fire))
	{
		return;
	}
	ResolveMeleeOnAuthority(Origin, Direction, bHeavy);
}

void ACSCharacter::ResolveMeleeOnAuthority(const FVector& Origin, const FVector& Direction, bool bHeavy)
{
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	if (!Director || !WeaponComponent)
	{
		return;
	}
	const int32 AttackerId = GetOwningPlayerId();
	FVector AuthoritativeOrigin;
	FVector IgnoredDirection;
	GetAimRay(AuthoritativeOrigin, IgnoredDirection);
	if (!Director->AcceptMelee(AttackerId, bHeavy, Origin, AuthoritativeOrigin))
	{
		return;
	}
	const UCSWeaponDefinition* Knife = Director->GetLoadout(AttackerId).Weapon;
	if (!Knife)
	{
		return;
	}

	// A small fan of rays: a blade sweeps, it does not need pixel precision.
	const FVector Aim = Direction.GetSafeNormal();
	const FRotator AimRot = Aim.Rotation();
	FCSShotResolution Best;
	for (const FVector2D& Offset : { FVector2D(0.f, 0.f), FVector2D(-9.f, 0.f), FVector2D(9.f, 0.f), FVector2D(0.f, -5.f), FVector2D(0.f, 5.f) })
	{
		const FVector Ray = (AimRot + FRotator(Offset.Y, Offset.X, 0.f)).Vector();
		const FCSShotResolution Shot = WeaponComponent->ResolveShotOnAuthority(AuthoritativeOrigin, Ray, Knife);
		if (Shot.VictimPlayerId != 0 && Shot.VictimPlayerId != AttackerId)
		{
			Best = Shot;
			break;
		}
		if (!Best.bHit && Shot.bHit)
		{
			Best = Shot;
		}
	}

	int32 HitKind = 0; // 0 air, 1 wall, 2 body
	if (Best.VictimPlayerId != 0 && Best.VictimPlayerId != AttackerId)
	{
		float Damage = bHeavy ? Knife->MeleeHeavyDamage : Knife->MeleeDamage;
		// From behind: the victim faces away from the attacker.
		if (const ACSCharacter* Victim = ACSMatchDirector::FindPawnForPlayer(this, Best.VictimPlayerId))
		{
			const FVector ToVictim = (Victim->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
			if (FVector::DotProduct(Victim->GetActorForwardVector().GetSafeNormal2D(), ToVictim) > 0.5f)
			{
				Damage *= Knife->BackstabMultiplier;
			}
		}
		Director->ApplyDamage(Best.VictimPlayerId, AttackerId, Damage, Best.Zone == ECSHitZone::Head ? ECSHitZone::Head : ECSHitZone::Torso);
		HitKind = 2;
	}
	else if (Best.bHit)
	{
		HitKind = 1;
	}

	const FVector Impact = Best.bHit ? Best.ImpactPoint : AuthoritativeOrigin + Aim * Knife->MeleeRange;
	if (UCSAuthority::IsSessionActive(this))
	{
		RpcMeleeSwing(bHeavy, Impact, HitKind);
	}
	else
	{
		RpcMeleeSwing_Receive(bHeavy, Impact, HitKind);
	}
}

void ACSCharacter::RpcMeleeSwing_Receive(bool bHeavy, FVector Impact, int32 HitKind)
{
	if (!CSRpcGuard::FromMasterClient(this, TEXT("RpcMeleeSwing")) || !CSValidate::IsSaneLocation(Impact))
	{
		return;
	}
	// The attacker already played the swing locally.
	if (!IsLocalPlayerView())
	{
		for (UCSAnimInstance* Anim : { GetBodyAnim(), GetArmsAnim() })
		{
			if (Anim)
			{
				Anim->PlayMelee(bHeavy);
			}
		}
		CSAudio::PlayAt(this, UCSAudioSettings::Get()->KnifeSwing, GetActorLocation(), 0.7f);
	}
	if (HitKind == 2)
	{
		CSAudio::PlayAt(this, UCSAudioSettings::Get()->KnifeHitBody, Impact, 1.f, FMath::FRandRange(0.95f, 1.05f));
		CSEffects::Impact(GetWorld(), Impact, (GetActorLocation() - Impact).GetSafeNormal(), /*bHitPlayer*/ true);
	}
	else if (HitKind == 1)
	{
		CSAudio::PlayAt(this, UCSAudioSettings::Get()->KnifeHitWall, Impact, 0.9f, FMath::FRandRange(0.95f, 1.08f));
		CSEffects::Impact(GetWorld(), Impact, (GetActorLocation() - Impact).GetSafeNormal(), /*bHitPlayer*/ false);
	}
}

// ---------------------------------------------------------------------------
// v2.0 flashbang (local view)
// ---------------------------------------------------------------------------

void ACSCharacter::ApplyFlash(float Strength, float Seconds)
{
	// A second flash only makes things worse, never better.
	if (Strength < GetFlashAmount())
	{
		return;
	}
	FlashStrength = FMath::Clamp(Strength, 0.f, 1.f);
	FlashSeconds = FMath::Max(0.3f, Seconds);
	FlashStart = GetWorld()->GetTimeSeconds();

	if (UAudioComponent* Old = FlashRing.Get())
	{
		Old->Stop();
	}
	FlashRing = CSAudio::Spawn2D(this, UCSAudioSettings::Get()->FlashbangRing, 0.2f + 0.6f * FlashStrength);
}

float ACSCharacter::GetFlashAmount() const
{
	if (FlashSeconds <= 0.f || !GetWorld())
	{
		return 0.f;
	}
	const float Elapsed = GetWorld()->GetTimeSeconds() - FlashStart;
	if (Elapsed >= FlashSeconds)
	{
		return 0.f;
	}
	// Full white for the first 40 %, then a slow fade - like the real thing.
	const float Hold = FlashSeconds * 0.4f;
	const float Fade = Elapsed <= Hold ? 1.f : 1.f - (Elapsed - Hold) / (FlashSeconds - Hold);
	return FlashStrength * FMath::Clamp(Fade, 0.f, 1.f);
}

void ACSCharacter::RequestThrowGrenade()
{
	if (GetWorldTimerManager().IsTimerActive(ThrowReleaseTimer))
	{
		return;
	}
	// Wind-up now; the grenade leaves the hand at the release point of the
	// motion, aimed wherever the player looks by then.
	PlayThrowPresentation();
	CSAudio::Play2D(this, UCSAudioSettings::Get()->GrenadePin, 0.7f);
	GetWorldTimerManager().SetTimer(ThrowReleaseTimer, [this]()
	{
		FVector Origin;
		FVector Direction;
		GetAimRay(Origin, Direction);
		if (UCSAuthority::IsSessionActive(this))
		{
			RpcRequestThrow(Origin, Direction);
			return;
		}
		RpcRequestThrow_Receive(Origin, Direction);
	}, 0.26f, false);
}

void ACSCharacter::RpcRequestThrow_Receive(FVector Origin, FVector Direction)
{
	CS_AUTHORITY_ONLY(this);
	if (!CSRpcGuard::FromOwner(this, TEXT("RpcRequestThrow")) || RefuseBotRpc() || RefuseBadAim(TEXT("RpcRequestThrow"), Origin, Direction) || !PassesCheatGuard(ECSRequestKind::Throw))
	{
		return;
	}
	if (ACSMatchDirector* Director = ACSMatchDirector::Get(this))
	{
		FVector AuthoritativeOrigin;
		FVector IgnoredDirection;
		GetAimRay(AuthoritativeOrigin, IgnoredDirection);
		Director->TryThrowGrenade(GetOwningPlayerId(), Origin, Direction, AuthoritativeOrigin, GetVelocity());
	}
}

void ACSCharacter::PlayThrowPresentation(bool bFromRelease)
{
	// From the release point when the grenade is already out (seen from elsewhere, bots).
	const float StartAt = bFromRelease ? 0.2f : 0.f;
	for (UCSAnimInstance* Anim : { GetBodyAnim(), GetArmsAnim() })
	{
		if (Anim)
		{
			Anim->PlayThrow(StartAt);
		}
	}
	if (IsLocalPlayerView())
	{
		ViewThrowTime = StartAt;
	}
}

void ACSCharacter::RequestBuy(int32 ShopIndex)
{
	if (UCSAuthority::IsSessionActive(this))
	{
		RpcRequestBuy(ShopIndex);
		return;
	}
	RpcRequestBuy_Receive(ShopIndex);
}

void ACSCharacter::RpcRequestBuy_Receive(int32 ShopIndex)
{
	CS_AUTHORITY_ONLY(this);
	if (!CSRpcGuard::FromOwner(this, TEXT("RpcRequestBuy")) || RefuseBotRpc() || !PassesCheatGuard(ECSRequestKind::Buy))
	{
		return;
	}
	if (ACSMatchDirector* Director = ACSMatchDirector::Get(this))
	{
		const ECSBuyResult Result = Director->TryBuy(GetOwningPlayerId(), ShopIndex);
		if (Result != ECSBuyResult::Ok)
		{
			UE_LOG(LogCSInventory, Log, TEXT("Buy %d by player %d refused: %s"), ShopIndex, GetOwningPlayerId(), *UEnum::GetValueAsString(Result));
		}
	}
}



// ---------------------------------------------------------------------------
// Alive state, driven by the authority
// ---------------------------------------------------------------------------

void ACSCharacter::SyncWithDirector()
{
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	if (!Director)
	{
		return;
	}

	const int32 PlayerId = GetOwningPlayerId();
	FCSPlayerCombatRecord Record;
	if (!Director->GetRecord(PlayerId, Record))
	{
		// Registration. Done here rather than only in HandleFusionObjectReady
		// because offline there is no Fusion handshake, so OnObjectReady never
		// fires and the player would never get a record (every shot then
		// rejected as NoRecord).
		//
		// In a session, wait for the handshake: until then ownership is not
		// resolved and GetOwningPlayerId() would fall back to the LOCAL id,
		// registering a remote pawn under the wrong player.
		const bool bOwnershipKnown = bNetworkReady || !UCSAuthority::IsSessionActive(this);
		if (bOwnershipKnown && UCSAuthority::IsGameAuthority(this))
		{
			const_cast<ACSMatchDirector*>(Director)->EnsurePlayer(PlayerId);
		}

		// The record existed and is gone: the authority processed this player's
		// departure. Their pawn can linger (Photon may not drop them for a long
		// time), so hide it on every peer rather than leave a frozen ghost.
		if (bHadRecord && !bDepartedHidden && !IsLocallyControlled())
		{
			bDepartedHidden = true;
			ApplyAliveState(false);
			GetMesh()->SetVisibility(false, true);
			UE_LOG(LogCSNet, Log, TEXT("%s: player %d departed - hiding pawn."), *GetName(), PlayerId);
		}
		return;
	}

	bHadRecord = true;

	if (Record.bAlive != bLocalAliveState)
	{
		bLocalAliveState = Record.bAlive;
		ApplyAliveState(Record.bAlive);

		const UCSAudioSettings* Audio = UCSAudioSettings::Get();
		CSAudio::PlayAt(this, Record.bAlive ? Audio->Respawn : Audio->Death, GetActorLocation());
	}

	// First sight of the record is the initial registration, not a respawn:
	// adopt the counter without teleporting. Treating it as a respawn sent
	// every player to spawn point 0 at match start, on top of each other.
	// v1.1: the authority now picks that first spawn properly (team pads, away
	// from enemies) instead of every record holding index 0, so the owner goes
	// there once.
	if (LastRespawnCounter == 0)
	{
		LastRespawnCounter = Record.RespawnCounter;
		if (IsLocallyControlled() && Record.bAlive)
		{
			HandleRespawn(Record.RespawnPointIndex);
		}
	}

	// A copy controlled elsewhere still follows the counter. Otherwise, when
	// control arrives here later - bots handed to a new host after a master
	// migration - every respawn that happened meanwhile looked like a fresh
	// one: all bots were teleported onto the same spawn point, stuck inside
	// each other, blind and frozen (v1.0 fix, found by the nethost scenario).
	if (!IsLocallyControlled())
	{
		LastRespawnCounter = Record.RespawnCounter;
	}

	// Only the owning client can move its own pawn, so only it acts on the
	// authority's respawn decision.
	if (IsLocallyControlled() && Record.RespawnCounter != LastRespawnCounter)
	{
		LastRespawnCounter = Record.RespawnCounter;
		if (Record.bAlive)
		{
			HandleRespawn(Record.RespawnPointIndex);
		}
	}
}

void ACSCharacter::ApplyAliveState(bool bNewAlive)
{
	UE_LOG(LogCSCombat, Log, TEXT("%s (player %d) is now %s"),
		*GetName(), GetOwningPlayerId(), bNewAlive ? TEXT("ALIVE") : TEXT("DEAD"));

	// Runs on EVERY peer off the authority's replicated flag, so a client that
	// ignores its own death still looks and behaves dead everywhere else.
	// The body stays visible and plays a death animation; the arms view of the
	// dead owner goes away (their HUD shows the death screen).
	if (UCSAnimInstance* BodyAnim = GetBodyAnim())
	{
		if (bNewAlive)
		{
			BodyAnim->ResetAlive();
		}
		else
		{
			// Fall away from where the killing shots came from.
			int32 Direction = 1; // back
			if (bHasLastHitFrom)
			{
				const FVector Local = GetActorRotation().UnrotateVector(LastHitFrom - GetActorLocation());
				const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
				Direction = FMath::Abs(Yaw) <= 45.f ? 0 : (FMath::Abs(Yaw) >= 135.f ? 1 : (Yaw < 0.f ? 2 : 3));
			}
			BodyAnim->PlayDeath(Direction);
		}
	}
	// v1.1: the body goes limp; the death clip above is only the fallback.
	SetRagdoll(!bNewAlive);
	if (FirstPersonMesh)
	{
		FirstPersonMesh->SetVisibility(bNewAlive && IsLocalPlayerView(), true);
	}
	if (ThirdPersonWeapon)
	{
		// Hide the gun while the body falls; the real one dropped as loot anyway.
		ThirdPersonWeapon->SetVisibility(bNewAlive);
	}
	if (ThirdPersonWeaponModel)
	{
		ThirdPersonWeaponModel->SetVisibility(bNewAlive);
	}
	bHasLastHitFrom = bNewAlive ? false : bHasLastHitFrom;

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(bNewAlive
			? ECollisionEnabled::QueryAndPhysics
			: ECollisionEnabled::NoCollision);
	}

	if (UCSCharacterMovementComponent* Movement = GetCSMovement())
	{
		Movement->SetMovementMode(bNewAlive ? MOVE_Walking : MOVE_None);
	}
}

void ACSCharacter::HandleRespawn(int32 SpawnPointIndex)
{
	const ACSGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACSGameMode>() : nullptr;
	if (!GameMode)
	{
		return;
	}

	if (const AActor* Start = GameMode->GetPlayerStartByIndex(SpawnPointIndex))
	{
		// The authority cannot move this pawn - it does not own it - so the
		// owning client performs the teleport itself once the authority has
		// flipped the record to alive.
		// Only the start's yaw: a pitched or rolled PlayerStart would otherwise
		// respawn the player staring at the sky.
		const FRotator Facing(0.f, Start->GetActorRotation().Yaw, 0.f);
		SetActorLocationAndRotation(Start->GetActorLocation(), Facing);
		if (AController* C = GetController())
		{
			C->SetControlRotation(Facing);
		}

		UE_LOG(LogCSCombat, Log, TEXT("%s respawned at %s (start rotation %s)"), *GetName(), *Start->GetName(),
			*Start->GetActorRotation().ToCompactString());
	}
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void ACSCharacter::ApplyInputMappings()
{
	if (!InputConfig)
	{
		UE_LOG(LogCS, Warning, TEXT("%s: ApplyInputMappings skipped - no InputConfig asset."), *GetName());
		return;
	}

	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	if (!LocalPlayer)
	{
		UE_LOG(LogCS, Warning, TEXT("%s: ApplyInputMappings skipped - controller has no LocalPlayer."), *GetName());
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Input =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer);
	if (!Input)
	{
		UE_LOG(LogCS, Error, TEXT("%s: EnhancedInputLocalPlayerSubsystem missing."), *GetName());
		return;
	}

	// The context is built in C++ rather than taken from the asset. Built once
	// per pawn and cached, because AddMappingContext keys off the object.
	if (!RuntimeMappingContext)
	{
		const UCSSettingsSubsystem* Settings = UCSSettingsSubsystem::Get(this);
		RuntimeMappingContext = InputConfig->BuildRuntimeMappingContext(this,
			[Settings](FName Id, const FKey& Default) { return Settings ? Settings->GetKeyFor(Id, Default) : Default; });
	}

	// Re-adding after a seamless travel is required: the subsystem's contexts
	// do not reliably survive the world change, and a stale duplicate is
	// harmless because AddMappingContext is idempotent per context object.
	Input->AddMappingContext(RuntimeMappingContext, InputConfig->MappingPriority);
	MappedInputSubsystem = Input;

	UE_LOG(LogCS, Log, TEXT("%s: runtime mapping context applied at priority %d (%d mappings)."),
		*GetName(), InputConfig->MappingPriority, RuntimeMappingContext->GetMappings().Num());
}

void ACSCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UE_LOG(LogCS, Log, TEXT("%s: SetupPlayerInputComponent, component class = %s"),
		*GetName(), *GetNameSafe(PlayerInputComponent ? PlayerInputComponent->GetClass() : nullptr));

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		UE_LOG(LogCS, Error,
			TEXT("Enhanced Input is required. Set DefaultPlayerInputClass to EnhancedPlayerInput ")
			TEXT("and DefaultInputComponentClass to EnhancedInputComponent in DefaultInput.ini."));
		return;
	}

	if (!InputConfig)
	{
		UE_LOG(LogCS, Warning, TEXT("%s has no InputConfig assigned; the pawn will not respond to input."), *GetName());
		return;
	}

	int32 Bound = 0;

	if (InputConfig->IA_Move)
	{
		Input->BindAction(InputConfig->IA_Move, ETriggerEvent::Triggered, this, &ACSCharacter::Input_Move);
		++Bound;
	}
	if (InputConfig->IA_Look)
	{
		Input->BindAction(InputConfig->IA_Look, ETriggerEvent::Triggered, this, &ACSCharacter::Input_Look);
		++Bound;
	}
	if (InputConfig->IA_Jump)
	{
		Input->BindAction(InputConfig->IA_Jump, ETriggerEvent::Started, this, &ACSCharacter::Input_JumpStart);
		Input->BindAction(InputConfig->IA_Jump, ETriggerEvent::Completed, this, &ACSCharacter::Input_JumpStop);
		++Bound;
	}
	if (InputConfig->IA_Sprint)
	{
		Input->BindAction(InputConfig->IA_Sprint, ETriggerEvent::Started, this, &ACSCharacter::Input_SprintStart);
		Input->BindAction(InputConfig->IA_Sprint, ETriggerEvent::Completed, this, &ACSCharacter::Input_SprintStop);
		++Bound;
	}
	if (InputConfig->IA_Crouch)
	{
		Input->BindAction(InputConfig->IA_Crouch, ETriggerEvent::Started, this, &ACSCharacter::Input_CrouchToggle);
		Input->BindAction(InputConfig->IA_Crouch, ETriggerEvent::Completed, this, &ACSCharacter::Input_CrouchRelease);
		++Bound;
	}
	if (InputConfig->IA_Fire)
	{
		Input->BindAction(InputConfig->IA_Fire, ETriggerEvent::Started, this, &ACSCharacter::Input_FireStart);
		Input->BindAction(InputConfig->IA_Fire, ETriggerEvent::Completed, this, &ACSCharacter::Input_FireStop);
		++Bound;
	}
	if (InputConfig->IA_Aim)
	{
		Input->BindAction(InputConfig->IA_Aim, ETriggerEvent::Started, this, &ACSCharacter::Input_AimStart);
		Input->BindAction(InputConfig->IA_Aim, ETriggerEvent::Completed, this, &ACSCharacter::Input_AimStop);
		++Bound;
	}
	if (InputConfig->IA_Reload)
	{
		Input->BindAction(InputConfig->IA_Reload, ETriggerEvent::Started, this, &ACSCharacter::Input_Reload);
		++Bound;
	}
	if (InputConfig->IA_Interact)
	{
		Input->BindAction(InputConfig->IA_Interact, ETriggerEvent::Started, this, &ACSCharacter::Input_Interact);
		++Bound;
	}
	if (InputConfig->IA_EquipSlot)
	{
		Input->BindAction(InputConfig->IA_EquipSlot, ETriggerEvent::Started, this, &ACSCharacter::Input_EquipSlot);
		++Bound;
	}
	if (InputConfig->IA_Drop)
	{
		Input->BindAction(InputConfig->IA_Drop, ETriggerEvent::Started, this, &ACSCharacter::Input_Drop);
		++Bound;
	}
	if (InputConfig->IA_PauseMenu)
	{
		Input->BindAction(InputConfig->IA_PauseMenu, ETriggerEvent::Started, this, &ACSCharacter::Input_PauseMenu);
		++Bound;
	}
	if (InputConfig->IA_Scoreboard)
	{
		// Hold to show: Started on press, Completed on release.
		Input->BindAction(InputConfig->IA_Scoreboard, ETriggerEvent::Started, this, &ACSCharacter::Input_ScoreboardStart);
		Input->BindAction(InputConfig->IA_Scoreboard, ETriggerEvent::Completed, this, &ACSCharacter::Input_ScoreboardStop);
		++Bound;
	}
	if (UInputAction* BuyAction = InputConfig->GetBuyMenuAction())
	{
		Input->BindAction(BuyAction, ETriggerEvent::Started, this, &ACSCharacter::Input_BuyMenu);
		++Bound;
	}

	UE_LOG(LogCS, Log, TEXT("%s: bound %d input actions."), *GetName(), Bound);

	ApplyInputMappings();
}

void ACSCharacter::Input_Move(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();

	if (Axis.IsNearlyZero() || !Controller)
	{
		return;
	}

	const FRotator YawOnly(0.f, Controller->GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(YawOnly).GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(FRotationMatrix(YawOnly).GetUnitAxis(EAxis::Y), Axis.X);
}

void ACSCharacter::Input_Look(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();

	// Sensitivity and invert-Y come from the local settings save.
	float Scale = BaseLookScale;
	float PitchSign = -1.f;
	if (const UCSSettingsSubsystem* Settings = UCSSettingsSubsystem::Get(this))
	{
		Scale *= Settings->GetPreferences().MouseSensitivity;
		PitchSign = Settings->GetPreferences().bInvertY ? 1.f : -1.f;
		// Zoomed in: turn proportionally slower, so the same mouse movement
		// moves the crosshair the same distance across the screen.
		const float BaseFov = Settings->GetPreferences().FieldOfView;
		if (FirstPersonCamera && BaseFov > 1.f)
		{
			Scale *= FMath::Clamp(FirstPersonCamera->FieldOfView / BaseFov, 0.1f, 1.f);
		}
	}
	AddLookSway(Axis);
	AddControllerYawInput(Axis.X * Scale);
	AddControllerPitchInput(PitchSign * Axis.Y * Scale);
}

void ACSCharacter::Input_JumpStart(const FInputActionValue& /*Value*/)
{
	Jump();
}

void ACSCharacter::Input_JumpStop(const FInputActionValue& /*Value*/)
{
	StopJumping();
}

void ACSCharacter::Input_SprintStart(const FInputActionValue& /*Value*/)
{
	if (UCSCharacterMovementComponent* Movement = GetCSMovement())
	{
		Movement->SetSprintInput(true);
	}
}

void ACSCharacter::Input_SprintStop(const FInputActionValue& /*Value*/)
{
	if (UCSCharacterMovementComponent* Movement = GetCSMovement())
	{
		Movement->SetSprintInput(false);
	}
}

void ACSCharacter::Input_CrouchToggle(const FInputActionValue& /*Value*/)
{
	const UCSCharacterMovementComponent* Movement = GetCSMovement();
	const bool bCurrentlyCrouching = Movement && Movement->IsCrouching();

	if (bToggleCrouch && bCurrentlyCrouching)
	{
		UnCrouch();
		return;
	}

	Crouch();
}

void ACSCharacter::Input_CrouchRelease(const FInputActionValue& /*Value*/)
{
	// Hold-to-crouch only; in toggle mode the key release does nothing.
	if (!bToggleCrouch)
	{
		UnCrouch();
	}
}

void ACSCharacter::Input_FireStart(const FInputActionValue& /*Value*/)
{
	if (WeaponComponent)
	{
		WeaponComponent->StartFire();
	}
}

void ACSCharacter::Input_FireStop(const FInputActionValue& /*Value*/)
{
	if (WeaponComponent)
	{
		WeaponComponent->StopFire();
	}
}

void ACSCharacter::Input_AimStart(const FInputActionValue& /*Value*/)
{
	if (WeaponComponent)
	{
		WeaponComponent->SetAiming(true);
	}
}

void ACSCharacter::Input_AimStop(const FInputActionValue& /*Value*/)
{
	if (WeaponComponent)
	{
		WeaponComponent->SetAiming(false);
	}
}

void ACSCharacter::Input_Reload(const FInputActionValue& /*Value*/)
{
	if (WeaponComponent)
	{
		WeaponComponent->RequestReload();
	}
}

void ACSCharacter::Input_Interact(const FInputActionValue& /*Value*/)
{
	RequestPickupFocused();
}

void ACSCharacter::Input_EquipSlot(const FInputActionValue& Value)
{
	// One action for all slot keys: each key carries a Scalar modifier equal
	// to its number (see UCSInputConfig::BuildRuntimeMappingContext).
	// v2.0: keys 1..5 are loadout slots 0..4 (primary, pistol, knife, frag, flash).
	const int32 KeyNumber = FMath::RoundToInt(Value.Get<float>());
	if (KeyNumber < 1 || KeyNumber > CSLoadout::NumSlots)
	{
		return;
	}
	RequestSlot(KeyNumber - 1);
}

void ACSCharacter::Input_Drop(const FInputActionValue& /*Value*/)
{
	RequestDropEquipped();
}

void ACSCharacter::Input_BuyMenu(const FInputActionValue& /*Value*/)
{
	if (ACSPlayerController* PC = Cast<ACSPlayerController>(GetController()))
	{
		PC->ToggleShopScreen();
	}
}

void ACSCharacter::Input_ScoreboardStart(const FInputActionValue& /*Value*/)
{
	if (ACSPlayerController* PC = Cast<ACSPlayerController>(GetController()))
	{
		PC->SetScoreboardHeld(true);
	}
}

void ACSCharacter::Input_ScoreboardStop(const FInputActionValue& /*Value*/)
{
	if (ACSPlayerController* PC = Cast<ACSPlayerController>(GetController()))
	{
		PC->SetScoreboardHeld(false);
	}
}

void ACSCharacter::Input_PauseMenu(const FInputActionValue& /*Value*/)
{
	if (ACSPlayerController* PC = Cast<ACSPlayerController>(GetController()))
	{
		PC->TogglePauseMenu();
	}
}

// ---------------------------------------------------------------------------
// v1.2 accounts: who is this player
// ---------------------------------------------------------------------------

void ACSCharacter::BroadcastIdentity()
{
	if (!IsLocallyControlled() || bIsBot)
	{
		return;
	}
	const UCSAccountSubsystem* Account = UCSAccountSubsystem::Get(this);
	if (!Account || !Account->IsReady())
	{
		return;
	}
	// Repeated for a while after spawning: a player who joins later has to
	// hear it too, and an RPC sent before they arrived is gone.
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < NextIdentityBroadcast)
	{
		return;
	}
	NextIdentityBroadcast = Now + 10.0;

	FString Nickname = Account->GetNickname();
	if (UCSAuthority::IsSessionActive(this))
	{
		RpcIdentify(Nickname);
	}
	else
	{
		RpcIdentify_Receive(Nickname);
	}
}

void ACSCharacter::RpcIdentify_Receive(FString& Nickname)
{
	// Runs on every peer. Cosmetic: the name in the HUD. Control characters
	// are dropped so a name cannot break the kill feed or the scoreboard.
	// Only the pawn's owner names it: nobody renames somebody else's pawn.
	if (!CSRpcGuard::FromOwner(this, TEXT("RpcIdentify")))
	{
		return;
	}
	FString Clean = Nickname.Left(24);
	Clean.ReplaceCharInline(TEXT('\n'), TEXT(' '));
	Clean.ReplaceCharInline(TEXT('\r'), TEXT(' '));
	Clean.ReplaceCharInline(TEXT('\t'), TEXT(' '));
	DisplayNickname = Clean.TrimStartAndEnd();
}

// ---------------------------------------------------------------------------
// v2.0 match records: participation tickets
// ---------------------------------------------------------------------------

void ACSCharacter::UpdateMatchTicket()
{
	if (!IsLocallyControlled() || bIsBot)
	{
		return;
	}
	UCSAccountSubsystem* Account = UCSAccountSubsystem::Get(this);
	const ACSGameState* GS = GetWorld() ? GetWorld()->GetGameState<ACSGameState>() : nullptr;
	if (!Account || !Account->IsReady() || !GS)
	{
		return;
	}
	const FString& MatchId = GS->GetBackendMatchId();
	if (MatchId.IsEmpty())
	{
		return;
	}

	// A new match: ask the backend for this player's own ticket, once.
	if (MatchId != TicketMatchId)
	{
		TicketMatchId = MatchId;
		Ticket.Reset();
		bTicketRequested = false;
	}
	if (Ticket.IsEmpty())
	{
		if (!bTicketRequested)
		{
			bTicketRequested = true;
			TWeakObjectPtr<ACSCharacter> WeakThis(this);
			Account->MatchTicket(MatchId, [WeakThis, MatchId](bool bOk, const FString& NewTicket)
			{
				if (ACSCharacter* Self = WeakThis.Get(); Self && bOk && Self->TicketMatchId == MatchId)
				{
					Self->Ticket = NewTicket;
					Self->NextTicketSend = 0.0;
				}
			});
		}
		return;
	}

	// Hand it to the Master Client now, and again every half minute: a new
	// master after a migration starts without it.
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < NextTicketSend)
	{
		return;
	}
	NextTicketSend = Now + 30.0;
	FString SendMatchId = TicketMatchId;
	FString SendTicket = Ticket;
	if (UCSAuthority::IsSessionActive(this))
	{
		RpcMatchTicket(SendMatchId, SendTicket);
	}
	else
	{
		RpcMatchTicket_Receive(SendMatchId, SendTicket);
	}
}

void ACSCharacter::RpcMatchTicket_Receive(FString& MatchId, FString& PlayerTicket)
{
	// Master Client. The backend verifies the ticket when the match is
	// reported; here it only has to belong to the running match.
	CS_AUTHORITY_ONLY(this);
	if (!CSRpcGuard::FromOwner(this, TEXT("RpcMatchTicket")) || RefuseBotRpc())
	{
		return;
	}
	const ACSGameState* GS = GetWorld() ? GetWorld()->GetGameState<ACSGameState>() : nullptr;
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	if (!GS || !Director || MatchId.IsEmpty() || MatchId != GS->GetBackendMatchId())
	{
		return;
	}
	Director->NoteTicket(GetOwningPlayerId(), PlayerTicket.Left(64));
}
