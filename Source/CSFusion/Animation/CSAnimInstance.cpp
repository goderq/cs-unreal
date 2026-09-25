// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Animation/CSAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "Animation/CSAnimationSettings.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "Characters/CSCharacter.h"
#include "TwoBoneIK.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	/** Seconds for upper-body actions to blend in / out. */
	constexpr float GUpperBlendIn = 0.12f;
	constexpr float GUpperBlendOut = 0.2f;
	constexpr float GDeathBlendIn = 0.12f;

	/** Bone the upper-body layer starts at (it and every child). */
	const FName GUpperBodyRoot(TEXT("spine_01"));

	UAnimSequence* LoadClip(const TSoftObjectPtr<UAnimSequence>& Ptr)
	{
		return Ptr.IsNull() ? nullptr : Ptr.LoadSynchronous();
	}

	float ClipLength(const UAnimSequence* Seq)
	{
		return Seq ? FMath::Max(0.01f, Seq->GetPlayLength()) : 1.f;
	}

	void SamplePose(const UAnimSequence* Seq, float Time, bool bLoop, FPoseContext& Out)
	{
		if (!Seq)
		{
			if (Out.ExpectsAdditivePose())
			{
				Out.ResetToAdditiveIdentity();
			}
			else
			{
				Out.ResetToRefPose();
			}
			return;
		}

		const float Len = ClipLength(Seq);
		float T = bLoop ? FMath::Fmod(Time, Len) : FMath::Clamp(Time, 0.f, Len);
		if (T < 0.f)
		{
			T += Len;
		}

		FAnimationPoseData Data(Out);
		Seq->GetAnimationPose(Data, FAnimExtractContext(static_cast<double>(T), /*bExtractRootMotion*/ false));
	}

	void Blend(FPoseContext& A, FPoseContext& B, float WeightOfA, FPoseContext& Out)
	{
		FAnimationPoseData DA(A);
		FAnimationPoseData DB(B);
		FAnimationPoseData DOut(Out);
		FAnimationRuntime::BlendTwoPosesTogether(DA, DB, WeightOfA, DOut);
	}

	void ApplyAdditive(FPoseContext& Base, const UAnimSequence* Seq, float Time, float Weight, FAnimInstanceProxy* Proxy)
	{
		if (!Seq || Weight <= KINDA_SMALL_NUMBER)
		{
			return;
		}
		FPoseContext Additive(Proxy, /*bInExpectsAdditivePose*/ true);
		SamplePose(Seq, Time, /*bLoop*/ false, Additive);

		FAnimationPoseData BaseData(Base);
		const FAnimationPoseData AddData(Additive);
		FAnimationRuntime::AccumulateAdditivePose(BaseData, AddData, Weight, Seq->GetAdditiveAnimType());
		Base.Pose.NormalizeRotations();
	}
}

// ---------------------------------------------------------------------------
// Game thread
// ---------------------------------------------------------------------------

void UCSAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	ResolveClips();
}

void UCSAnimInstance::ResolveClips()
{
	const UCSAnimationSettings* Settings = UCSAnimationSettings::Get();
	const FCSStanceAnimSet& Set = Settings->GetStance(Stance);

	IdleClip = LoadClip(Set.Idle);
	WalkClips.Reset();
	JogClips.Reset();
	for (int32 i = 0; i < 8; ++i)
	{
		WalkClips.Add(Set.Walk.IsValidIndex(i) ? LoadClip(Set.Walk[i]) : nullptr);
		JogClips.Add(Set.Jog.IsValidIndex(i) ? LoadClip(Set.Jog[i]) : nullptr);
	}
	FallClip = LoadClip(Set.FallLoop);
	JumpStartClip = LoadClip(Set.JumpStart);
	LandClip = LoadClip(Set.LandRecovery);
	AimUpClip = LoadClip(Set.AimUp);
	AimDownClip = LoadClip(Set.AimDown);
	FireClip = LoadClip(Set.Fire);
	ReloadClip = LoadClip(Set.Reload);
	EquipClip = LoadClip(Set.Equip);
	DryFireClip = LoadClip(Set.DryFire);
	WalkClipSpeed = FMath::Max(1.f, Set.WalkClipSpeed);
	JogClipSpeed = FMath::Max(WalkClipSpeed + 1.f, Set.JogClipSpeed);

	HitFrontClip = LoadClip(Settings->HitReactFront);
	HitBackClip = LoadClip(Settings->HitReactBack);
	DeathClips.Reset();
	for (const TSoftObjectPtr<UAnimSequence>& Death : Settings->Death)
	{
		DeathClips.Add(LoadClip(Death));
	}

	bClipsResolved = true;
}

void UCSAnimInstance::SetStance(ECSWeaponStance NewStance, bool bInstant)
{
	if (NewStance == Stance && bClipsResolved)
	{
		if (!bInstant)
		{
			PlayEquip();
		}
		return;
	}
	Stance = NewStance;
	ResolveClips();
	if (!bInstant)
	{
		PlayEquip();
	}
}

void UCSAnimInstance::PlayFire()
{
	FireTime = 0.f;
}

void UCSAnimInstance::PlayReload(float DurationSeconds)
{
	if (!ReloadClip)
	{
		return;
	}
	ActiveUpperClip = ReloadClip;
	UpperTime = 0.f;
	// Stretch the clip to the weapon's real reload time so hands and ammo agree.
	UpperRate = DurationSeconds > 0.05f ? ClipLength(ReloadClip) / DurationSeconds : 1.f;
}

void UCSAnimInstance::PlayEquip()
{
	if (!EquipClip)
	{
		return;
	}
	ActiveUpperClip = EquipClip;
	UpperTime = 0.f;
	// The full equip clip is long; play it quickly so switching feels snappy.
	UpperRate = ClipLength(EquipClip) / 0.6f;
}

void UCSAnimInstance::PlayDryFire()
{
	if (!DryFireClip || IsPlayingUpperBody())
	{
		return;
	}
	ActiveUpperClip = DryFireClip;
	UpperTime = 0.f;
	UpperRate = 1.5f;
}

void UCSAnimInstance::CancelUpperBody()
{
	UpperTime = -1.f;
	ActiveUpperClip = nullptr;
}

void UCSAnimInstance::PlayHitReact(bool bFromFront)
{
	ActiveHitClip = bFromFront ? HitFrontClip : HitBackClip;
	HitTime = ActiveHitClip ? 0.f : -1.f;
}

void UCSAnimInstance::PlayDeath(int32 Direction)
{
	if (DeathClips.Num() == 0)
	{
		return;
	}
	ActiveDeathClip = DeathClips[FMath::Clamp(Direction, 0, DeathClips.Num() - 1)];
	DeathTime = 0.f;
	DeathAlpha = 0.f;
	CancelUpperBody();
	FireTime = -1.f;
	HitTime = -1.f;
}

void UCSAnimInstance::ResetAlive()
{
	DeathTime = -1.f;
	DeathAlpha = 0.f;
	ActiveDeathClip = nullptr;
}

void UCSAnimInstance::PreUpdateAnimation(float DeltaSeconds)
{
	// Advance everything BEFORE the base class copies state into the proxy
	// (FAnimInstanceProxy::PreUpdate runs inside Super), so the proxy always
	// evaluates this frame's values.
	AdvanceGameThread(DeltaSeconds);
	Super::PreUpdateAnimation(DeltaSeconds);
}

void UCSAnimInstance::AdvanceGameThread(float Dt)
{
	if (!bClipsResolved)
	{
		ResolveClips();
	}

	const ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner());
	bool bFalling = false;
	float VerticalSpeed = 0.f;
	bool bCrouching = false;
	if (Character)
	{
		// Remote players report the smoothed velocity (see ACSCharacter::
		// UpdateRemoteSmoothing); the raw replicated one arrives in steps.
		const ACSCharacter* CSChar = Cast<ACSCharacter>(Character);
		FVector Velocity = CSChar ? CSChar->GetAnimationVelocity() : Character->GetVelocity();
		Velocity.Z = 0.f;
		const float RawSpeed = Velocity.Size();
		// Speed and direction glide instead of snapping, so a network hiccup or
		// a quick strafe change never pops the legs between clips.
		GroundSpeed = FMath::FInterpTo(GroundSpeed, RawSpeed, Dt, 10.f);
		if (RawSpeed > 5.f)
		{
			const FVector Local = Character->GetActorRotation().UnrotateVector(Velocity);
			// Atan2(y, x): 0 = forward, +90 = right in UE's left-handed frame.
			const float Target = FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X)) + 360.f, 360.f);
			if (GroundSpeed < 30.f)
			{
				DirectionDeg = Target; // starting from standstill: no sweep through other directions
			}
			else
			{
				const float Diff = FMath::FindDeltaAngleDegrees(DirectionDeg, Target);
				const float Step = FMath::Clamp(Diff, -540.f * Dt, 540.f * Dt);
				DirectionDeg = FMath::Fmod(DirectionDeg + Step + 360.f, 360.f);
			}
		}
		AimPitch = FRotator::NormalizeAxis(Character->GetBaseAimRotation().Pitch);
		// Stance is our own replicated property, so remote players crouch too.
		bCrouching = CSChar ? CSChar->GetStance() == ECSStanceState::Crouching : Character->bIsCrouched;
		if (const UCharacterMovementComponent* Move = Character->GetCharacterMovement())
		{
			bFalling = Move->IsFalling();
		}
	}

	// Locomotion phase: one normalized phase shared by all walk/jog clips (the
	// Mannequin clips are authored with matching foot timing), advancing at a
	// rate that makes the feet match the actual ground speed.
	const float WalkLen = ClipLength(WalkClips.Num() ? WalkClips[0].Get() : nullptr);
	const float JogLen = ClipLength(JogClips.Num() ? JogClips[0].Get() : nullptr);
	const float JogBlend = FMath::Clamp((GroundSpeed - WalkClipSpeed) / (JogClipSpeed - WalkClipSpeed), 0.f, 1.f);
	const float CycleLen = FMath::Lerp(WalkLen, JogLen, JogBlend);
	const float RefSpeed = FMath::Lerp(WalkClipSpeed, JogClipSpeed, JogBlend);
	const float Rate = GroundSpeed > 5.f ? FMath::Clamp(GroundSpeed / RefSpeed, 0.5f, 2.f) : 1.f;
	LocoPhase = FMath::Fmod(LocoPhase + Dt * Rate / CycleLen, 1.f);
	IdleTime += Dt;

	FallAlpha = FMath::FInterpConstantTo(FallAlpha, bFalling ? 1.f : 0.f, Dt, 6.f);

	// v1.1 jumps: take-off clip when leaving the ground upwards, a knee bend on
	// landing that is deeper the longer the fall was.
	if (bFalling && !bWasFalling && VerticalSpeed > 150.f)
	{
		JumpStartTime = 0.f;
	}
	if (!bFalling && bWasFalling && FallTime > 0.2f)
	{
		LandTime = 0.f;
		LandWeight = FMath::Clamp(FallTime / 0.8f, 0.35f, 1.f);
		JumpStartTime = -1.f;
	}
	bWasFalling = bFalling;
	if (JumpStartTime >= 0.f)
	{
		JumpStartTime += Dt;
		if (!JumpStartClip || JumpStartTime > ClipLength(JumpStartClip))
		{
			JumpStartTime = -1.f;
		}
	}
	if (LandTime >= 0.f)
	{
		LandTime += Dt;
		if (!LandClip || LandTime > ClipLength(LandClip))
		{
			LandTime = -1.f;
		}
	}
	CrouchAlpha = FMath::FInterpTo(CrouchAlpha, bCrouching && !bFirstPerson ? 1.f : 0.f, Dt, 9.f);
	if (ThrowTime >= 0.f)
	{
		ThrowTime += Dt;
		if (ThrowTime > 0.7f)
		{
			ThrowTime = -1.f;
		}
	}
	if (MeleeTime >= 0.f)
	{
		MeleeTime += Dt;
		if (MeleeTime > (bMeleeHeavy ? 1.0f : 0.45f))
		{
			MeleeTime = -1.f;
		}
	}

	const bool bWantIK = bLeftHandIK && UpperTime < 0.f && DeathTime < 0.f;
	LeftHandIKAlpha = FMath::FInterpConstantTo(LeftHandIKAlpha, bWantIK ? 1.f : 0.f, Dt, 5.f);
	FallTime = bFalling ? FallTime + Dt : 0.f;

	if (FireTime >= 0.f)
	{
		FireTime += Dt;
		if (FireTime > ClipLength(FireClip))
		{
			FireTime = -1.f;
		}
	}
	if (UpperTime >= 0.f)
	{
		UpperTime += Dt * UpperRate;
		if (!ActiveUpperClip || UpperTime > ClipLength(ActiveUpperClip))
		{
			CancelUpperBody();
		}
	}
	if (HitTime >= 0.f)
	{
		HitTime += Dt;
		if (!ActiveHitClip || HitTime > ClipLength(ActiveHitClip))
		{
			HitTime = -1.f;
		}
	}
	if (DeathTime >= 0.f)
	{
		DeathTime += Dt;
		DeathAlpha = FMath::Min(1.f, DeathAlpha + Dt / GDeathBlendIn);
	}
}

FAnimInstanceProxy* UCSAnimInstance::CreateAnimInstanceProxy()
{
	return new FCSAnimInstanceProxy(this);
}

void UCSAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete InProxy;
}

// ---------------------------------------------------------------------------
// Proxy
// ---------------------------------------------------------------------------

void FCSAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	const UCSAnimInstance* I = CastChecked<UCSAnimInstance>(InAnimInstance);
	FCSAnimSnapshot& S = Snapshot;

	S.Idle = I->IdleClip;
	for (int32 i = 0; i < 8; ++i)
	{
		S.Walk[i] = I->WalkClips.IsValidIndex(i) ? I->WalkClips[i].Get() : nullptr;
		S.Jog[i] = I->JogClips.IsValidIndex(i) ? I->JogClips[i].Get() : nullptr;
	}
	S.Fall = I->FallClip;
	S.AimUp = I->AimUpClip;
	S.AimDown = I->AimDownClip;
	S.Fire = I->FireClip;
	S.Upper = I->ActiveUpperClip;
	S.HitReact = I->ActiveHitClip;
	S.Death = I->ActiveDeathClip;
	S.JumpStart = I->JumpStartClip;
	S.LandRecovery = I->LandClip;
	S.JumpStartTime = I->JumpStartTime;
	S.LandTime = I->LandTime;
	S.LandWeight = I->LandWeight;
	S.CrouchAlpha = I->CrouchAlpha;
	S.ThrowTime = I->bFirstPerson ? -1.f : I->ThrowTime;
	S.MeleeTime = I->bFirstPerson ? -1.f : I->MeleeTime;
	S.bMeleeHeavy = I->bMeleeHeavy;

	S.IdleTime = I->IdleTime;
	S.LocoPhase = I->LocoPhase;
	S.DirectionDeg = I->DirectionDeg;
	S.WalkAlpha = FMath::Clamp(I->GroundSpeed / I->WalkClipSpeed, 0.f, 1.f);
	S.JogAlpha = FMath::Clamp((I->GroundSpeed - I->WalkClipSpeed) / (I->JogClipSpeed - I->WalkClipSpeed), 0.f, 1.f);
	if (I->GroundSpeed < 5.f)
	{
		S.WalkAlpha = 0.f;
		S.JogAlpha = 0.f;
	}
	S.FallAlpha = I->FallAlpha;
	S.FallTime = I->FallTime;
	S.AimPitch = FMath::Clamp(I->AimPitch, -80.f, 80.f);
	S.bApplyAim = !I->bFirstPerson;
	// The Mannequin reload / equip clips are third-person moves that swing the
	// gun down out of a camera-attached view; in first person only part of the
	// motion is applied, enough to read as a reload without losing the weapon.
	S.UpperWeightScale = I->bFirstPerson ? 0.35f : 1.f;

	S.FireTime = I->FireTime;
	S.UpperTime = I->UpperTime;
	S.UpperAlpha = 0.f;
	if (I->UpperTime >= 0.f && I->ActiveUpperClip)
	{
		const float Len = ClipLength(I->ActiveUpperClip);
		S.UpperAlpha = FMath::Clamp(FMath::Min(I->UpperTime / (GUpperBlendIn * I->UpperRate),
			(Len - I->UpperTime) / (GUpperBlendOut * I->UpperRate)), 0.f, 1.f);
	}
	S.HitTime = I->HitTime;
	S.DeathTime = I->DeathTime;
	S.DeathAlpha = I->DeathAlpha;
	S.LeftHandIKAlpha = I->LeftHandIKAlpha;
	S.LeftHandTargetInHandR = I->LeftHandTargetInHandR;
	S.bTwoHandIK = I->bTwoHandIK && I->DeathTime < 0.f;
	S.GripSocketLocal = I->GripSocketLocal;
	S.GripFrameCS = I->GripFrameCS;
	S.SupportCS = I->SupportCS;
	S.LeftGripOffset = I->LeftGripOffset;
}

void FCSAnimInstanceProxy::SampleDirectional(const UAnimSequence* const Clips[8], FPoseContext& Out) const
{
	const float Angle = Snapshot.DirectionDeg;
	const int32 Index = FMath::FloorToInt(Angle / 45.f) % 8;
	const int32 Next = (Index + 1) % 8;
	const float Alpha = (Angle - Index * 45.f) / 45.f;

	FPoseContext A(Out);
	FPoseContext B(Out);
	SamplePose(Clips[Index], Snapshot.LocoPhase * ClipLength(Clips[Index]), true, A);
	SamplePose(Clips[Next], Snapshot.LocoPhase * ClipLength(Clips[Next]), true, B);
	Blend(A, B, 1.f - Alpha, Out);
}

bool FCSAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	const FCSAnimSnapshot& S = Snapshot;
	FAnimInstanceProxy* Self = this;

	// 1. Locomotion
	FPoseContext Loco(Output);
	SamplePose(S.Idle, S.IdleTime, true, Loco);
	if (S.WalkAlpha > 0.f)
	{
		FPoseContext Walk(Output);
		SampleDirectional(S.Walk, Walk);
		if (S.JogAlpha > 0.f)
		{
			FPoseContext Jog(Output);
			SampleDirectional(S.Jog, Jog);
			FPoseContext Moving(Output);
			Blend(Walk, Jog, 1.f - S.JogAlpha, Moving);
			Walk.Pose.CopyBonesFrom(Moving.Pose);
			Walk.Curve.CopyFrom(Moving.Curve);
		}
		FPoseContext Mixed(Output);
		Blend(Loco, Walk, 1.f - S.WalkAlpha, Mixed);
		Loco.Pose.CopyBonesFrom(Mixed.Pose);
		Loco.Curve.CopyFrom(Mixed.Curve);
	}

	// 2. Falling
	if (S.FallAlpha > 0.f && S.Fall)
	{
		FPoseContext Fall(Output);
		// v1.1: the take-off clip first, then the loop.
		if (S.JumpStartTime >= 0.f && S.JumpStart)
		{
			SamplePose(S.JumpStart, S.JumpStartTime, false, Fall);
		}
		else
		{
			SamplePose(S.Fall, S.FallTime, true, Fall);
		}
		FPoseContext Mixed(Output);
		Blend(Loco, Fall, 1.f - S.FallAlpha, Mixed);
		Loco.Pose.CopyBonesFrom(Mixed.Pose);
		Loco.Curve.CopyFrom(Mixed.Curve);
	}

	// 2b. Crouch (v1.1), procedural: the Mannequin pack has no crouch clips.
	if (S.CrouchAlpha > 0.01f)
	{
		ApplyCrouch(Loco);
	}

	// 3. Death replaces everything once blended in.
	if (S.DeathTime >= 0.f && S.Death)
	{
		FPoseContext Death(Output);
		SamplePose(S.Death, S.DeathTime, false, Death);
		Blend(Loco, Death, 1.f - S.DeathAlpha, Output);
		return true;
	}

	// 4. Upper-body action, spine_01 and everything under it.
	if (S.UpperAlpha > 0.f && S.Upper)
	{
		FPoseContext Upper(Output);
		SamplePose(S.Upper, S.UpperTime, false, Upper);

		const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
		const int32 RootPoseIndex = Bones.GetPoseBoneIndexForBoneName(GUpperBodyRoot);
		TArray<float> Weights;
		Weights.SetNumZeroed(Output.Pose.GetNumBones());
		if (RootPoseIndex != INDEX_NONE)
		{
			const FCompactPoseBoneIndex Root = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(RootPoseIndex));
			for (const FCompactPoseBoneIndex Bone : Output.Pose.ForEachBoneIndex())
			{
				// Walk up the parent chain; bones under spine_01 get the action.
				for (FCompactPoseBoneIndex P = Bone; P != INDEX_NONE; P = Bones.GetParentBoneIndex(P))
				{
					if (P == Root)
					{
						Weights[Bone.GetInt()] = S.UpperAlpha * S.UpperWeightScale;
						break;
					}
				}
			}
		}

		FAnimationPoseData LocoData(Loco);
		FAnimationPoseData UpperData(Upper);
		FAnimationPoseData OutData(Output);
		FAnimationRuntime::BlendTwoPosesTogetherPerBone(LocoData, UpperData, Weights, OutData);
	}
	else
	{
		Output.Pose.CopyBonesFrom(Loco.Pose);
		Output.Curve.CopyFrom(Loco.Curve);
	}

	// 5. Aim offset (third person only).
	if (S.bApplyAim)
	{
		if (S.AimPitch > 0.f)
		{
			ApplyAdditive(Output, S.AimUp, 0.f, S.AimPitch / 80.f, Self);
		}
		else if (S.AimPitch < 0.f)
		{
			ApplyAdditive(Output, S.AimDown, 0.f, -S.AimPitch / 80.f, Self);
		}
	}

	// 6. Recoil and hit reaction.
	if (S.FireTime >= 0.f)
	{
		ApplyAdditive(Output, S.Fire, S.FireTime, 1.f, Self);
	}
	if (S.HitTime >= 0.f)
	{
		ApplyAdditive(Output, S.HitReact, S.HitTime, 1.f, Self);
	}
	if (S.LandTime >= 0.f)
	{
		ApplyAdditive(Output, S.LandRecovery, S.LandTime, S.LandWeight, Self);
	}

	if (S.ThrowTime >= 0.f)
	{
		ApplyThrow(Output);
	}
	if (S.MeleeTime >= 0.f)
	{
		ApplyMelee(Output);
	}

	// 7. Left hand onto the weapon (v1.0). The weapon is rigid on hand_r, so
	// the target is a fixed point in hand_r space; the Mannequin clips were
	// made for Epic's weapons and put the hand where their forend was, which
	// matches no other model.
	if (S.bTwoHandIK)
	{
		SolveTwoHandIK(Output);
	}
	else if (S.LeftHandIKAlpha > 0.f)
	{
		SolveLeftHandIK(Output);
	}

	return true;
}

void FCSAnimInstanceProxy::SolveTwoHandIK(FPoseContext& Output) const
{
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	auto Index = [&Bones](const TCHAR* Name) -> FCompactPoseBoneIndex
	{
		const int32 PoseIndex = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return PoseIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE)
			: Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(PoseIndex));
	};
	const FCompactPoseBoneIndex Chain[2][3] = {
		{ Index(TEXT("upperarm_r")), Index(TEXT("lowerarm_r")), Index(TEXT("hand_r")) },
		{ Index(TEXT("upperarm_l")), Index(TEXT("lowerarm_l")), Index(TEXT("hand_l")) },
	};
	for (const auto& Arm : Chain)
	{
		for (const FCompactPoseBoneIndex& Bone : Arm)
		{
			if (Bone == INDEX_NONE)
			{
				return;
			}
		}
	}

	FCSPose<FCompactPose> CSPose;
	CSPose.InitPose(Output.Pose);

	// Where the clip holds its (Epic) weapon: the HandGrip_R socket frame.
	const FTransform HandRAnim = CSPose.GetComponentSpaceTransform(Chain[0][2]);
	const FTransform SocketAnim = Snapshot.GripSocketLocal * HandRAnim;

	// Right hand: its socket onto the model's grip frame.
	const FTransform HandRTarget = Snapshot.GripSocketLocal.Inverse() * Snapshot.GripFrameCS;
	// Left hand: the clip's hold relative to its weapon, carried over to the
	// model, then moved onto the model's support point.
	FTransform HandLTarget = CSPose.GetComponentSpaceTransform(Chain[1][2]).GetRelativeTransform(SocketAnim)
		* Snapshot.GripFrameCS;
	// The palm (HandGrip_L), not the wrist, onto the support point.
	HandLTarget.SetLocation(Snapshot.SupportCS - HandLTarget.GetRotation().RotateVector(Snapshot.LeftGripOffset));

	TArray<FBoneTransform> Solved;
	const FTransform* Targets[2] = { &HandRTarget, &HandLTarget };
	for (int32 Side = 0; Side < 2; ++Side)
	{
		FTransform RootT = CSPose.GetComponentSpaceTransform(Chain[Side][0]);
		FTransform JointT = CSPose.GetComponentSpaceTransform(Chain[Side][1]);
		FTransform EndT = CSPose.GetComponentSpaceTransform(Chain[Side][2]);

		// Elbows hang down and a little out, as when holding a rifle.
		const FVector Mid = (RootT.GetLocation() + EndT.GetLocation()) * 0.5f;
		const FVector Bend = (JointT.GetLocation() - Mid).GetSafeNormal() * 0.5f + FVector(0.f, 0.f, -1.f);
		const FVector Pole = JointT.GetLocation() + Bend.GetSafeNormal() * 40.f;

		AnimationCore::SolveTwoBoneIK(RootT, JointT, EndT, Pole, Targets[Side]->GetLocation(),
			/*bAllowStretching*/ false, 1.0, 1.0);
		EndT.SetRotation(Targets[Side]->GetRotation());

		Solved.Add(FBoneTransform(Chain[Side][0], RootT));
		Solved.Add(FBoneTransform(Chain[Side][1], JointT));
		Solved.Add(FBoneTransform(Chain[Side][2], EndT));
	}
	// SafeSetCSBoneTransforms wants parents before children.
	Solved.Sort([](const FBoneTransform& A, const FBoneTransform& B) { return A.BoneIndex < B.BoneIndex; });
	CSPose.SafeSetCSBoneTransforms(Solved);
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CSPose), Output.Pose);
}

void FCSAnimInstanceProxy::SolveLeftHandIK(FPoseContext& Output) const
{
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	auto Index = [&Bones](const TCHAR* Name) -> FCompactPoseBoneIndex
	{
		const int32 PoseIndex = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return PoseIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE)
			: Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(PoseIndex));
	};
	const FCompactPoseBoneIndex Upper = Index(TEXT("upperarm_l"));
	const FCompactPoseBoneIndex Lower = Index(TEXT("lowerarm_l"));
	const FCompactPoseBoneIndex Hand = Index(TEXT("hand_l"));
	const FCompactPoseBoneIndex HandR = Index(TEXT("hand_r"));
	if (Upper == INDEX_NONE || Lower == INDEX_NONE || Hand == INDEX_NONE || HandR == INDEX_NONE)
	{
		return;
	}

	FCSPose<FCompactPose> CSPose;
	CSPose.InitPose(Output.Pose);
	FTransform RootT = CSPose.GetComponentSpaceTransform(Upper);
	FTransform JointT = CSPose.GetComponentSpaceTransform(Lower);
	FTransform EndT = CSPose.GetComponentSpaceTransform(Hand);
	const FTransform HandRT = CSPose.GetComponentSpaceTransform(HandR);

	// The palm (HandGrip_L), not the wrist, onto the support point.
	const FVector Palm = HandRT.TransformPosition(Snapshot.LeftHandTargetInHandR)
		- EndT.GetRotation().RotateVector(Snapshot.LeftGripOffset);
	const FVector Target = FMath::Lerp(EndT.GetLocation(), Palm, Snapshot.LeftHandIKAlpha);

	// Pole: the elbow bends the way the clip bends it, biased downwards - a
	// forend further out than the clip's would otherwise swing it up and out.
	const FVector Mid = (RootT.GetLocation() + EndT.GetLocation()) * 0.5f;
	const FVector Bend = (JointT.GetLocation() - Mid).GetSafeNormal() * 0.5f + FVector(0.f, 0.f, -1.f);
	const FVector Pole = JointT.GetLocation() + Bend.GetSafeNormal() * 40.f;

	AnimationCore::SolveTwoBoneIK(RootT, JointT, EndT, Pole, Target, /*bAllowStretching*/ false, 1.0, 1.0);

	TArray<FBoneTransform> Solved;
	Solved.Add(FBoneTransform(Upper, RootT));
	Solved.Add(FBoneTransform(Lower, JointT));
	Solved.Add(FBoneTransform(Hand, EndT));
	CSPose.SafeSetCSBoneTransforms(Solved);
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CSPose), Output.Pose);
}

void FCSAnimInstanceProxy::ApplyCrouch(FPoseContext& Output) const
{
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	auto Index = [&Bones](const TCHAR* Name) -> FCompactPoseBoneIndex
	{
		const int32 PoseIndex = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return PoseIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE)
			: Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(PoseIndex));
	};
	const FCompactPoseBoneIndex Pelvis = Index(TEXT("pelvis"));
	const FCompactPoseBoneIndex Spine = Index(TEXT("spine_01"));
	const FCompactPoseBoneIndex Legs[2][3] = {
		{ Index(TEXT("thigh_l")), Index(TEXT("calf_l")), Index(TEXT("foot_l")) },
		{ Index(TEXT("thigh_r")), Index(TEXT("calf_r")), Index(TEXT("foot_r")) },
	};
	if (Pelvis == INDEX_NONE || Spine == INDEX_NONE)
	{
		return;
	}
	for (const auto& Leg : Legs)
	{
		for (const FCompactPoseBoneIndex& Bone : Leg)
		{
			if (Bone == INDEX_NONE)
			{
				return;
			}
		}
	}

	const float A = Snapshot.CrouchAlpha;
	// The Mannequin faces +Y in component space; up is +Z, its right is -X.
	constexpr float Drop = 42.f;
	constexpr float Back = 8.f;
	constexpr float LeanDeg = 18.f;

	FCSPose<FCompactPose> CSPose;
	CSPose.InitPose(Output.Pose);

	// Feet stay where the locomotion put them.
	const FTransform Feet[2] = { CSPose.GetComponentSpaceTransform(Legs[0][2]), CSPose.GetComponentSpaceTransform(Legs[1][2]) };

	// 1. Pelvis down and a little back; everything above follows.
	FTransform PelvisT = CSPose.GetComponentSpaceTransform(Pelvis);
	PelvisT.AddToTranslation(FVector(0.f, -Back * A, -Drop * A));
	{
		TArray<FBoneTransform> Moved;
		Moved.Add(FBoneTransform(Pelvis, PelvisT));
		CSPose.SafeSetCSBoneTransforms(Moved);
	}

	// 2. Legs: two-bone IK back onto the planted feet, knees forward.
	TArray<FBoneTransform> Solved;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		FTransform RootT = CSPose.GetComponentSpaceTransform(Legs[Side][0]);
		FTransform JointT = CSPose.GetComponentSpaceTransform(Legs[Side][1]);
		FTransform EndT = CSPose.GetComponentSpaceTransform(Legs[Side][2]);
		const FVector Pole = JointT.GetLocation() + FVector(Side == 0 ? 8.f : -8.f, 60.f, 0.f);
		AnimationCore::SolveTwoBoneIK(RootT, JointT, EndT, Pole, Feet[Side].GetLocation(), /*bAllowStretching*/ false, 1.0, 1.0);
		EndT.SetRotation(Feet[Side].GetRotation());
		Solved.Add(FBoneTransform(Legs[Side][0], RootT));
		Solved.Add(FBoneTransform(Legs[Side][1], JointT));
		Solved.Add(FBoneTransform(Legs[Side][2], EndT));
	}

	// 3. Upper body leans into the crouch (about the character's right axis).
	FTransform SpineT = CSPose.GetComponentSpaceTransform(Spine);
	const FQuat Lean(FVector(1.f, 0.f, 0.f), FMath::DegreesToRadians(-LeanDeg * A));
	SpineT.SetRotation(Lean * SpineT.GetRotation());
	Solved.Add(FBoneTransform(Spine, SpineT));

	Solved.Sort([](const FBoneTransform& X, const FBoneTransform& Y) { return X.BoneIndex < Y.BoneIndex; });
	CSPose.SafeSetCSBoneTransforms(Solved);
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CSPose), Output.Pose);
}

void FCSAnimInstanceProxy::ApplyThrow(FPoseContext& Output) const
{
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	auto Index = [&Bones](const TCHAR* Name) -> FCompactPoseBoneIndex
	{
		const int32 PoseIndex = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return PoseIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE)
			: Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(PoseIndex));
	};
	const FCompactPoseBoneIndex Spine = Index(TEXT("spine_03"));
	const FCompactPoseBoneIndex Arm = Index(TEXT("upperarm_r"));
	if (Spine == INDEX_NONE || Arm == INDEX_NONE)
	{
		return;
	}

	// 0 - 0.22 s wind-up (arm back and up, chest turns away), 0.22 - 0.36 s
	// release (arm whips forward over the shoulder), then recovery.
	const float T = Snapshot.ThrowTime;
	float ArmPitch = 0.f;	// + = arm back / up
	float ChestYaw = 0.f;
	if (T < 0.22f)
	{
		const float A = FMath::SmoothStep(0.f, 1.f, T / 0.22f);
		ArmPitch = 110.f * A;
		ChestYaw = 25.f * A;
	}
	else if (T < 0.36f)
	{
		const float A = FMath::SmoothStep(0.f, 1.f, (T - 0.22f) / 0.14f);
		ArmPitch = FMath::Lerp(110.f, -40.f, A);
		ChestYaw = FMath::Lerp(25.f, -20.f, A);
	}
	else
	{
		const float A = FMath::SmoothStep(0.f, 1.f, (T - 0.36f) / 0.34f);
		ArmPitch = FMath::Lerp(-40.f, 0.f, A);
		ChestYaw = FMath::Lerp(-20.f, 0.f, A);
	}

	FCSPose<FCompactPose> CSPose;
	CSPose.InitPose(Output.Pose);

	// Component space: the Mannequin faces +Y, up is +Z, its right is -X.
	TArray<FBoneTransform> Changed;
	FTransform SpineT = CSPose.GetComponentSpaceTransform(Spine);
	SpineT.SetRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(ChestYaw)) * SpineT.GetRotation());
	Changed.Add(FBoneTransform(Spine, SpineT));
	CSPose.SafeSetCSBoneTransforms(Changed);

	Changed.Reset();
	FTransform ArmT = CSPose.GetComponentSpaceTransform(Arm);
	// About the shoulder's side axis: positive swings the hand back and up.
	ArmT.SetRotation(FQuat(FVector(1.f, 0.f, 0.f), FMath::DegreesToRadians(ArmPitch)) * ArmT.GetRotation());
	Changed.Add(FBoneTransform(Arm, ArmT));
	CSPose.SafeSetCSBoneTransforms(Changed);

	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CSPose), Output.Pose);
}

void FCSAnimInstanceProxy::ApplyMelee(FPoseContext& Output) const
{
	// v2.0 knife on the third-person body: a flat slash across the chest, or a
	// straight stab. Same component-space rotation as the throw (see ApplyThrow).
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	auto Index = [&Bones](const TCHAR* Name)
	{
		const int32 PoseIndex = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return PoseIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE)
			: Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(PoseIndex));
	};
	const FCompactPoseBoneIndex Spine = Index(TEXT("spine_03"));
	const FCompactPoseBoneIndex Arm = Index(TEXT("upperarm_r"));
	if (Spine == INDEX_NONE || Arm == INDEX_NONE)
	{
		return;
	}

	const float T = Snapshot.MeleeTime;
	float ChestYaw = 0.f;	// + = turns to the left
	float ArmYaw = 0.f;		// swings the arm across the body
	float ArmPitch = 0.f;	// + = arm back / up
	if (Snapshot.bMeleeHeavy)
	{
		const float Back = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(T / 0.18f, 0.f, 1.f));
		const float Drive = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((T - 0.18f) / 0.12f, 0.f, 1.f));
		const float Out = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((T - 0.55f) / 0.4f, 0.f, 1.f));
		ArmPitch = (35.f * Back - 60.f * Drive) * (1.f - Out);
		ChestYaw = (-15.f * Back + 20.f * Drive) * (1.f - Out);
	}
	else
	{
		const float Cock = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(T / 0.08f, 0.f, 1.f));
		const float Sweep = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((T - 0.08f) / 0.14f, 0.f, 1.f));
		const float Out = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((T - 0.24f) / 0.2f, 0.f, 1.f));
		ArmYaw = FMath::Lerp(-40.f * Cock, 70.f, Sweep) * (1.f - Out);
		ChestYaw = FMath::Lerp(-20.f * Cock, 25.f, Sweep) * (1.f - Out);
		ArmPitch = 20.f * (1.f - Out) * Cock;
	}

	FCSPose<FCompactPose> CSPose;
	CSPose.InitPose(Output.Pose);

	TArray<FBoneTransform> Changed;
	FTransform SpineT = CSPose.GetComponentSpaceTransform(Spine);
	SpineT.SetRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(ChestYaw)) * SpineT.GetRotation());
	Changed.Add(FBoneTransform(Spine, SpineT));
	CSPose.SafeSetCSBoneTransforms(Changed);

	Changed.Reset();
	FTransform ArmT = CSPose.GetComponentSpaceTransform(Arm);
	const FQuat Swing = FQuat(FVector::UpVector, FMath::DegreesToRadians(ArmYaw))
		* FQuat(FVector(1.f, 0.f, 0.f), FMath::DegreesToRadians(ArmPitch));
	ArmT.SetRotation(Swing * ArmT.GetRotation());
	Changed.Add(FBoneTransform(Arm, ArmT));
	CSPose.SafeSetCSBoneTransforms(Changed);

	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CSPose), Output.Pose);
}
