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
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameModes/CSGameState.h"
#include "Items/CSItemDefinition.h"
#include "Items/CSItemSettings.h"
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

const UCSWeaponDefinition* UCSWeaponComponent::GetKnifeWeapon() const
{
	const UCSItemSettings* Items = UCSItemSettings::Get();
	const UCSItemDefinition* Knife = Items->GetItem(Items->FindItemIndex(Items->KnifeItem));
	return Knife ? Knife->Weapon.LoadSynchronous() : nullptr;
}

const UCSWeaponDefinition* UCSWeaponComponent::GetActiveWeapon() const
{
	// Whatever is in the equipped loadout slot, resolved from authoritative
	// state so the client and the authority agree on what is in hand. Until the
	// loadout has replicated the hands show the knife, which everybody carries.
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
	return GetKnifeWeapon();
}

// ---------------------------------------------------------------------------
// Local input
// ---------------------------------------------------------------------------

bool UCSWeaponComponent::IsAiming() const
{
	if (!bAiming || !OwnerCharacter)
	{
		return false;
	}
	const ACSMatchDirector* Director = GetDirector();
	if (!Director)
	{
		return true;
	}
	// Nothing to aim with a grenade or a knife (right click stabs instead).
	const FCSLoadoutView Loadout = Director->GetLoadout(OwnerCharacter->GetOwningPlayerId());
	return !Loadout.bGrenade && !Loadout.bKnife;
}

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

	if (OwnerCharacter && OwnerCharacter->IsLocallyControlled())
	{
		// Another weapon in hand: its series starts afresh. The view keeps
		// what it shows; only the bookkeeping forgets it.
		const ACSMatchDirector* Director = GetDirector();
		const bool bDead = Director && !Director->IsPlayerAlive(OwnerCharacter->GetOwningPlayerId());
		if (SprayWeapon.Get() != Weapon || bDead)
		{
			SprayWeapon = Weapon;
			LocalSpray = FCSSprayState();
			AppliedKick = FRotator::ZeroRotator;
		}
		// The view follows the series: steady while firing, back once the
		// trigger is released (B8).
		if (!AppliedKick.IsNearlyZero(0.001f) || LocalSpray.Series > 0.f)
		{
			ApplyKick(CSShotModel::RecoilAt(*Weapon, CSShotModel::SeriesAt(LocalSpray, *Weapon, UCSAuthority::GetNetworkTimeSeconds(this))));
		}
	}

	if (bTriggerHeld && Weapon->bAutomatic)
	{
		TryFireOnce();
	}
}

void UCSWeaponComponent::ApplyKick(const FRotator& Target)
{
	AController* Controller = OwnerCharacter ? OwnerCharacter->GetController() : nullptr;
	if (!Controller)
	{
		AppliedKick = Target;
		return;
	}
	const FRotator Delta = Target - AppliedKick;
	if (!Delta.IsNearlyZero(0.0001f))
	{
		Controller->SetControlRotation(Controller->GetControlRotation() + Delta);
	}
	AppliedKick = Target;
}

float UCSWeaponComponent::GetCurrentSpreadDegrees() const
{
	const UCSWeaponDefinition* Weapon = GetActiveWeapon();
	if (!Weapon || !OwnerCharacter)
	{
		return 0.f;
	}
	// The same model as the authority, fed with what this machine knows.
	const double Now = UCSAuthority::GetNetworkTimeSeconds(this);
	FCSShooterState Local;
	Local.bAimed = IsAiming() && Now - LocalAimSince >= UCSCombatSettings::Get()->MinAimSeconds;
	Local.SpeedRatio = CSShotModel::SpeedRatioFor(OwnerCharacter->GetVelocity().Size2D());
	const UCharacterMovementComponent* Move = OwnerCharacter->GetCharacterMovement();
	Local.bAirborne = Move && Move->IsFalling();
	return CSShotModel::SpreadDegrees(*Weapon, Local, CSShotModel::SeriesAt(LocalSpray, *Weapon, Now));
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
		return; // loadout not replicated yet
	}

	// Local gate. This only keeps the client from spamming the wire and makes
	// the trigger feel right - it is NOT the check that matters. The authority
	// re-runs every one of these against its own state.
	const double Now = UCSAuthority::GetNetworkTimeSeconds(this);
	if (LocalLastFireTime > 0.0 && (Now - LocalLastFireTime) < Weapon->GetFireInterval())
	{
		return;
	}

	// Mirrors the authority's MatchOver refusal so there is no muzzle flash for
	// a shot that cannot count.
	if (const ACSGameState* GS = GetWorld()->GetGameState<ACSGameState>())
	{
		if (GS->GetMatchPhase() == ECSMatchPhase::PostMatch)
		{
			return;
		}
	}

	const int32 LocalId = OwnerCharacter->GetOwningPlayerId();
	if (const ACSMatchDirector* Director = GetDirector())
	{
		if (!Director->IsPlayerAlive(LocalId))
		{
			return;
		}
		const FCSLoadoutView Loadout = Director->GetLoadout(LocalId);
		// v2.0: the knife slashes, one swing per click.
		if (Loadout.bKnife)
		{
			bTriggerHeld = false;
			TrySwing(/*bHeavy*/ false);
			return;
		}
		// v1.1: a grenade in hand is thrown, one per click.
		if (Loadout.bGrenade)
		{
			const float Interval = UCSCombatSettings::Get()->GrenadeThrowInterval;
			if (Loadout.RoundsInMag > 0 && (LocalLastFireTime <= 0.0 || Now - LocalLastFireTime >= Interval))
			{
				LocalLastFireTime = Now;
				bTriggerHeld = false;
				OwnerCharacter->RequestThrowGrenade();
			}
			return;
		}
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
	FVector ViewDirection;
	OwnerCharacter->GetAimRay(Origin, ViewDirection);

	// The view shows the recoil for this shot already (AppliedKick); the
	// authority adds the pattern itself, so what goes on the wire is where the
	// player aims without it (B8).
	const FVector PlayerAim = (ViewDirection.Rotation() - AppliedKick).Vector();

	// Predicted, purely cosmetic feedback so the shot feels instant: the
	// tracer goes along the view, which is where the bullet goes.
	OwnerCharacter->PlayLocalFireEffects(Weapon);

	// Authoritative request. The result comes back as replicated director state.
	OwnerCharacter->RequestFire(Origin, PlayerAim, bAiming);

	// The next shot sits one step further up the pattern; the view follows.
	CSShotModel::CommitShot(LocalSpray, *Weapon, Now);
	SprayWeapon = Weapon;
	ApplyKick(CSShotModel::RecoilAt(*Weapon, CSShotModel::SeriesAt(LocalSpray, *Weapon, Now)));
}

void UCSWeaponComponent::SetAiming(bool bNewAiming)
{
	// Right mouse with the knife in hand is the heavy stab, not aiming.
	if (bNewAiming && OwnerCharacter && OwnerCharacter->IsLocallyControlled())
	{
		if (const ACSMatchDirector* Director = GetDirector())
		{
			if (Director->GetLoadout(OwnerCharacter->GetOwningPlayerId()).bKnife)
			{
				TrySwing(/*bHeavy*/ true);
				bAiming = false;
				return;
			}
		}
	}
	if (bAiming != bNewAiming && OwnerCharacter && OwnerCharacter->IsLocallyControlled())
	{
		// The authority keeps the aim state and times it (B8).
		LocalAimSince = UCSAuthority::GetNetworkTimeSeconds(this);
		OwnerCharacter->RequestSetAiming(bNewAiming);
	}
	bAiming = bNewAiming;
}

void UCSWeaponComponent::TrySwing(bool bHeavy)
{
	const UCSWeaponDefinition* Knife = GetActiveWeapon();
	if (!OwnerCharacter || !Knife || !Knife->IsKnife())
	{
		return;
	}
	// Local rate gate; the authority re-checks with its own clock.
	const double Now = UCSAuthority::GetNetworkTimeSeconds(this);
	if (Now < LocalNextSwingTime)
	{
		return;
	}
	LocalNextSwingTime = Now + (bHeavy ? Knife->MeleeHeavyInterval : Knife->MeleeInterval);
	OwnerCharacter->RequestMelee(bHeavy);
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

bool UCSWeaponComponent::TraceHitboxes(const ACSCharacter* Victim, const FVector& Origin, const FVector& Direction,
	float MaxDistance, ECSHitZone& OutZone, float& OutDistance)
{
	if (!Victim)
	{
		return false;
	}
	// The stance everyone sees (the body is animated from the same replicated
	// stance), the feet under the replicated location.
	const bool bCrouched = Victim->GetStance() == ECSStanceState::Crouching;
	const FVector Feet = Victim->GetActorLocation() - FVector(0.f, 0.f, CSShotModel::BodyHeight(bCrouched) * 0.5f);
	return CSShotModel::TraceHitboxes(Feet, Victim->GetActorForwardVector(), bCrouched, Origin, Direction, MaxDistance, OutZone, OutDistance);
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

	// The capsule is only the coarse volume that stops the trace; the light
	// hitboxes inside it decide (C6). A bullet through the gap beside a body
	// flies on to whatever is behind it.
	for (int32 Pass = 0; Pass < 4; ++Pass)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, AuthoritativeOrigin, TraceEnd, ECC_Visibility, Params))
		{
			Result.ImpactPoint = TraceEnd;
			return Result;
		}
		const ACSCharacter* Victim = Cast<ACSCharacter>(Hit.GetActor());
		if (!Victim)
		{
			// Geometry, not a player.
			Result.bHit = true;
			Result.ImpactPoint = Hit.ImpactPoint;
			return Result;
		}
		ECSHitZone Zone = ECSHitZone::Torso;
		float Distance = 0.f;
		if (TraceHitboxes(Victim, AuthoritativeOrigin, Direction, Weapon->Range, Zone, Distance))
		{
			Result.bHit = true;
			Result.ImpactPoint = AuthoritativeOrigin + Direction * Distance;
			Result.Zone = Zone;
			// Identity comes from Fusion ownership, never from anything on the pawn.
			Result.VictimPlayerId = Victim->GetOwningPlayerId();
			Result.Damage = Weapon->ComputeDamage(Zone, Distance);
			return Result;
		}
		Params.AddIgnoredActor(Victim);
	}
	Result.ImpactPoint = TraceEnd;
	return Result;
}
