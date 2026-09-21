// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// ACSCharacter v1.0 view code: first-person weapon framing and aim down
// sights, left-hand targets, and smoothing of remote players. All cosmetic;
// nothing here touches gameplay state or what the authority traces against.

#include "Characters/CSCharacter.h"

#include "Animation/CSAnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMeshSocket.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Settings/CSSettingsSubsystem.h"
#include "Weapons/CSWeaponComponent.h"
#include "Weapons/CSWeaponPresentation.h"

namespace
{
	/** Frame-rate independent exponential approach factor. */
	float Approach(float Speed, float Dt)
	{
		return 1.f - FMath::Exp(-Speed * Dt);
	}

	/** Weapon transform in camera space that puts the model point Point at Pos with Rotation. */
	FTransform PlaceModel(const FCSWeaponModel& Model, const FVector& Point, const FRotator& Rotation, const FVector& Pos)
	{
		const FQuat Q(Rotation);
		return FTransform(Q, Pos - Q.RotateVector(Point * Model.Scale), FVector(Model.Scale));
	}

	FTransform BlendTransforms(const FTransform& A, const FTransform& B, float Alpha)
	{
		FTransform Out;
		Out.Blend(A, B, Alpha);
		return Out;
	}
}

const FCSWeaponModel* ACSCharacter::GetDisplayedModel() const
{
	return UCSWeaponPresentationSettings::Find(DisplayedWeapon.Get());
}

FVector ACSCharacter::GetAnimationVelocity() const
{
	return (bSmoothingInit && !IsLocallyControlled()) ? SmoothedVelocity : GetVelocity();
}

void ACSCharacter::UpdateHandTargets()
{
	const FCSWeaponModel* Model = GetDisplayedModel();
	for (USkeletalMeshComponent* MeshComp : { GetMesh(), FirstPersonMesh.Get() })
	{
		UCSAnimInstance* Anim = MeshComp ? Cast<UCSAnimInstance>(MeshComp->GetAnimInstance()) : nullptr;
		if (!Anim)
		{
			continue;
		}
		if (const USkeletalMeshSocket* LeftGrip = MeshComp->GetSocketByName(TEXT("HandGrip_L")))
		{
			Anim->SetLeftGripOffset(LeftGrip->GetSocketLocalTransform().GetLocation());
		}
		const USkeletalMeshSocket* Socket = MeshComp->GetSocketByName(WeaponSocket);
		// First person places the weapon by the camera and IKs both hands
		// (UpdateFirstPersonView); only the body uses the in-hand left-hand IK.
		if (!Model || !Socket || MeshComp == FirstPersonMesh)
		{
			Anim->SetLeftHandTarget(false, FVector::ZeroVector);
			continue;
		}
		// Support point: model -> hand socket -> hand_r bone (the socket's parent).
		const FVector InSocket = Model->ToHand(Model->Support);
		Anim->SetLeftHandTarget(true, Socket->GetSocketLocalTransform().TransformPosition(InSocket));
	}
}

void ACSCharacter::UpdateFirstPersonView(float DeltaSeconds)
{
	if (!IsLocalPlayerView() || !FirstPersonCamera || !FirstPersonMesh)
	{
		return;
	}

	const FCSWeaponModel* Model = GetDisplayedModel();
	UCSAnimInstance* Arms = GetArmsAnim();
	const bool bAlive = IsAliveAuthoritative();
	UStaticMeshComponent* WeaponComp = FirstPersonWeaponModel;
	const bool bHasModel = Model && WeaponComp && WeaponComp->GetStaticMesh();

	// --- Procedural equip / reload clocks -------------------------------------
	if (ViewEquipTime >= 0.f)
	{
		ViewEquipTime += DeltaSeconds;
		if (ViewEquipTime > 0.45f)
		{
			ViewEquipTime = -1.f;
		}
	}
	if (ViewReloadTime >= 0.f)
	{
		ViewReloadTime += DeltaSeconds;
		if (ViewReloadTime > ViewReloadDuration)
		{
			ViewReloadTime = -1.f;
		}
	}
	const bool bBusy = ViewEquipTime >= 0.f || ViewReloadTime >= 0.f;

	// --- Aim blend ---------------------------------------------------------
	const bool bWantAim = bAlive && !bBusy && WeaponComponent && WeaponComponent->IsAiming();
	AimAlpha = FMath::FInterpConstantTo(AimAlpha, bWantAim ? 1.f : 0.f, DeltaSeconds, 1.f / 0.16f);
	const float Ease = FMath::SmoothStep(0.f, 1.f, AimAlpha);

	float BaseFov = DefaultFieldOfView;
	if (const UCSSettingsSubsystem* Settings = UCSSettingsSubsystem::Get(this))
	{
		BaseFov = Settings->GetPreferences().FieldOfView;
	}
	const float AimFov = BaseFov * (Model ? Model->AimFovScale : 0.85f);
	FirstPersonCamera->SetFieldOfView(FMath::Lerp(BaseFov, AimFov, Ease));

	// A scope replaces the whole weapon view once it is up to the eye.
	bScopedView = Model && Model->bScope && AimAlpha > 0.92f;
	FirstPersonMesh->SetHiddenInGame(bScopedView, /*bPropagateToChildren*/ true);
	if (WeaponComp)
	{
		// The model hangs off the camera, not the arms: mirror their visibility.
		WeaponComp->SetVisibility(bHasModel && FirstPersonMesh->IsVisible());
		WeaponComp->SetHiddenInGame(bScopedView);
	}

	const FTransform HipDefault(FRotator(0.f, -90.f, 0.f), FirstPersonMeshOffset);
	if (!bHasModel)
	{
		// Skeletal fallback weapon in the hand: the clip's own framing.
		FirstPersonMesh->SetRelativeTransform(HipDefault);
		if (Arms)
		{
			Arms->SetFirstPersonHands(false, FTransform::Identity, FTransform::Identity, FVector::ZeroVector);
		}
		return;
	}

	// --- Procedural motion ---------------------------------------------------
	const float Hip01 = 1.f - 0.85f * Ease;
	const float Speed = GetVelocity().Size2D();
	const bool bGrounded = GetCharacterMovement() && GetCharacterMovement()->IsMovingOnGround();
	const float Move01 = bGrounded ? FMath::Clamp(Speed / 450.f, 0.f, 1.3f) : 0.f;
	BobPhase = FMath::Fmod(BobPhase + DeltaSeconds * (4.f + 6.5f * Move01), 2.f * PI);
	const FVector Bob = FVector(0.f, FMath::Sin(BobPhase) * 0.7f, -FMath::Abs(FMath::Cos(BobPhase)) * 0.6f) * Move01;

	const FVector2D SwayTarget(FMath::Clamp(LookSway.X * 0.35f, -2.5f, 2.5f), FMath::Clamp(LookSway.Y * 0.35f, -2.5f, 2.5f));
	LookSwayNow = FMath::Lerp(LookSwayNow, SwayTarget, Approach(10.f, DeltaSeconds));
	LookSway = FVector2D::ZeroVector;

	FireKick = FMath::FInterpTo(FireKick, 0.f, DeltaSeconds, 14.f);

	// Equip: rises from below, muzzle down. Reload: dips and rolls out, then back.
	FVector ActionOffset = FVector::ZeroVector;
	FRotator ActionRotation = FRotator::ZeroRotator;
	if (ViewEquipTime >= 0.f)
	{
		const float Down = 1.f - FMath::InterpEaseOut(0.f, 1.f, ViewEquipTime / 0.45f, 2.5f);
		ActionOffset = FVector(-4.f, 0.f, -22.f) * Down;
		ActionRotation = FRotator(-35.f, 0.f, 10.f) * Down;
	}
	else if (ViewReloadTime >= 0.f)
	{
		const float T = ViewReloadTime / ViewReloadDuration;
		// In over the first 25 %, held (with a tug at the middle), out over the last 25 %.
		const float Envelope = FMath::Clamp(FMath::Min(T / 0.25f, (1.f - T) / 0.25f), 0.f, 1.f);
		const float E = FMath::SmoothStep(0.f, 1.f, Envelope);
		const float Tug = FMath::Sin(FMath::Clamp((T - 0.4f) / 0.2f, 0.f, 1.f) * PI) * 1.5f;
		ActionOffset = FVector(-2.f, -3.f, -9.f - Tug) * E;
		ActionRotation = FRotator(-12.f, 8.f, 32.f) * E;
	}

	const FVector ProcOffset = (Bob + FVector(0.f, -LookSwayNow.X, -LookSwayNow.Y)) * Hip01
		+ FVector(-FireKick * FMath::Lerp(2.4f, 1.3f, Ease), 0.f, 0.f) + ActionOffset;
	const FRotator ProcRotation = FRotator(FireKick * FMath::Lerp(3.f, 1.f, Ease), 0.f, -LookSwayNow.X * 2.f * Hip01)
		+ ActionRotation;

	// --- Weapon in camera space ------------------------------------------------
	// Hip: placed by the grip, so the arms reach the same way for every model
	// whatever its length. Aimed: the sight on the view axis.
	const FVector HipGrip = Model->HipGrip.IsNearlyZero() ? FVector(22.f, 12.f, -22.f) : Model->HipGrip;
	const FTransform Hip = PlaceModel(*Model, Model->Grip, Model->HipRotation, HipGrip);
	const FTransform Ads = PlaceModel(*Model, Model->Sight, FRotator::ZeroRotator, FVector(Model->SightDistance, 0.f, 0.f));
	FTransform Weapon = BlendTransforms(Hip, Ads, Ease);
	Weapon.SetScale3D(FVector(Model->Scale));

	// Procedural rotation turns the weapon about its grip (the wrist), not
	// about the model origin.
	const FVector GripBefore = Weapon.TransformPosition(Model->Grip);
	Weapon.SetRotation(FQuat(ProcRotation) * Weapon.GetRotation());
	Weapon.AddToTranslation(GripBefore - Weapon.TransformPosition(Model->Grip) + ProcOffset);
	WeaponComp->SetRelativeTransform(Weapon);

	// --- Arms: fixed below and behind the view, hands by IK --------------------
	const FVector ArmsOffset = UCSWeaponPresentationSettings::Get()->ArmsOffset;
	FirstPersonMesh->SetRelativeLocationAndRotation(HipDefault.GetLocation() + ArmsOffset + Bob * 0.3f * Hip01,
		HipDefault.GetRotation());

	const USkeletalMeshSocket* Socket = FirstPersonMesh->GetSocketByName(WeaponSocket);
	if (Arms && Socket)
	{
		const FTransform MeshWorld = FirstPersonMesh->GetComponentTransform();
		const FTransform ModelWorld = WeaponComp->GetComponentTransform();
		// Where the HandGrip_R socket must be: the model's grip frame
		// (model = MeshInHand * socket  =>  socket = MeshInHand^-1 * model).
		const FTransform GripWorld = Model->GetMeshInHand().Inverse() * ModelWorld;
		const FTransform GripCS = GripWorld.GetRelativeTransform(MeshWorld);
		const FVector SupportCS = MeshWorld.InverseTransformPosition(ModelWorld.TransformPosition(Model->Support));
		Arms->SetFirstPersonHands(bAlive, Socket->GetSocketLocalTransform(), GripCS, SupportCS);
	}
}

void ACSCharacter::UpdateRemoteSmoothing(float DeltaSeconds)
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!Body || IsLocallyControlled() || DeltaSeconds <= 0.f)
	{
		return;
	}

	const FVector Actor = GetActorLocation();
	const float Yaw = GetActorRotation().Yaw;
	if (!bSmoothingInit)
	{
		SmoothedLocation = Actor;
		LastReplicatedLocation = Actor;
		SmoothedVelocity = FVector::ZeroVector;
		SmoothedYaw = Yaw;
		BodyMeshBaseLocation = Body->GetRelativeLocation();
		BodyMeshBaseRotation = Body->GetRelativeRotation();
		bSmoothingInit = true;
		return;
	}

	// Velocity from the replicated steps. Updates do not arrive every frame
	// (the step alternates between zero and a jump), so it is low-passed.
	const FVector Step = Actor - LastReplicatedLocation;
	LastReplicatedLocation = Actor;
	if (Step.Size() > 400.f)
	{
		// Respawn or teleport: snap, do not glide across the map.
		SmoothedLocation = Actor;
		SmoothedVelocity = FVector::ZeroVector;
		SmoothedYaw = Yaw;
	}
	else
	{
		SmoothedVelocity = FMath::Lerp(SmoothedVelocity, Step / DeltaSeconds, Approach(7.f, DeltaSeconds));
		// Predict with that velocity, then pull towards the latest truth.
		SmoothedLocation += SmoothedVelocity * DeltaSeconds;
		SmoothedLocation = FMath::Lerp(SmoothedLocation, Actor, Approach(12.f, DeltaSeconds));
		const FVector Error = SmoothedLocation - Actor;
		if (Error.Size() > 90.f)
		{
			SmoothedLocation = Actor + Error.GetClampedToMaxSize(90.f);
		}
		SmoothedYaw += FMath::FindDeltaAngleDegrees(SmoothedYaw, Yaw) * Approach(16.f, DeltaSeconds);
	}

	const FVector Offset = GetActorRotation().UnrotateVector(SmoothedLocation - Actor);
	const float YawOffset = FMath::FindDeltaAngleDegrees(Yaw, SmoothedYaw);
	Body->SetRelativeLocationAndRotation(BodyMeshBaseLocation + Offset,
		BodyMeshBaseRotation + FRotator(0.f, YawOffset, 0.f));
}
