// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Weapons/CSWeaponComponent.h"

#include "Audio/CSAudio.h"
#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
#include "Components/CapsuleComponent.h"
#include "Core/CSAuthority.h"
#include "Core/CSCombatSettings.h"
#include "Core/CSLog.h"
#include "Engine/World.h"
#include "Weapons/CSWeaponDefinition.h"

UCSWeaponComponent::UCSWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(false); // No authoritative state lives here.
}

void UCSWeaponComponent::BeginPlay()
{
	Super::BeginPlay();
	OwnerCharacter = Cast<ACSCharacter>(GetOwner());
}

ACSMatchDirector* UCSWeaponComponent::GetDirector() const
{
	return ACSMatchDirector::Get(this);
}

const UCSWeaponDefinition* UCSWeaponComponent::GetStarterWeapon() const
{
	const UCSCombatSettings* Settings = UCSCombatSettings::Get();
	return Settings ? Settings->StarterWeapon.LoadSynchronous() : nullptr;
}

const UCSWeaponDefinition* UCSWeaponComponent::GetActiveWeapon() const
{
	// The equipped inventory weapon if any, else the starter pistol, which is
	// always available by design. Resolved from authoritative state, so the
	// client and the authority agree on what is in hand.
	if (OwnerCharacter)
	{
		if (const ACSMatchDirector* Director = GetDirector())
		{
			if (const UCSWeaponDefinition* Weapon = Director->GetLoadout(OwnerCharacter->GetOwningPlayerId()).Weapon)
			{
				return Weapon;
			}
		}
	}
	return GetStarterWeapon();
}

// ---------------------------------------------------------------------------
// Local input
// ---------------------------------------------------------------------------

void UCSWeaponComponent::StartFire()
{
	bTriggerHeld = true;
	TryFireOnce();
}

void UCSWeaponComponent::StopFire()
{
	bTriggerHeld = false;
}

void UCSWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const UCSWeaponDefinition* Weapon = GetActiveWeapon();
	if (!Weapon)
	{
		return;
	}

	// Spread recovery is local and cosmetic.
	CurrentBloomDegrees = FMath::Max(0.f,
		CurrentBloomDegrees - Weapon->SpreadRecoveryPerSecond * DeltaTime);

	if (bTriggerHeld && Weapon->bAutomatic)
	{
		TryFireOnce();
	}
}

float UCSWeaponComponent::GetCurrentSpreadDegrees() const
{
	const UCSWeaponDefinition* Weapon = GetActiveWeapon();
	if (!Weapon)
	{
		return 0.f;
	}

	const float Base = bAiming ? Weapon->AimSpreadDegrees : Weapon->HipSpreadDegrees;
	return Base + CurrentBloomDegrees;
}

void UCSWeaponComponent::TryFireOnce()
{
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled())
	{
		return;
	}

	const UCSWeaponDefinition* Weapon = GetActiveWeapon();
	if (!Weapon)
	{
		UE_LOG(LogCSCombat, Warning,
			TEXT("No starter weapon configured. Set CS Combat > StarterWeapon in Project Settings."));
		return;
	}

	// Local gate. This only keeps the client from spamming the wire and makes
	// the trigger feel right - it is NOT the check that matters. The authority
	// re-runs every one of these against its own state.
	const double Now = UCSAuthority::GetNetworkTimeSeconds(this);
	if (LocalLastFireTime > 0.0 && (Now - LocalLastFireTime) < Weapon->GetFireInterval())
	{
		return;
	}

	const int32 LocalId = OwnerCharacter->GetOwningPlayerId();
	if (const ACSMatchDirector* Director = GetDirector())
	{
		if (!Director->IsPlayerAlive(LocalId))
		{
			return;
		}
		const FCSLoadoutView Loadout = Director->GetLoadout(LocalId);
		if (Loadout.bReloading)
		{
			return;
		}
		if (Loadout.RoundsInMag <= 0)
		{
			// Dry fire: ask for a reload rather than spamming the server.
			if (Loadout.Weapon)
			{
				CSAudio::Play2D(this, Loadout.Weapon->EmptySound, 0.8f);
			}
			RequestReload();
			return;
		}
	}

	LocalLastFireTime = Now;

	FVector Origin;
	FVector Direction;
	OwnerCharacter->GetAimRay(Origin, Direction);

	// Predicted, purely cosmetic feedback so the shot feels instant.
	OwnerCharacter->PlayLocalFireEffects(Weapon);
	CurrentBloomDegrees = FMath::Min(Weapon->MaxBloomSpreadDegrees,
		CurrentBloomDegrees + Weapon->SpreadPerShot);

	// Authoritative request. The result comes back as replicated director state.
	OwnerCharacter->RequestFire(Origin, Direction, bAiming);
}

void UCSWeaponComponent::RequestReload()
{
	if (!OwnerCharacter || !OwnerCharacter->IsLocallyControlled())
	{
		return;
	}
	OwnerCharacter->RequestReload();
}

// ---------------------------------------------------------------------------
// Authority-side resolution
// ---------------------------------------------------------------------------

ECSHitZone UCSWeaponComponent::ResolveHitZone(const FName& BoneName)
{
	if (BoneName.IsNone())
	{
		return ECSHitZone::Torso;
	}

	const FString Bone = BoneName.ToString().ToLower();

	if (Bone.Contains(TEXT("head")) || Bone.Contains(TEXT("neck")))
	{
		return ECSHitZone::Head;
	}
	if (Bone.Contains(TEXT("spine")) || Bone.Contains(TEXT("pelvis")) || Bone.Contains(TEXT("clavicle")))
	{
		return ECSHitZone::Torso;
	}
	if (Bone.Contains(TEXT("arm")) || Bone.Contains(TEXT("hand")) ||
		Bone.Contains(TEXT("leg")) || Bone.Contains(TEXT("foot")) ||
		Bone.Contains(TEXT("thigh")) || Bone.Contains(TEXT("calf")))
	{
		return ECSHitZone::Limb;
	}

	return ECSHitZone::Torso;
}

FCSShotResolution UCSWeaponComponent::ResolveShotOnAuthority(const FVector& AuthoritativeOrigin,
	const FVector& AimDirection, const UCSWeaponDefinition* Weapon) const
{
	FCSShotResolution Result;

	const UWorld* World = GetWorld();
	if (!World || !Weapon)
	{
		return Result;
	}

	const FVector Direction = AimDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return Result;
	}

	const FVector TraceEnd = AuthoritativeOrigin + Direction * Weapon->Range;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CSWeaponTrace), /*bTraceComplex*/ true);
	Params.AddIgnoredActor(GetOwner());
	Params.bReturnPhysicalMaterial = false;

	FHitResult Hit;
	const bool bBlocked = World->LineTraceSingleByChannel(
		Hit, AuthoritativeOrigin, TraceEnd, ECC_Visibility, Params);

	if (!bBlocked)
	{
		Result.ImpactPoint = TraceEnd;
		return Result;
	}

	Result.bHit = true;
	Result.ImpactPoint = Hit.ImpactPoint;

	const ACSCharacter* Victim = Cast<ACSCharacter>(Hit.GetActor());
	if (!Victim)
	{
		// Geometry, not a player.
		return Result;
	}

	if (!Hit.BoneName.IsNone())
	{
		Result.Zone = ResolveHitZone(Hit.BoneName);
	}
	else
	{
		// Placeholder characters are hit on the capsule, which has no bones.
		// Split it by height instead: top of the capsule is the head, the
		// lower part the legs. Stage 6 skeletal meshes take the bone path.
		const UCapsuleComponent* Capsule = Victim->GetCapsuleComponent();
		const float Centre = Victim->GetActorLocation().Z;
		const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f;
		const float Z = Hit.ImpactPoint.Z;

		if (Z >= Centre + HalfHeight - 26.f)
		{
			Result.Zone = ECSHitZone::Head;
		}
		else if (Z <= Centre - HalfHeight * 0.45f)
		{
			Result.Zone = ECSHitZone::Limb;
		}
		else
		{
			Result.Zone = ECSHitZone::Torso;
		}
	}

	// Identity comes from Fusion ownership, never from anything on the pawn.
	Result.VictimPlayerId = UCSAuthority::GetOwningPlayerId(Victim);

	const float Distance = FVector::Dist(AuthoritativeOrigin, Hit.ImpactPoint);
	Result.Damage = Weapon->ComputeDamage(Result.Zone, Distance);

	return Result;
}
