// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Weapons/CSGrenade.h"

#include "Audio/CSAudio.h"
#include "Audio/CSAudioSettings.h"
#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
#include "Components/DecalComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/CSAuthority.h"
#include "EngineUtils.h"
#include "FX/CSTransientFX.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Items/CSItemDefinition.h"
#include "Items/CSItemSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"

namespace
{
	const TSoftObjectPtr<UMaterialInterface> ScorchMaterial(
		FSoftObjectPath(TEXT("/Game/FX/Materials/M_CS_BulletHole.M_CS_BulletHole")));
}

ACSGrenade::ACSGrenade()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(5.f);
	Collision->SetCollisionProfileName(TEXT("Projectile"));
	Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	RootComponent = Collision;

	Model = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Model"));
	Model->SetupAttachment(Collision);
	Model->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Model->SetCastShadow(true);

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	Movement->bShouldBounce = true;
	Movement->Bounciness = 0.32f;
	Movement->Friction = 0.35f;
	Movement->BounceVelocityStopSimulatingThreshold = 35.f;
	Movement->ProjectileGravityScale = 1.f;
	Movement->bRotationFollowsVelocity = false;
	Movement->bAutoActivate = false;
}

void ACSGrenade::SetType(ECSGrenadeType InType)
{
	Type = InType;

	// The model is the item's world mesh (see bootstrap_v11.py / bootstrap_v20.py).
	const UCSItemSettings* Items = UCSItemSettings::Get();
	const TCHAR* ItemId = Type == ECSGrenadeType::Flash ? TEXT("flashbang") : TEXT("grenade");
	if (const UCSItemDefinition* Item = Items->GetItem(Items->FindItemIndex(ItemId)))
	{
		if (UStaticMesh* Mesh = Item->WorldMesh.LoadSynchronous())
		{
			Model->SetStaticMesh(Mesh);
		}
	}
}

void ACSGrenade::Launch(int32 InSerial, int32 InThrowerId, const FVector& Velocity, float FuseSeconds, ECSGrenadeType InType)
{
	Serial = InSerial;
	ThrowerId = InThrowerId;
	FuseEnd = GetWorld()->GetTimeSeconds() + FuseSeconds;
	SetType(InType);

	Movement->OnProjectileBounce.AddDynamic(this, &ACSGrenade::HandleBounce);
	Movement->Velocity = Velocity;
	Movement->Activate(true);
	Spin = FRotator(FMath::FRandRange(-500.f, -300.f), FMath::FRandRange(-120.f, 120.f), FMath::FRandRange(-200.f, 200.f));

	if (const ACSCharacter* Thrower = ACSMatchDirector::FindPawnForPlayer(this, ThrowerId))
	{
		Collision->IgnoreActorWhenMoving(const_cast<ACSCharacter*>(Thrower), true);
	}
}

void ACSGrenade::HandleBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity)
{
	Spin *= 0.5f;
	const double Now = GetWorld()->GetTimeSeconds();
	if (ImpactVelocity.Size() > 120.f && Now - LastBounceSound > 0.12)
	{
		LastBounceSound = Now;
		CSAudio::PlayAt(this, UCSAudioSettings::Get()->GrenadeBounce, GetActorLocation(),
			FMath::Clamp(ImpactVelocity.Size() / 900.f, 0.25f, 1.f), FMath::FRandRange(0.9f, 1.1f));
	}
}

void ACSGrenade::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bExploded)
	{
		return;
	}

	if (Movement->IsActive() && !Movement->Velocity.IsNearlyZero())
	{
		Model->AddLocalRotation(Spin * DeltaSeconds);
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < FuseEnd)
	{
		return;
	}
	if (UCSAuthority::IsGameAuthority(this))
	{
		if (ACSMatchDirector* Director = ACSMatchDirector::Get(this))
		{
			Director->ExplodeGrenade(this);
			return;
		}
	}
	// A copy that never hears its explosion cleans itself up.
	if (Now > FuseEnd + 3.0)
	{
		Destroy();
	}
}

void ACSGrenade::Explode(const FVector& Location)
{
	if (bExploded)
	{
		return;
	}
	bExploded = true;
	SetActorLocation(Location);
	Movement->StopMovementImmediately();
	Model->SetVisibility(false);

	UWorld* World = GetWorld();

	if (Type == ECSGrenadeType::Flash)
	{
		ExplodeFlash(Location);
		SetLifeSpan(0.5f);
		return;
	}

	// Fireball: a bright flash with a big light, then a rolling smoke ball.
	ACSTransientFX::FParams Flash;
	Flash.Color = FLinearColor(1.f, 0.55f, 0.18f);
	Flash.Intensity = 30.f;
	Flash.Lifetime = 0.16f;
	Flash.StartScale = FVector(0.6f);
	Flash.EndScale = FVector(3.2f);
	Flash.LightIntensity = 90000.f;
	Flash.LightRadius = 1400.f;
	ACSTransientFX::Spawn(World, FTransform(Location + FVector(0.f, 0.f, 25.f)), Flash);

	ACSTransientFX::FParams Core;
	Core.Shape = ECSFXShape::Star;
	Core.Color = FLinearColor(1.f, 0.8f, 0.4f);
	Core.Intensity = 40.f;
	Core.Lifetime = 0.09f;
	Core.StartScale = FVector(1.4f);
	Core.EndScale = FVector(2.4f);
	ACSTransientFX::Spawn(World, FTransform(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Location + FVector(0.f, 0.f, 30.f)), Core);

	for (int32 i = 0; i < 4; ++i)
	{
		ACSTransientFX::FParams Smoke;
		Smoke.bAdditive = false;
		Smoke.Color = FLinearColor(0.07f, 0.065f, 0.06f);
		Smoke.Opacity = 0.8f;
		Smoke.Lifetime = 1.8f + 0.4f * i;
		Smoke.StartScale = FVector(0.8f);
		Smoke.EndScale = FVector(3.4f + 0.6f * i);
		const FVector Offset(FMath::FRandRange(-60.f, 60.f), FMath::FRandRange(-60.f, 60.f), 30.f + 45.f * i);
		ACSTransientFX::Spawn(World, FTransform(Location + Offset), Smoke);
	}

	// Scorch mark on the floor.
	FHitResult Floor;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CSGrenadeScorch), false, this);
	if (World->LineTraceSingleByChannel(Floor, Location + FVector(0.f, 0.f, 50.f), Location - FVector(0.f, 0.f, 150.f), ECC_WorldStatic, Params))
	{
		if (UMaterialInterface* Decal = ScorchMaterial.LoadSynchronous())
		{
			if (UDecalComponent* Scorch = UGameplayStatics::SpawnDecalAtLocation(World, Decal, FVector(30.f, 150.f, 150.f),
					Floor.ImpactPoint, (-Floor.ImpactNormal).Rotation(), 25.f))
			{
				Scorch->SetFadeOut(20.f, 5.f, false);
			}
		}
	}

	CSAudio::PlayAt(World, UCSAudioSettings::Get()->GrenadeExplode, Location, 1.f, FMath::FRandRange(0.95f, 1.05f));

	// Nearby cameras shake (the local player only).
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			const float Distance = FVector::Dist(Pawn->GetActorLocation(), Location);
			if (ACSCharacter* CSPawn = Cast<ACSCharacter>(Pawn))
			{
				CSPawn->AddExplosionShake(FMath::Clamp(1.f - Distance / 1500.f, 0.f, 1.f));
			}
		}
	}

	SetLifeSpan(0.5f);
}

void ACSGrenade::ExplodeFlash(const FVector& Location)
{
	UWorld* World = GetWorld();

	// A white-hot pop: very bright, very short, with a light that floods the room.
	ACSTransientFX::FParams Pop;
	Pop.Color = FLinearColor(1.f, 0.97f, 0.9f);
	Pop.Intensity = 80.f;
	Pop.Lifetime = 0.12f;
	Pop.StartScale = FVector(0.4f);
	Pop.EndScale = FVector(2.2f);
	Pop.LightIntensity = 400000.f;
	Pop.LightRadius = 2600.f;
	ACSTransientFX::Spawn(World, FTransform(Location + FVector(0.f, 0.f, 15.f)), Pop);

	ACSTransientFX::FParams Wisp;
	Wisp.bAdditive = false;
	Wisp.Color = FLinearColor(0.55f, 0.55f, 0.56f);
	Wisp.Opacity = 0.35f;
	Wisp.Lifetime = 1.4f;
	Wisp.StartScale = FVector(0.3f);
	Wisp.EndScale = FVector(1.4f);
	ACSTransientFX::Spawn(World, FTransform(Location + FVector(0.f, 0.f, 20.f)), Wisp);

	CSAudio::PlayAt(World, UCSAudioSettings::Get()->FlashbangExplode, Location, 1.f, FMath::FRandRange(0.97f, 1.03f));

	// Blind the local player according to what they can see of it. Every peer
	// decides this for its own camera; the authority separately blinds bots.
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (ACSCharacter* Viewer = Cast<ACSCharacter>(PC->GetPawn()))
		{
			FVector Eye;
			FRotator ViewRotation;
			PC->GetPlayerViewPoint(Eye, ViewRotation);
			float Seconds = 0.f;
			const float Strength = ACSMatchDirector::ComputeFlashStrength(World, Location, Eye, ViewRotation.Vector(), this, Seconds);
			if (Strength > 0.f && Viewer->IsAliveAuthoritative())
			{
				Viewer->ApplyFlash(Strength, Seconds);
			}
		}
	}
}

ACSGrenade* ACSGrenade::FindBySerial(const UObject* WorldContextObject, int32 InSerial, bool bIncludeExploded)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ACSGrenade> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (It->Serial == InSerial && (bIncludeExploded || !It->bExploded))
		{
			return *It;
		}
	}
	return nullptr;
}
