// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Pickups/CSWorldPickup.h"

#include "Components/StaticMeshComponent.h"
#include "Core/CSAuthority.h"
#include "Core/CSLog.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Items/CSItemDefinition.h"
#include "Items/CSItemSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Weapons/CSWeaponDefinition.h"
#include "Weapons/CSWeaponPresentation.h"
#include "UObject/ConstructorHelpers.h"

ACSWorldPickup::ACSWorldPickup()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);

	// Separate root so the mesh can be animated (fly-out arc, bob) without
	// moving the replicated actor transform.
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	// Pickups never block shots, movement or the camera.
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetMobility(EComponentMobility::Movable);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (DefaultMesh.Succeeded())
	{
		Mesh->SetStaticMesh(DefaultMesh.Object);
		Mesh->SetRelativeScale3D(FVector(0.3f));
	}

	FusionActor = CreateDefaultSubobject<UFusionActorComponent>(TEXT("FusionActor"));
	FusionActor->Ownership = EFusionObjectOwnerFlags::MasterClient;
}

void ACSWorldPickup::BeginPlay()
{
	Super::BeginPlay();
	ApplyVisuals();
}

void ACSWorldPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACSWorldPickup, ItemIndex);
	DOREPLIFETIME(ACSWorldPickup, Count);
	DOREPLIFETIME(ACSWorldPickup, AmmoInMag);
	DOREPLIFETIME(ACSWorldPickup, ExpiresAtNetworkTime);
	DOREPLIFETIME(ACSWorldPickup, SpawnNetworkTime);
	DOREPLIFETIME(ACSWorldPickup, DropOrigin);
}

void ACSWorldPickup::SetDropOrigin(const FVector& Origin)
{
	CS_AUTHORITY_ONLY(this);
	DropOrigin = Origin;
}

void ACSWorldPickup::MakeRoomForDrops(const UObject* WorldContextObject, int32 Incoming)
{
	CS_AUTHORITY_ONLY(WorldContextObject);

	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World)
	{
		return;
	}

	const int32 Cap = UCSItemSettings::Get()->MaxWorldPickups;
	int32 Excess = CountAlive(WorldContextObject) + Incoming - Cap;
	if (Excess <= 0)
	{
		return;
	}

	// Oldest dropped items go first; map loot is never evicted.
	TArray<ACSWorldPickup*> Dropped;
	for (TActorIterator<ACSWorldPickup> It(World); It; ++It)
	{
		if (!It->IsActorBeingDestroyed() && It->IsDropped() && It->IsAvailable())
		{
			Dropped.Add(*It);
		}
	}
	Dropped.Sort([](const ACSWorldPickup& A, const ACSWorldPickup& B)
	{
		return A.GetSpawnNetworkTime() < B.GetSpawnNetworkTime();
	});

	for (ACSWorldPickup* Pickup : Dropped)
	{
		if (Excess-- <= 0)
		{
			break;
		}
		UE_LOG(LogCSInventory, Log, TEXT("Pickup cap reached: evicting oldest drop %s."), *Pickup->GetName());
		Pickup->Claim();
	}
}

const UCSItemDefinition* ACSWorldPickup::GetItemDefinition() const
{
	return UCSItemSettings::Get()->GetItem(ItemIndex);
}

FText ACSWorldPickup::GetPromptName() const
{
	const UCSItemDefinition* Item = GetItemDefinition();
	if (!Item)
	{
		return FText::GetEmpty();
	}
	if (Item->bStackable && Count > 1)
	{
		return FText::Format(NSLOCTEXT("CS", "PickupStack", "{0} x{1}"), Item->DisplayName, FText::AsNumber(Count));
	}
	return Item->DisplayName;
}

void ACSWorldPickup::InitializeItem(int32 InItemIndex, int32 InCount, int32 InAmmoInMag, bool bInDropped)
{
	CS_AUTHORITY_ONLY(this);

	ItemIndex = InItemIndex;
	Count = FMath::Max(1, InCount);
	AmmoInMag = FMath::Max(0, InAmmoInMag);

	const float Lifetime = UCSItemSettings::Get()->DroppedItemLifetimeSeconds;
	SpawnNetworkTime = UCSAuthority::GetNetworkTimeSeconds(this);
	ExpiresAtNetworkTime = (bInDropped && Lifetime > 0.f)
		? SpawnNetworkTime + Lifetime
		: 0.0;

	ApplyVisuals();
}

bool ACSWorldPickup::Claim()
{
	CS_AUTHORITY_ONLY_RET(this, false);

	if (!IsAvailable())
	{
		return false;
	}

	bClaimed = true;
	Destroy();
	return true;
}

void ACSWorldPickup::SetRemainingCount(int32 NewCount)
{
	CS_AUTHORITY_ONLY(this);
	Count = FMath::Max(0, NewCount);
}

int32 ACSWorldPickup::CountAlive(const UObject* WorldContextObject)
{
	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	int32 Alive = 0;
	if (World)
	{
		for (TActorIterator<ACSWorldPickup> It(World); It; ++It)
		{
			if (!It->IsActorBeingDestroyed())
			{
				++Alive;
			}
		}
	}
	return Alive;
}

void ACSWorldPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Cosmetic, local on every peer: a short fly-out arc from DropOrigin for
	// freshly dropped loot, then a gentle spin and bob.
	VisualTime += DeltaSeconds;
	if (Mesh)
	{
		// Spin about the world vertical. A weapon model lies on its side
		// (ModelLie) and is centred by ModelCentre, both kept while it turns.
		SpinYaw = FMath::Fmod(SpinYaw + 90.f * DeltaSeconds, 360.f);
		const FQuat Spin(FRotator(0.f, SpinYaw, 0.f));
		Mesh->SetRelativeRotation(Spin * ModelLie);

		constexpr float ArcSeconds = 0.45f;
		const double Age = UCSAuthority::GetNetworkTimeSeconds(this) - SpawnNetworkTime;
		FVector Offset(0.f, 0.f, 6.f * FMath::Sin(VisualTime * 2.5f));

		if (!DropOrigin.IsZero() && Age >= 0.0 && Age < ArcSeconds)
		{
			const float Alpha = static_cast<float>(Age / ArcSeconds);
			const FVector FromLocal = DropOrigin - GetActorLocation();
			const float Hop = 70.f * FMath::Sin(Alpha * PI);
			Offset = FMath::Lerp(FromLocal, FVector::ZeroVector, Alpha) + FVector(0.f, 0.f, Hop);
		}

		Mesh->SetRelativeLocation(Offset + Spin.RotateVector(ModelCentre));
	}

	// Dropped items expire so a long match cannot accumulate loot forever.
	if (ExpiresAtNetworkTime > 0.0 && UCSAuthority::IsGameAuthority(this) &&
		UCSAuthority::GetNetworkTimeSeconds(this) >= ExpiresAtNetworkTime && !bClaimed)
	{
		UE_LOG(LogCSInventory, Verbose, TEXT("Pickup %s expired."), *GetName());
		bClaimed = true;
		Destroy();
	}
}

void ACSWorldPickup::OnRep_Item()
{
	ApplyVisuals();
}

void ACSWorldPickup::ApplyVisuals()
{
	const UCSItemDefinition* Item = GetItemDefinition();
	if (!Item || !Mesh)
	{
		return;
	}

	// v1.0: weapons on the ground show the same model as in the hands.
	if (Item->IsWeapon())
	{
		const FCSWeaponModel* Model = UCSWeaponPresentationSettings::Find(Item->Weapon.LoadSynchronous());
		if (UStaticMesh* ModelMesh = Model ? Model->Mesh.LoadSynchronous() : nullptr)
		{
			Mesh->SetStaticMesh(ModelMesh);
			// Centre the model on the pickup: offset by its bounds centre.
			ModelLie = FQuat(Model->PickupRotation);
			ModelCentre = -ModelLie.RotateVector(ModelMesh->GetBounds().Origin * Model->Scale);
			Mesh->SetRelativeScale3D(FVector(Model->Scale));
			return;
		}
	}

	UStaticMesh* WorldMesh = Item->WorldMesh.LoadSynchronous();
	if (WorldMesh)
	{
		Mesh->SetStaticMesh(WorldMesh);
	}
	Mesh->SetRelativeScale3D(Item->WorldMeshScale);

	// Weapons show their real, textured model. Other items use simple shapes
	// tinted by type so they read at a glance.
	if (WorldMesh && Item->IsWeapon())
	{
		return;
	}
	if (UMaterialInterface* Base = Mesh->GetMaterial(0))
	{
		UMaterialInstanceDynamic* Tinted = Cast<UMaterialInstanceDynamic>(Base);
		if (!Tinted)
		{
			Tinted = UMaterialInstanceDynamic::Create(Base, this);
			Mesh->SetMaterial(0, Tinted);
		}
		Tinted->SetVectorParameterValue(TEXT("Color"), Item->PlaceholderColor);
	}
}
