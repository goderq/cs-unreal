// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// ACSCharacter v1.0 view code: first-person weapon framing and aim down
// sights, left-hand targets, and smoothing of remote players. All cosmetic;
// nothing here touches gameplay state or what the authority traces against.

#include "Characters/CSCharacter.h"

#include "Animation/CSAnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Characters/CSCharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMeshSocket.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Settings/CSSettingsSubsystem.h"
#include "Weapons/CSWeaponComponent.h"
#include "Weapons/CSWeaponPresentation.h"

namespace
{
	/** Length of the first-person weapon inspect, seconds. */
	constexpr float InspectSeconds = 2.6f;

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
		// One-handed things (knife, grenades) leave the left hand to the clip.
		if (!Model || !Socket || MeshComp == FirstPersonMesh || Model->bOneHanded)
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
	if (ViewMeleeTime >= 0.f)
	{
		ViewMeleeTime += DeltaSeconds;
		if (ViewMeleeTime > (bViewMeleeHeavy ? 1.0f : 0.45f))
		{
			ViewMeleeTime = -1.f;
		}
	}
	if (ViewThrowTime >= 0.f)
	{
		ViewThrowTime += DeltaSeconds;
		if (ViewThrowTime > 0.7f)
		{
			ViewThrowTime = -1.f;
		}
	}
	const bool bBusy = ViewEquipTime >= 0.f || ViewReloadTime >= 0.f;

	// --- Aim blend ---------------------------------------------------------
	const bool bWantAim = bAlive && !bBusy && WeaponComponent && WeaponComponent->IsAiming();
	// Heavier weapons come up to the eye more slowly (UCSWeaponDefinition::AimSeconds).
	const UCSWeaponDefinition* ViewWeapon = DisplayedWeapon.Get();
	const float AimSeconds = ViewWeapon ? FMath::Max(ViewWeapon->AimSeconds, 0.05f) : 0.16f;
	AimAlpha = FMath::FInterpConstantTo(AimAlpha, bWantAim ? 1.f : 0.f, DeltaSeconds, 1.f / AimSeconds);

	// Inspect runs only while the hands are otherwise idle; anything else ends it.
	if (ViewInspectTime >= 0.f)
	{
		ViewInspectTime += DeltaSeconds;
		const bool bInterrupted = !bAlive || bBusy || bWantAim || ViewMeleeTime >= 0.f || ViewThrowTime >= 0.f || FireKick > 0.3f;
		if (bInterrupted || ViewInspectTime > InspectSeconds)
		{
			ViewInspectTime = -1.f;
		}
	}
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
		// A thrown grenade leaves the hand at the release point.
		WeaponComp->SetHiddenInGame(bScopedView || ViewThrowTime > 0.3f);
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

	// Breathing: a slow rise and fall while standing still, much less aimed.
	BreathPhase = FMath::Fmod(BreathPhase + DeltaSeconds * 1.7f, 2.f * PI);
	const float Still01 = 1.f - FMath::Clamp(Move01, 0.f, 1.f);
	const float Breath = FMath::Sin(BreathPhase) * Still01 * FMath::Lerp(1.f, 0.3f, Ease);

	// Strafing leans the weapon into the turn of the body.
	const float Lateral = FVector::DotProduct(GetVelocity(), GetActorRightVector()) / 450.f;
	StrafeTilt = FMath::FInterpTo(StrafeTilt, FMath::Clamp(Lateral, -1.f, 1.f), DeltaSeconds, 8.f);

	// In the air the weapon trails the jump; on landing it dips and settles.
	const float VerticalSpeed = GetVelocity().Z;
	AirLag = FMath::FInterpTo(AirLag, bGrounded ? 0.f : FMath::Clamp(-VerticalSpeed / 700.f, -1.f, 1.f), DeltaSeconds, 10.f);
	if (bViewWasFalling && bGrounded)
	{
		WeaponLandDip = FMath::Clamp(ViewFallSpeed / 800.f, 0.3f, 1.f);
	}
	bViewWasFalling = !bGrounded;
	ViewFallSpeed = bGrounded ? 0.f : FMath::Max(ViewFallSpeed, -VerticalSpeed);
	WeaponLandDip = FMath::FInterpTo(WeaponLandDip, 0.f, DeltaSeconds, 7.f);

	// Equip: rises from below, muzzle down. Reload: dips and rolls out, then back.
	FVector ActionOffset = FVector::ZeroVector;
	FRotator ActionRotation = FRotator::ZeroRotator;
	if (ViewMeleeTime >= 0.f)
	{
		const float T = ViewMeleeTime;
		if (bViewMeleeHeavy)
		{
			// Stab: draw back, drive straight forward, hold, recover.
			const float Back = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(T / 0.18f, 0.f, 1.f));
			const float Drive = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((T - 0.18f) / 0.12f, 0.f, 1.f));
			const float Out = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((T - 0.55f) / 0.4f, 0.f, 1.f));
			ActionOffset = (FVector(-9.f, 2.f, 3.f) * Back + FVector(34.f, -4.f, -2.f) * Drive) * (1.f - Out);
			ActionRotation = (FRotator(12.f, 0.f, 0.f) * Back + FRotator(-10.f, 6.f, -20.f) * Drive) * (1.f - Out);
		}
		else
		{
			// Slash: cock to the right, sweep across to the left, return.
			const float Cock = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(T / 0.08f, 0.f, 1.f));
			const float Sweep = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((T - 0.08f) / 0.14f, 0.f, 1.f));
			const float Out = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((T - 0.24f) / 0.2f, 0.f, 1.f));
			const FVector From(4.f, 12.f, 6.f);
			const FVector To(16.f, -22.f, -6.f);
			ActionOffset = FMath::Lerp(From * Cock, To, Sweep) * (1.f - Out);
			ActionRotation = FMath::Lerp(FRotator(10.f, 30.f, 40.f) * Cock, FRotator(-6.f, -55.f, -35.f), Sweep) * (1.f - Out);
		}
	}
	else if (ViewThrowTime >= 0.f)
	{
		// Overarm throw: pull back and up, whip forward, hand drops away.
		const float T = ViewThrowTime;
		if (T < 0.22f)
		{
			const float A = FMath::SmoothStep(0.f, 1.f, T / 0.22f);
			ActionOffset = FVector(-8.f, 5.f, 12.f) * A;
			ActionRotation = FRotator(35.f, 0.f, 12.f) * A;
		}
		else if (T < 0.36f)
		{
			const float A = FMath::SmoothStep(0.f, 1.f, (T - 0.22f) / 0.14f);
			ActionOffset = FMath::Lerp(FVector(-8.f, 5.f, 12.f), FVector(28.f, -6.f, 4.f), A);
			ActionRotation = FMath::Lerp(FRotator(35.f, 0.f, 12.f), FRotator(-35.f, 0.f, -8.f), A);
		}
		else
		{
			const float A = FMath::SmoothStep(0.f, 1.f, (T - 0.36f) / 0.34f);
			ActionOffset = FMath::Lerp(FVector(28.f, -6.f, 4.f), FVector(0.f, 0.f, -30.f), A);
			ActionRotation = FMath::Lerp(FRotator(-35.f, 0.f, -8.f), FRotator(-20.f, 0.f, 0.f), A);
		}
	}
	else 	if (ViewEquipTime >= 0.f)
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
	else if (ViewInspectTime >= 0.f)
	{
		// Inspect: bring it in and turn the right side to the eye, then roll
		// over to show the top and left, then back into the hands.
		const float T = ViewInspectTime;
		const float In = FMath::SmoothStep(0.f, 0.45f, T);
		const float Out = FMath::SmoothStep(InspectSeconds - 0.45f, InspectSeconds, T);
		const float Turn = FMath::SmoothStep(1.1f, 1.7f, T);
		const float Drift = FMath::Sin(T * 2.3f) * 0.6f;
		const float Envelope = In * (1.f - Out);
		ActionOffset = FMath::Lerp(FVector(-5.f, -7.f, 4.f), FVector(-3.f, -5.f, 6.f), Turn) * Envelope + FVector(0.f, 0.f, Drift) * Envelope;
		ActionRotation = FMath::Lerp(FRotator(6.f, 28.f, -58.f), FRotator(22.f, -26.f, 38.f), Turn) * Envelope;
	}

	// Camera motion setting (C11): breathing, strafe tilt, air lag and the
	// landing dip scale down to nothing; fire kick and sway follow the mouse
	// and the weapon, so they stay.
	const float Motion = CameraMotionScale();
	const float BreathM = Breath * Motion;
	const float TiltM = StrafeTilt * Motion;
	const float LagM = AirLag * Motion;
	const float DipM = WeaponLandDip * Motion;
	const FVector ProcOffset = (Bob * Motion + FVector(0.f, -LookSwayNow.X - TiltM * 0.6f, -LookSwayNow.Y + BreathM * 0.25f + LagM * 1.8f)) * Hip01
		+ FVector(-FireKick * FMath::Lerp(2.4f, 1.3f, Ease), 0.f, -DipM * FMath::Lerp(2.2f, 0.8f, Ease)) + ActionOffset;
	const FRotator ProcRotation = FRotator(
			FireKick * FMath::Lerp(3.f, 1.f, Ease) + BreathM * 0.2f + LagM * 2.f * Hip01 - DipM * FMath::Lerp(3.f, 1.f, Ease),
			0.f,
			(-LookSwayNow.X * 2.f - TiltM * 4.f) * Hip01 - TiltM * 0.8f * Ease)
		+ ActionRotation;

	// --- Weapon in camera space ------------------------------------------------
	// Hip: placed by the grip, so the arms reach the same way for every model
	// whatever its length. Aimed: the sight on the view axis.
	const FVector HipGrip = Model->HipGrip.IsNearlyZero() ? FVector(22.f, 12.f, -22.f) : Model->HipGrip;
	const FTransform Hip = PlaceModel(*Model, Model->Grip, Model->HipRotation, HipGrip);
	// v1.1: never closer than would put the grip - and the hand on it - into
	// the face. Long rifles with the rear sight far ahead of the grip (AK-47)
	// used to shove hand and receiver right up to the eye.
	const float GripBehindSight = (Model->Sight.X - Model->Grip.X) * Model->Scale;
	const float AdsDistance = FMath::Max(Model->SightDistance, GripBehindSight + MinAdsGripDistance);
	const FTransform Ads = PlaceModel(*Model, Model->Sight, FRotator::ZeroRotator, FVector(AdsDistance, 0.f, 0.f));
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
		// One-handed (knife, grenades): the left hand rests low and to the side,
		// below the frame, instead of reaching for a forend that is not there.
		const FVector RestCamera(12.f, -26.f, -58.f);
		const FVector SupportWorld = Model->bOneHanded
			? FirstPersonCamera->GetComponentTransform().TransformPosition(RestCamera)
			: ModelWorld.TransformPosition(Model->Support);
		const FVector SupportCS = MeshWorld.InverseTransformPosition(SupportWorld);
		// The model's extra left-hand turn, from its own frame into component space.
		const FQuat ModelCS = ModelWorld.GetRelativeTransform(MeshWorld).GetRotation();
		const FQuat SupportTurnCS = Model->bOneHanded ? FQuat::Identity
			: ModelCS * FQuat(Model->SupportRotation) * ModelCS.Inverse();
		Arms->SetFirstPersonHands(bAlive, Socket->GetSocketLocalTransform(), GripCS, SupportCS, SupportTurnCS);
	}
}

void ACSCharacter::UpdateRemoteSmoothing(float DeltaSeconds)
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!Body || IsLocallyControlled() || DeltaSeconds <= 0.f || bRagdoll)
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
	// The base is re-read every frame: crouching moves it (ACharacter::OnStartCrouch).
	Body->SetRelativeLocationAndRotation(GetBaseTranslationOffset() + Offset,
		GetBaseRotationOffset().Rotator() + FRotator(0.f, YawOffset, 0.f));
}

// ---------------------------------------------------------------------------
// v1.1 movement feel: crouch camera, landing dip, jump cooldown
// ---------------------------------------------------------------------------

void ACSCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	// The capsule centre just dropped: keep the eyes where they were and let
	// UpdateCameraHeight glide them down, instead of a one-frame snap.
	CameraZ += ScaledHalfHeightAdjust;
}

void ACSCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	CameraZ -= ScaledHalfHeightAdjust;
}

void ACSCharacter::Landed(const FHitResult& Hit)
{
	// Velocity still carries the fall here (the movement component flattens it after).
	const float Impact = FMath::Max(0.f, -static_cast<float>(GetVelocity().Z));
	Super::Landed(Hit);

	LastLandingImpact = Impact;
	LastLandedTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	// Kick the camera spring down; a long fall dips deeper.
	LandDipVelocity -= FMath::Clamp(Impact * 0.09f, 15.f, 140.f);
}

bool ACSCharacter::CanJumpInternal_Implementation() const
{
	// No bunny hopping: a short pause after every landing.
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	return Super::CanJumpInternal_Implementation() && Now - LastLandedTime > 0.22;
}

void ACSCharacter::UpdateCameraHeight(float DeltaSeconds)
{
	if (!FirstPersonCamera || DeltaSeconds <= 0.f)
	{
		return;
	}
	const UCSCharacterMovementComponent* Move = GetCSMovement();
	const float Target = (Move && Move->IsCrouching()) ? CrouchedCameraHeight : CameraHeight;
	CameraZ = FMath::FInterpTo(CameraZ, Target, DeltaSeconds, 11.f);

	// Damped spring for the landing dip.
	constexpr float Stiffness = 170.f;
	constexpr float Damping = 20.f;
	LandDipVelocity += (-Stiffness * LandDip - Damping * LandDipVelocity) * DeltaSeconds;
	LandDip = FMath::Clamp(LandDip + LandDipVelocity * DeltaSeconds, -12.f, 4.f);

	// Blast shake: a quick jitter of the eye point that dies away.
	ExplosionShake = FMath::FInterpConstantTo(ExplosionShake, 0.f, DeltaSeconds, 1.6f);
	const float ShakeAmp = ExplosionShake * ExplosionShake * 4.f;
	const double Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const FVector Shake(0.f, FMath::Sin(Time * 71.0) * ShakeAmp, FMath::Sin(Time * 57.0 + 1.3) * ShakeAmp);

	const float Motion = CameraMotionScale();
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, CameraZ + LandDip * Motion) + Shake * Motion);
}

float ACSCharacter::CameraMotionScale() const
{
	if (!IsLocallyControlled() || IsBot())
	{
		return 1.f;
	}
	const UCSSettingsSubsystem* Settings = UCSSettingsSubsystem::Get(this);
	return Settings ? Settings->GetPreferences().CameraMotion : 1.f;
}

void ACSCharacter::StartInspect()
{
	const bool bAiming = WeaponComponent && WeaponComponent->IsAiming();
	const bool bBusy = ViewEquipTime >= 0.f || ViewReloadTime >= 0.f || ViewMeleeTime >= 0.f || ViewThrowTime >= 0.f;
	if (!IsLocalPlayerView() || !IsAliveAuthoritative() || bAiming || bBusy || ViewInspectTime >= 0.f)
	{
		return;
	}
	ViewInspectTime = 0.f;
}
