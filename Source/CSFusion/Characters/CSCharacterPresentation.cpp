// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// ACSCharacter presentation (Stage 6): character meshes, weapon in hand,
// animation triggers, footsteps, shot effects and hit feedback.
//
// Everything here is cosmetic and runs on every peer from replicated state:
// the director's loadout drives the weapon mesh and stance, its reload
// window drives the reload animation, RpcConfirmShot drives fire effects and
// RpcCombatEvent drives hit reactions. Nothing here changes gameplay.

#include "Characters/CSCharacter.h"

#include "Animation/CSAnimInstance.h"
#include "Animation/CSAnimationSettings.h"
#include "Audio/CSAudio.h"
#include "Audio/CSAudioSettings.h"
#include "Characters/CSCharacterMovementComponent.h"
#include "Combat/CSMatchDirector.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Core/CSAuthority.h"
#include "Engine/SkeletalMesh.h"
#include "FX/CSEffects.h"
#include "Inventory/CSPlayerInventory.h"
#include "Weapons/CSWeaponDefinition.h"
#include "Weapons/CSWeaponPresentation.h"

UCSAnimInstance* ACSCharacter::GetBodyAnim() const
{
	return GetMesh() ? Cast<UCSAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
}

UCSAnimInstance* ACSCharacter::GetArmsAnim() const
{
	return FirstPersonMesh ? Cast<UCSAnimInstance>(FirstPersonMesh->GetAnimInstance()) : nullptr;
}

void ACSCharacter::SetupCharacterMeshes()
{
	// Manny or Quinn by player id, so two players are easy to tell apart.
	const UCSAnimationSettings* AnimSettings = UCSAnimationSettings::Get();
	if (AnimSettings->CharacterMeshes.Num() > 0)
	{
		const int32 Index = FMath::Abs(GetOwningPlayerId()) % AnimSettings->CharacterMeshes.Num();
		if (USkeletalMesh* CharacterMesh = AnimSettings->CharacterMeshes[Index].LoadSynchronous())
		{
			if (GetMesh()->GetSkeletalMeshAsset() != CharacterMesh)
			{
				GetMesh()->SetSkeletalMesh(CharacterMesh);
			}
			if (FirstPersonMesh->GetSkeletalMeshAsset() != CharacterMesh)
			{
				FirstPersonMesh->SetSkeletalMesh(CharacterMesh);
			}
		}
	}

	// Arms view: hide the head (it would fill the camera) and the legs.
	static const FName HiddenBones[] = { FName(TEXT("head")), FName(TEXT("thigh_l")), FName(TEXT("thigh_r")) };
	for (const FName& Bone : HiddenBones)
	{
		FirstPersonMesh->HideBoneByName(Bone, PBO_None);
	}

	if (UCSAnimInstance* Arms = GetArmsAnim())
	{
		Arms->SetFirstPerson(true);
	}
	if (UCSAnimInstance* BodyAnim = GetBodyAnim())
	{
		BodyAnim->SetFirstPerson(false);
	}
}

FTransform ACSCharacter::GetMuzzleTransform(bool bFirstPersonView) const
{
	const UStaticMeshComponent* ModelComp = bFirstPersonView ? FirstPersonWeaponModel.Get() : ThirdPersonWeaponModel.Get();
	if (const FCSWeaponModel* Model = GetDisplayedModel())
	{
		if (ModelComp && ModelComp->GetStaticMesh())
		{
			const FTransform C = ModelComp->GetComponentTransform();
			FTransform T(C.GetUnitAxis(EAxis::X).ToOrientationQuat(), C.TransformPosition(Model->Muzzle));
			if (bFirstPersonView)
			{
				FVector Origin;
				FVector Direction;
				GetAimRay(Origin, Direction);
				T.SetRotation(Direction.ToOrientationQuat());
			}
			return T;
		}
	}

	const USkeletalMeshComponent* Weapon = bFirstPersonView ? FirstPersonWeapon.Get() : ThirdPersonWeapon.Get();
	const UCSWeaponDefinition* Def = DisplayedWeapon.Get();
	const FName Socket = Def ? Def->MuzzleSocket : FName(TEXT("Muzzle"));
	if (Weapon && Weapon->GetSkeletalMeshAsset() && Weapon->DoesSocketExist(Socket))
	{
		FTransform T = Weapon->GetSocketTransform(Socket);
		// Fire along the view in first person: the gun barrel is only roughly
		// aligned with the crosshair, and a flash pointing sideways looks wrong.
		if (bFirstPersonView)
		{
			FVector Origin;
			FVector Direction;
			GetAimRay(Origin, Direction);
			T.SetRotation(Direction.ToOrientationQuat());
		}
		return T;
	}
	// No weapon mesh: start from in front of the eyes.
	FVector Origin;
	FVector Direction;
	GetAimRay(Origin, Direction);
	return FTransform(Direction.Rotation(), Origin + Direction * 40.f - FVector(0.f, 0.f, 10.f));
}

void ACSCharacter::UpdateWeaponPresentation()
{
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	if (!Director || !IsAliveAuthoritative())
	{
		return;
	}
	const FCSLoadoutView Loadout = Director->GetLoadout(GetOwningPlayerId());
	const UCSWeaponDefinition* Weapon = Loadout.Weapon;

	if (Weapon != DisplayedWeapon.Get())
	{
		const bool bFirstTime = !DisplayedWeapon.IsValid();
		DisplayedWeapon = Weapon;
		bShownReloading = false;

		// v1.0: a static model placed in the hand frame (see
		// CSWeaponPresentation.h) when the weapon has one; otherwise the data
		// asset's skeletal mesh, as before.
		const FCSWeaponModel* Model = UCSWeaponPresentationSettings::Find(Weapon);
		UStaticMesh* ModelMesh = Model ? Model->Mesh.LoadSynchronous() : nullptr;
		if (ModelMesh)
		{
			// Third person: in the hand. First person: placed by
			// UpdateFirstPersonView every frame.
			FirstPersonWeaponModel->SetStaticMesh(ModelMesh);
			ThirdPersonWeaponModel->SetStaticMesh(ModelMesh);
			ThirdPersonWeaponModel->SetRelativeTransform(Model->GetMeshInHand());
			FirstPersonWeapon->SetSkeletalMesh(nullptr);
			ThirdPersonWeapon->SetSkeletalMesh(nullptr);
		}
		else
		{
			FirstPersonWeaponModel->SetStaticMesh(nullptr);
			ThirdPersonWeaponModel->SetStaticMesh(nullptr);

			USkeletalMesh* FPMesh = Weapon ? Weapon->FirstPersonMesh.LoadSynchronous() : nullptr;
			USkeletalMesh* TPMesh = Weapon ? Weapon->ThirdPersonMesh.LoadSynchronous() : nullptr;
			if (!TPMesh)
			{
				TPMesh = FPMesh;
			}
			if (!FPMesh)
			{
				FPMesh = TPMesh;
			}
			const float Scale = Weapon ? Weapon->MeshScale : 1.f;
			FirstPersonWeapon->SetSkeletalMesh(FPMesh);
			ThirdPersonWeapon->SetSkeletalMesh(TPMesh);
			FirstPersonWeapon->SetRelativeScale3D(FVector(Scale));
			ThirdPersonWeapon->SetRelativeScale3D(FVector(Scale));
		}
		UpdateHandTargets();

		const ECSWeaponStance NewStance = Weapon ? Weapon->Stance : ECSWeaponStance::Pistol;
		for (UCSAnimInstance* Anim : { GetBodyAnim(), GetArmsAnim() })
		{
			if (Anim)
			{
				Anim->SetStance(NewStance, /*bInstant*/ bFirstTime);
			}
		}
		if (!bFirstTime && Weapon)
		{
			CSAudio::PlayAt(this, Weapon->EquipSound, GetActorLocation(), IsLocalPlayerView() ? 0.8f : 0.6f);
		}
		ViewEquipTime = bFirstTime ? -1.f : 0.f;
		ViewReloadTime = -1.f;
	}

	// Pickup: the replicated inventory grew.
	if (const ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(this, GetOwningPlayerId()))
	{
		int32 Items = 0;
		for (const FCSInventorySlot& Slot : Inventory->GetSlots())
		{
			Items += Slot.IsEmpty() ? 0 : Slot.Count;
		}
		if (LastInventoryItems >= 0 && Items > LastInventoryItems)
		{
			CSAudio::PlayAt(this, UCSAudioSettings::Get()->Pickup, GetActorLocation(), IsLocalPlayerView() ? 0.8f : 0.6f);
		}
		LastInventoryItems = Items;
	}

	// Reload: follow the authority's reload window so every peer sees it.
	if (Loadout.bReloading && !bShownReloading)
	{
		bShownReloading = true;
		const double Now = UCSAuthority::GetNetworkTimeSeconds(this);
		FCSPlayerCombatRecord Record;
		float Duration = Weapon ? Weapon->ReloadSeconds : 1.5f;
		if (Director->GetRecord(GetOwningPlayerId(), Record) && Record.ReloadCompleteNetworkTime > Now)
		{
			Duration = static_cast<float>(Record.ReloadCompleteNetworkTime - Now);
		}
		for (UCSAnimInstance* Anim : { GetBodyAnim(), GetArmsAnim() })
		{
			if (Anim)
			{
				Anim->PlayReload(Duration);
			}
		}
		ViewReloadTime = 0.f;
		ViewReloadDuration = FMath::Max(0.3f, Duration);
		if (Weapon)
		{
			CSAudio::PlayAt(this, Weapon->ReloadSound, GetActorLocation(), IsLocalPlayerView() ? 0.9f : 0.7f);
		}
	}
	else if (!Loadout.bReloading)
	{
		bShownReloading = false;
	}
}

void ACSCharacter::UpdateFootsteps(float DeltaSeconds)
{
	const UCSCharacterMovementComponent* Movement = GetCSMovement();
	if (!Movement || !IsAliveAuthoritative())
	{
		return;
	}

	const bool bFalling = Movement->IsFalling();
	const UCSAudioSettings* Audio = UCSAudioSettings::Get();
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());

	if (bWasFalling && !bFalling)
	{
		CSAudio::PlayAt(this, Audio->JumpLand, Feet + FVector(0.f, 0.f, 10.f), IsLocalPlayerView() ? 0.7f : 1.f, 1.f, ECSSound::Footstep);
		StepAccumulator = 0.f;
	}
	bWasFalling = bFalling;

	if (bFalling || Audio->Footsteps.Num() == 0)
	{
		return;
	}

	const float Speed = GetVelocity().Size2D();
	if (Speed < 60.f)
	{
		StepAccumulator = Audio->StepDistance * 0.6f; // the first step comes quickly
		return;
	}
	// Crouch-walking is silent, as in most competitive shooters.
	if (Movement->IsCrouching())
	{
		return;
	}

	StepAccumulator += Speed * DeltaSeconds;
	if (StepAccumulator >= Audio->StepDistance)
	{
		StepAccumulator = 0.f;
		// The set follows the physical material underfoot (metal, wood, dirt,
		// otherwise stone).
		const TArray<TSoftObjectPtr<USoundBase>>& Steps = CSAudio::FootstepsFor(CSAudio::SurfaceBelow(this, Feet, this));
		const int32 Pick = FMath::RandRange(0, Steps.Num() - 1);
		const float Volume = (IsLocalPlayerView() ? 0.45f : 0.9f) * FMath::GetMappedRangeValueClamped(
			FVector2D(150.f, 620.f), FVector2D(0.6f, 1.f), Speed);
		CSAudio::PlayAt(this, Steps[Pick], Feet + FVector(0.f, 0.f, 10.f), Volume, FMath::FRandRange(0.92f, 1.08f),
			ECSSound::Footstep);
	}
}

void ACSCharacter::PlayShotPresentation(const FVector& TracerEnd, bool bLocalPrediction)
{
	const UCSWeaponDefinition* Weapon = DisplayedWeapon.Get();
	const bool bFirstPersonView = IsLocalPlayerView();

	for (UCSAnimInstance* Anim : { GetBodyAnim(), GetArmsAnim() })
	{
		if (Anim)
		{
			Anim->PlayFire();
		}
	}

	if (bFirstPersonView)
	{
		FireKick = 1.f; // procedural kick in UpdateFirstPersonView
	}

	const FTransform Muzzle = GetMuzzleTransform(bFirstPersonView);
	const float FlashScale = Weapon ? Weapon->MuzzleFlashScale : 1.f;

	// The owner sees the first-person flash; everyone else the third-person one.
	CSEffects::MuzzleFlash(GetWorld(), Muzzle, FlashScale, this, bFirstPersonView);

	if (Weapon)
	{
		// Own shots skip occlusion and the gunshot voice limit: a muzzle pushed
		// into a wall must not muffle the shooter's own gun.
		CSAudio::PlayAt(this, Weapon->FireSound, Muzzle.GetLocation(), bFirstPersonView ? 0.85f : 1.f,
			FMath::FRandRange(0.96f, 1.04f), bFirstPersonView ? ECSSound::World : ECSSound::Weapon);
	}

	const FLinearColor TracerColor = Weapon ? Weapon->TracerColor : FLinearColor(1.f, 0.75f, 0.35f);
	CSEffects::Tracer(GetWorld(), Muzzle.GetLocation(), TracerEnd, TracerColor);
}

void ACSCharacter::BindCombatEvents()
{
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	if (!Director || BoundDirector.Get() == Director)
	{
		return;
	}
	if (ACSMatchDirector* Old = BoundDirector.Get())
	{
		Old->OnCombatEvent.Remove(CombatEventHandle);
	}
	BoundDirector = Director;
	CombatEventHandle = Director->OnCombatEvent.AddUObject(this, &ACSCharacter::HandleCombatEvent);
}

void ACSCharacter::UnbindCombatEvents()
{
	if (ACSMatchDirector* Director = BoundDirector.Get())
	{
		Director->OnCombatEvent.Remove(CombatEventHandle);
	}
	BoundDirector.Reset();
}

void ACSCharacter::HandleCombatEvent(const FCSCombatEvent& Event)
{
	const int32 Me = GetOwningPlayerId();
	const UCSAudioSettings* Audio = UCSAudioSettings::Get();

	if (Event.VictimId == Me)
	{
		LastHitFrom = Event.FromLocation;
		bHasLastHitFrom = true;

		if (!Event.bKilled)
		{
			const FVector Local = GetActorRotation().UnrotateVector(Event.FromLocation - GetActorLocation());
			for (UCSAnimInstance* Anim : { GetBodyAnim(), GetArmsAnim() })
			{
				if (Anim)
				{
					Anim->PlayHitReact(Local.X >= 0.f);
				}
			}
		}
		if (IsLocalPlayerView())
		{
			CSAudio::Play2D(this, Audio->Hurt, 0.8f);
		}
	}

	// Shooter feedback, only on the shooter's own screen.
	if (Event.InstigatorId == Me && Event.VictimId != Me && IsLocalPlayerView())
	{
		if (Event.bKilled)
		{
			CSAudio::Play2D(this, Audio->KillConfirm, 0.9f);
		}
		else if (Event.Zone == ECSHitZone::Head)
		{
			CSAudio::Play2D(this, Audio->Headshot, 0.9f);
		}
		else
		{
			CSAudio::Play2D(this, Audio->HitMarker, 0.7f);
		}
	}
}

// ---------------------------------------------------------------------------
// v1.1 death ragdoll
// ---------------------------------------------------------------------------

void ACSCharacter::SetRagdoll(bool bEnable)
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!Body || bEnable == bRagdoll)
	{
		return;
	}
	if (bEnable && !Body->GetPhysicsAsset())
	{
		return; // no physics asset: the death clip plays instead
	}
	bRagdoll = bEnable;

	if (bEnable)
	{
		// Collides with the world, never with shots, cameras or other players.
		Body->SetCollisionProfileName(TEXT("Ragdoll"));
		Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		Body->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Body->SetCollisionResponseToChannel(CSCollision::AudioOcclusion, ECR_Ignore);
		Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Body->SetAllBodiesSimulatePhysics(true);
		Body->SetSimulatePhysics(true);
		Body->WakeAllRigidBodies();
		Body->bBlendPhysics = true;

		// Knocked away from where the killing shots came from, keeping the
		// momentum the body had (a running player keeps sliding forward).
		FVector Push = GetActorForwardVector() * -1.f;
		if (bHasLastHitFrom)
		{
			Push = (GetActorLocation() - LastHitFrom).GetSafeNormal2D();
		}
		const FVector Carry = GetVelocity() * 0.8f;
		Body->SetAllPhysicsLinearVelocity(Carry);
		Body->AddImpulse(Push * 260.f + FVector(0.f, 0.f, 60.f), TEXT("spine_03"), /*bVelChange*/ true);
		return;
	}

	Body->SetSimulatePhysics(false);
	Body->SetAllBodiesSimulatePhysics(false);
	Body->bBlendPhysics = false;
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCollisionProfileName(TEXT("CharacterMesh"));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Simulation detaches the mesh from the capsule; put it back where it lives.
	Body->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	Body->SetRelativeLocationAndRotation(GetBaseTranslationOffset(), GetBaseRotationOffset());
	bSmoothingInit = false;
}

// ---------------------------------------------------------------------------
// v1.1 spawn protection look
// ---------------------------------------------------------------------------

void ACSCharacter::UpdateProtectionLook(float DeltaSeconds)
{
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	const bool bProtected = Director && Director->IsProtected(GetOwningPlayerId());
	GhostAlpha = FMath::FInterpConstantTo(GhostAlpha, bProtected ? 1.f : 0.f, DeltaSeconds, 5.f);

	UMaterialInterface* Ghost = UCSAnimationSettings::Get()->SpawnProtectionMaterial.LoadSynchronous();
	if (!Ghost)
	{
		return;
	}

	TArray<UMeshComponent*, TInlineAllocator<6>> Meshes;
	Meshes.Add(GetMesh());
	Meshes.Add(ThirdPersonWeaponModel.Get());
	Meshes.Add(ThirdPersonWeapon.Get());
	if (IsLocalPlayerView())
	{
		Meshes.Add(FirstPersonMesh.Get());
		Meshes.Add(FirstPersonWeaponModel.Get());
		Meshes.Add(FirstPersonWeapon.Get());
	}

	if (GhostAlpha > 0.f)
	{
		// Swap every slot to the ghost material. Re-checked each frame, so a
		// weapon switched while protected gets the look too.
		for (UMeshComponent* MeshComp : Meshes)
		{
			if (!MeshComp)
			{
				continue;
			}
			TArray<TObjectPtr<UMaterialInterface>>& Saved = GhostOriginals.FindOrAdd(MeshComp);
			for (int32 Slot = 0; Slot < MeshComp->GetNumMaterials(); ++Slot)
			{
				UMaterialInterface* Current = MeshComp->GetMaterial(Slot);
				if (!Current || Current->GetBaseMaterial() != Ghost->GetBaseMaterial())
				{
					if (Saved.Num() <= Slot)
					{
						Saved.SetNum(Slot + 1);
					}
					Saved[Slot] = Current;
					GhostKeepAlive.AddUnique(Current);
					UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Ghost, this);
					GhostMaterials.Add(MID);
					MeshComp->SetMaterial(Slot, MID);
				}
			}
		}
		for (UMaterialInstanceDynamic* MID : GhostMaterials)
		{
			if (MID)
			{
				MID->SetScalarParameterValue(TEXT("Fade"), GhostAlpha);
			}
		}
		bGhostMaterials = true;
		return;
	}

	if (bGhostMaterials)
	{
		// Protection over: every slot back to what it had.
		for (TPair<TWeakObjectPtr<UMeshComponent>, TArray<TObjectPtr<UMaterialInterface>>>& Pair : GhostOriginals)
		{
			if (UMeshComponent* MeshComp = Pair.Key.Get())
			{
				for (int32 Slot = 0; Slot < Pair.Value.Num() && Slot < MeshComp->GetNumMaterials(); ++Slot)
				{
					UMaterialInterface* Now = MeshComp->GetMaterial(Slot);
					if (Now && Now->GetBaseMaterial() == Ghost->GetBaseMaterial())
					{
						MeshComp->SetMaterial(Slot, Pair.Value[Slot]);
					}
				}
			}
		}
		GhostOriginals.Reset();
		GhostMaterials.Reset();
		GhostKeepAlive.Reset();
		bGhostMaterials = false;
	}
}
