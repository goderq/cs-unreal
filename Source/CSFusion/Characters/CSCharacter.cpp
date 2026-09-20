// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Characters/CSCharacter.h"

#include "Camera/CameraComponent.h"
#include "Characters/CSCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/CSAuthority.h"
#include "Core/CSLog.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Input/CSInputConfig.h"
#include "InputActionValue.h"
#include "Net/UnrealNetwork.h"

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
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	Body->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Body->SetRelativeLocation(FVector(0.f, 0.f, -88.f));
	Body->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));

#if CS_WITH_FUSION
	// Fusion adopts an actor only when it has BOTH a UFusionActorComponent and
	// bReplicates. PlayerAttached means: owned by the controlling client, and
	// destroyed automatically when that player leaves the room.
	FusionActor = CreateDefaultSubobject<UFusionActorComponent>(TEXT("FusionActor"));
	FusionActor->Ownership = EFusionObjectOwnerFlags::PlayerAttached;
#endif
}

UCSCharacterMovementComponent* ACSCharacter::GetCSMovement() const
{
	return Cast<UCSCharacterMovementComponent>(GetCharacterMovement());
}

void ACSCharacter::BeginPlay()
{
	Super::BeginPlay();

#if CS_WITH_FUSION
	if (FusionActor)
	{
		// Bind to OnObjectReady rather than trusting BeginPlay: before the
		// Fusion handshake completes, replicated values are still defaults and
		// HasAuthority() reads true on every peer.
		FusionActor->OnObjectReady.AddDynamic(this, &ACSCharacter::HandleFusionObjectReady);
		FusionActor->OnOwnerChanged.AddDynamic(this, &ACSCharacter::HandleFusionOwnerChanged);
	}
#else
	bNetworkReady = true;
#endif

	RefreshMeshVisibility();
	ApplyInputMappings();
}

#if CS_WITH_FUSION
void ACSCharacter::HandleFusionObjectReady()
{
	bNetworkReady = true;
	RefreshMeshVisibility();

	UE_LOG(LogCSNet, Log, TEXT("%s: Fusion object ready. CanWrite=%s"),
		*GetName(), UCSAuthority::CanWrite(this) ? TEXT("yes") : TEXT("no"));
}

void ACSCharacter::HandleFusionOwnerChanged()
{
	RefreshMeshVisibility();
}
#endif

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

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void ACSCharacter::ApplyInputMappings()
{
	if (!InputConfig || !InputConfig->DefaultMappingContext)
	{
		return;
	}

	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Input =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		Input->AddMappingContext(InputConfig->DefaultMappingContext, InputConfig->MappingPriority);
	}
}

void ACSCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

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

	if (InputConfig->IA_Move)
	{
		Input->BindAction(InputConfig->IA_Move, ETriggerEvent::Triggered, this, &ACSCharacter::Input_Move);
	}
	if (InputConfig->IA_Look)
	{
		Input->BindAction(InputConfig->IA_Look, ETriggerEvent::Triggered, this, &ACSCharacter::Input_Look);
	}
	if (InputConfig->IA_Jump)
	{
		Input->BindAction(InputConfig->IA_Jump, ETriggerEvent::Started, this, &ACSCharacter::Input_JumpStart);
		Input->BindAction(InputConfig->IA_Jump, ETriggerEvent::Completed, this, &ACSCharacter::Input_JumpStop);
	}
	if (InputConfig->IA_Sprint)
	{
		Input->BindAction(InputConfig->IA_Sprint, ETriggerEvent::Started, this, &ACSCharacter::Input_SprintStart);
		Input->BindAction(InputConfig->IA_Sprint, ETriggerEvent::Completed, this, &ACSCharacter::Input_SprintStop);
	}
	if (InputConfig->IA_Crouch)
	{
		Input->BindAction(InputConfig->IA_Crouch, ETriggerEvent::Started, this, &ACSCharacter::Input_CrouchToggle);
		Input->BindAction(InputConfig->IA_Crouch, ETriggerEvent::Completed, this, &ACSCharacter::Input_CrouchRelease);
	}

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
