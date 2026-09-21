// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Pickups/CSPickupSpawnPoint.h"

#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"
#include "Core/CSAuthority.h"
#include "Core/CSLog.h"
#include "Engine/World.h"
#include "Items/CSItemDefinition.h"
#include "Items/CSItemSettings.h"
#include "Pickups/CSWorldPickup.h"
#include "Weapons/CSWeaponDefinition.h"

ACSPickupSpawnPoint::ACSPickupSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

#if WITH_EDITORONLY_DATA
	Sprite = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (Sprite)
	{
		Sprite->SetupAttachment(GetRootComponent());
	}
#endif
}

ACSWorldPickup* ACSPickupSpawnPoint::SpawnPickup() const
{
	CS_AUTHORITY_ONLY_RET(this, nullptr);

	const UCSItemSettings* Settings = UCSItemSettings::Get();
	const int32 ItemIndex = Settings->FindItemIndex(ItemId);
	const UCSItemDefinition* Item = Settings->GetItem(ItemIndex);
	if (!Item)
	{
		UE_LOG(LogCSInventory, Warning, TEXT("%s: unknown item '%s' - not in CS Items registry."),
			*GetName(), *ItemId.ToString());
		return nullptr;
	}

	// v1.1: weapons are bought in the shop, never lying around the map.
	// -mapweapons brings them back for the self-tests that pick weapons up.
	if (Item->IsWeapon() && !FParse::Param(FCommandLine::Get(), TEXT("mapweapons")))
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ACSWorldPickup* Pickup = GetWorld()->SpawnActor<ACSWorldPickup>(
		ACSWorldPickup::StaticClass(), GetActorLocation(), GetActorRotation(), Params);
	if (!Pickup)
	{
		return nullptr;
	}

	// A fresh weapon comes with a full magazine.
	int32 Ammo = 0;
	if (Item->IsWeapon())
	{
		if (const UCSWeaponDefinition* Weapon = Item->Weapon.LoadSynchronous())
		{
			Ammo = Weapon->MagazineSize;
		}
	}

	Pickup->InitializeItem(ItemIndex, Count > 0 ? Count : Item->DefaultPickupCount, Ammo, /*bDropped*/ false);
	return Pickup;
}
