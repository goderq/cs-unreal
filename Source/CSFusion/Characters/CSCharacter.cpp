// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Characters/CSCharacter.h"

// Required from UE 5.8: generated .gen.cpp files are no longer auto-scanned.
#include UE_INLINE_GENERATED_CPP_BY_NAME(CSCharacter.fusion)

#include "Camera/CameraComponent.h"
#include "Characters/CSCharacterMovementComponent.h"
#include "Combat/CSMatchDirector.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Core/CSAuthority.h"
#include "Core/CSCombatSettings.h"
#include "Core/CSLog.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/CSGameMode.h"
#include "Input/CSInputConfig.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Net/UnrealNetwork.h"
#include "Weapons/CSWeaponComponent.h"
#include "Weapons/CSWeaponDefinition.h"

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

	// Until real character meshes arrive (Stage 6) the capsule is the hit
	// volume: the authority's hitscan traces on ECC_Visibility, which the
	// default Pawn profile ignores, so without this shots pass straight
	// through players.
	Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// --- First person camera ----------------------------------------------
	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(Capsule);
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, CameraHeight));
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->SetFieldOfView(DefaultFieldOfView);

	// --- First person arms -------------------------------------------------
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonMesh"));
	FirstPersonMesh->SetupAttachment(FirstPersonCamera);
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->bCastDynamicShadow = false;
	FirstPersonMesh->CastShadow = false;
	FirstPersonMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonMesh->SetRelativeLocation(FVector(0.f, 0.f, -10.f));

	// --- Third person body -------------------------------------------------
	USkeletalMeshComponent* Body = GetMesh();
	Body->SetOwnerNoSee(true);
	Body->bCastHiddenShadow = true;           // owner still sees their shadow
	Body->SetCollisionObjectType(ECC_Pawn);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	Body->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Body->SetRelativeLocation(FVector(0.f, 0.f, -88.f));
	Body->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));

	// --- Placeholder body (Stage 6 replaces with a skinned character) ---------
	// Without any visible geometry, remote players are invisible. A cylinder
	// torso and a sphere head, hidden from the owner, give everyone else
	// something to see and aim at. Visuals only - no collision.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(Capsule);
	PlaceholderBody->SetRelativeLocation(FVector(0.f, 0.f, -12.f));
	PlaceholderBody->SetRelativeScale3D(FVector(0.62f, 0.62f, 1.5f));
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderBody->SetOwnerNoSee(true);
	if (CylinderMesh.Succeeded())
	{
		PlaceholderBody->SetStaticMesh(CylinderMesh.Object);
	}

	PlaceholderHead = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderHead"));
	PlaceholderHead->SetupAttachment(Capsule);
	PlaceholderHead->SetRelativeLocation(FVector(0.f, 0.f, 70.f));
	PlaceholderHead->SetRelativeScale3D(FVector(0.4f));
	PlaceholderHead->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderHead->SetOwnerNoSee(true);
	if (SphereMesh.Succeeded())
	{
		PlaceholderHead->SetStaticMesh(SphereMesh.Object);
	}

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

	RefreshMeshVisibility();
	ApplyInputMappings();
}

void ACSCharacter::HandleFusionObjectReady()
{
	bNetworkReady = true;
	RefreshMeshVisibility();

	// Ownership is only meaningful now, and the authority needs a record for
	// this player before it will accept anything from them.
	if (UCSAuthority::IsGameAuthority(this))
	{
		if (ACSMatchDirector* Director = ACSMatchDirector::Get(this))
		{
			Director->EnsurePlayer(UCSAuthority::GetOwningPlayerId(this));
		}
	}

	UE_LOG(LogCSNet, Log, TEXT("%s: Fusion object ready. CanWrite=%s owner=%d"),
		*GetName(), UCSAuthority::CanWrite(this) ? TEXT("yes") : TEXT("no"),
		UCSAuthority::GetOwningPlayerId(this));
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
	return UCSAuthority::GetOwningPlayerId(this);
}

bool ACSCharacter::IsAliveAuthoritative() const
{
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	return Director && Director->IsPlayerAlive(GetOwningPlayerId());
}

void ACSCharacter::RefreshMeshVisibility()
{
	const bool bFirstPerson = IsLocallyControlled();

	if (FirstPersonMesh)
	{
		FirstPersonMesh->SetVisibility(bFirstPerson, /*bPropagateToChildren*/ true);
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
	}
}

void ACSCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACSCharacter, Stance);
}

void ACSCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsLocallyControlled())
	{
		UpdateStance();
	}

	SyncWithDirector();
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
	AddControllerPitchInput(-Weapon->RecoilPitch);
	AddControllerYawInput(FMath::RandRange(-Weapon->RecoilYaw, Weapon->RecoilYaw));

	// Stage 6 attaches muzzle flash, tracer and sound here.
}

// ---------------------------------------------------------------------------
// Combat wire contract
// ---------------------------------------------------------------------------

void ACSCharacter::RequestFire(const FVector& Origin, const FVector& Direction)
{
	if (UCSAuthority::IsSessionActive(this))
	{
		RpcRequestFire(Origin, Direction);
		return;
	}

	// Offline: same handler, no wire.
	RpcRequestFire_Receive(Origin, Direction);
}

void ACSCharacter::RpcRequestFire_Receive(FVector Origin, FVector Direction)
{
	// Runs on the Master Client. `this` is the shooter's pawn, so the sender
	// cannot be spoofed: the id comes from Fusion ownership, not the payload.
	CS_AUTHORITY_ONLY(this);

	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	if (!Director || !WeaponComponent)
	{
		return;
	}

	const int32 ShooterId = UCSAuthority::GetOwningPlayerId(this);
	const UCSWeaponDefinition* Weapon = WeaponComponent->GetActiveWeapon();

	// The authority traces from where IT believes the pawn is, never from the
	// client's claimed origin. The claim is only checked for plausibility.
	FVector AuthoritativeOrigin;
	FVector IgnoredDirection;
	GetAimRay(AuthoritativeOrigin, IgnoredDirection);

	const ECSFireRejection Verdict =
		Director->ValidateFire(ShooterId, Weapon, Origin, Direction, AuthoritativeOrigin);

	if (Verdict != ECSFireRejection::Accepted)
	{
		if (UCSCombatSettings::Get()->bLogRejections)
		{
			UE_LOG(LogCSAuth, Warning, TEXT("Fire from player %d rejected: %s"),
				ShooterId, *UEnum::GetValueAsString(Verdict));
		}
		return;
	}

	Director->CommitFire(ShooterId);

	const FCSShotResolution Shot =
		WeaponComponent->ResolveShotOnAuthority(AuthoritativeOrigin, Direction, Weapon);

	if (Shot.VictimPlayerId != 0 && Shot.VictimPlayerId != ShooterId)
	{
		Director->ApplyDamage(Shot.VictimPlayerId, ShooterId, Shot.Damage, Shot.Zone);
	}

	// Cosmetic only; the damage already happened in replicated state.
	RpcConfirmShot(AuthoritativeOrigin, Shot.ImpactPoint, Shot.VictimPlayerId != 0);
}

void ACSCharacter::RpcConfirmShot_Receive(FVector Origin, FVector Impact, bool bHitPlayer)
{
	// Stage 6 draws the tracer, impact decal and hit marker here.
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

	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	if (!Director || !WeaponComponent)
	{
		return;
	}

	Director->BeginReload(UCSAuthority::GetOwningPlayerId(this), WeaponComponent->GetActiveWeapon());
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
		return;
	}

	if (Record.bAlive != bLocalAliveState)
	{
		bLocalAliveState = Record.bAlive;
		ApplyAliveState(Record.bAlive);
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
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		Body->SetVisibility(bNewAlive, true);
	}
	if (PlaceholderBody)
	{
		PlaceholderBody->SetVisibility(bNewAlive);
	}
	if (PlaceholderHead)
	{
		PlaceholderHead->SetVisibility(bNewAlive);
	}
	if (FirstPersonMesh)
	{
		FirstPersonMesh->SetVisibility(bNewAlive && IsLocallyControlled(), true);
	}

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
		SetActorLocationAndRotation(Start->GetActorLocation(), Start->GetActorRotation());
		if (AController* C = GetController())
		{
			C->SetControlRotation(Start->GetActorRotation());
		}

		UE_LOG(LogCSCombat, Log, TEXT("%s respawned at %s"), *GetName(), *Start->GetName());
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
		RuntimeMappingContext = InputConfig->BuildRuntimeMappingContext(this);
	}

	// Re-adding after a seamless travel is required: the subsystem's contexts
	// do not reliably survive the world change, and a stale duplicate is
	// harmless because AddMappingContext is idempotent per context object.
	Input->AddMappingContext(RuntimeMappingContext, InputConfig->MappingPriority);

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

	// Per-player sensitivity and invert-Y are applied by the settings system
	// (Stage 8) through the Input Mapping Context modifiers, so raw values are
	// used here and only scaled by the pawn's own base multiplier.
	AddControllerYawInput(Axis.X * BaseLookScale);
	AddControllerPitchInput(-Axis.Y * BaseLookScale);
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
