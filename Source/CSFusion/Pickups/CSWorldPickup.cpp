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
#include "UObject/ConstructorHelpers.h"

ACSWorldPickup::ACSWorldPickup()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
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
	ExpiresAtNetworkTime = (bInDropped && Lifetime > 0.f)
		? UCSAuthority::GetNetworkTimeSeconds(this) + Lifetime
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

	// Cosmetic spin and bob, local on every peer.
	VisualTime += DeltaSeconds;
	if (Mesh)
	{
		Mesh->AddLocalRotation(FRotator(0.f, 90.f * DeltaSeconds, 0.f));
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

	if (UStaticMesh* WorldMesh = Item->WorldMesh.LoadSynchronous())
	{
		Mesh->SetStaticMesh(WorldMesh);
	}
	Mesh->SetRelativeScale3D(Item->WorldMeshScale);

	// Tint the engine's basic shape material so item types read at a glance
	// until real meshes arrive in Stage 6.
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
