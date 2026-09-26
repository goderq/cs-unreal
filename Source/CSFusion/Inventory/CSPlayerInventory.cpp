// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Inventory/CSPlayerInventory.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CSPlayerInventory.fusion)

#include "Core/CSAuthority.h"
#include "Core/CSLog.h"
#include "Core/CSWorldCache.h"
#include "EngineUtils.h"
#include "Items/CSItemDefinition.h"
#include "Items/CSItemSettings.h"
#include "Net/UnrealNetwork.h"
#include "Weapons/CSWeaponDefinition.h"

ACSPlayerInventory::ACSPlayerInventory()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);

	FusionActor = CreateDefaultSubobject<UFusionActorComponent>(TEXT("FusionActor"));
	FusionActor->Ownership = EFusionObjectOwnerFlags::MasterClient;
}

void ACSPlayerInventory::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACSPlayerInventory, OwnerPlayerId);
	DOREPLIFETIME(ACSPlayerInventory, Slots);
	DOREPLIFETIME(ACSPlayerInventory, EquippedSlot);
}

ACSPlayerInventory* ACSPlayerInventory::Find(const UObject* WorldContextObject, int32 PlayerId)
{
	if (PlayerId == 0)
	{
		return nullptr;
	}

	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World)
	{
		return nullptr;
	}

	UCSWorldCache* Cache = UCSWorldCache::Get(World);
	return UCSWorldCache::FindOwned<ACSPlayerInventory>(World, Cache ? &Cache->Inventories : nullptr, PlayerId,
		[](const ACSPlayerInventory* Inventory) { return Inventory->OwnerPlayerId; });
}

int32 ACSPlayerInventory::SlotForItem(const UCSItemDefinition* Item)
{
	if (!Item)
	{
		return INDEX_NONE;
	}
	switch (Item->LoadoutRole)
	{
	case ECSLoadoutRole::Primary:	return CSLoadout::Primary;
	case ECSLoadoutRole::Pistol:	return CSLoadout::Pistol;
	case ECSLoadoutRole::Knife:		return CSLoadout::Knife;
	case ECSLoadoutRole::Frag:		return CSLoadout::Frag;
	case ECSLoadoutRole::Flash:		return CSLoadout::Flash;
	default:						return INDEX_NONE;
	}
}

// ---------------------------------------------------------------------------
// Reads
// ---------------------------------------------------------------------------

bool ACSPlayerInventory::GetSlot(int32 Slot, FCSInventorySlot& OutSlot) const
{
	if (!Slots.IsValidIndex(Slot))
	{
		return false;
	}
	OutSlot = Slots[Slot];
	return true;
}

const UCSItemDefinition* ACSPlayerInventory::GetItemInSlot(int32 Slot) const
{
	if (!Slots.IsValidIndex(Slot) || Slots[Slot].IsEmpty())
	{
		return nullptr;
	}
	return UCSItemSettings::Get()->GetItem(Slots[Slot].ItemIndex);
}

const UCSWeaponDefinition* ACSPlayerInventory::GetEquippedWeapon() const
{
	const UCSItemDefinition* Item = GetItemInSlot(EquippedSlot);
	return Item ? Item->Weapon.LoadSynchronous() : nullptr;
}

int32 ACSPlayerInventory::GetBestWeaponSlot() const
{
	for (const int32 Slot : { CSLoadout::Primary, CSLoadout::Pistol, CSLoadout::Knife })
	{
		if (HasItemInSlot(Slot))
		{
			return Slot;
		}
	}
	return CSLoadout::Knife;
}

// ---------------------------------------------------------------------------
// Authority-only writes
// ---------------------------------------------------------------------------

void ACSPlayerInventory::InitializeFor(int32 PlayerId)
{
	CS_AUTHORITY_ONLY(this);

	OwnerPlayerId = PlayerId;
	Slots.SetNum(CSLoadout::NumSlots);
	for (FCSInventorySlot& Slot : Slots)
	{
		Slot.Clear();
	}
	EquippedSlot = CSLoadout::Knife;
	MarkChanged();
}

int32 ACSPlayerInventory::AddItem(int32 ItemIndex, int32 Count, int32 AmmoInMag, int32 Reserve)
{
	CS_AUTHORITY_ONLY_RET(this, Count);

	const UCSItemDefinition* Item = UCSItemSettings::Get()->GetItem(ItemIndex);
	const int32 Slot = SlotForItem(Item);
	if (!Item || Count <= 0 || !Slots.IsValidIndex(Slot))
	{
		return Count;
	}

	FCSInventorySlot& Target = Slots[Slot];

	// Grenades stack in their own slot; everything else is one per slot and
	// only goes into an empty one.
	if (CSLoadout::IsGrenadeSlot(Slot))
	{
		if (!Target.IsEmpty() && Target.ItemIndex != ItemIndex)
		{
			return Count;
		}
		const int32 Room = Item->GetMaxStack() - (Target.IsEmpty() ? 0 : Target.Count);
		const int32 Added = FMath::Clamp(Count, 0, Room);
		if (Added <= 0)
		{
			return Count;
		}
		Target.ItemIndex = ItemIndex;
		Target.Count = (Target.Count > 0 ? Target.Count : 0) + Added;
		MarkChanged();
		return Count - Added;
	}

	if (!Target.IsEmpty())
	{
		return Count;
	}

	const UCSWeaponDefinition* Weapon = Item->Weapon.LoadSynchronous();
	const bool bFirearm = Weapon && Weapon->IsFirearm();
	Target.ItemIndex = ItemIndex;
	Target.Count = 1;
	Target.AmmoInMag = bFirearm ? FMath::Clamp(AmmoInMag, 0, Weapon->MagazineSize) : 0;
	Target.Reserve = bFirearm ? FMath::Clamp(Reserve, 0, Weapon->ReserveAmmo) : 0;

	UE_LOG(LogCSInventory, Log, TEXT("Player %d slot %d <- %s (%d / %d)"),
		OwnerPlayerId, Slot + 1, *Item->ItemId.ToString(), Target.AmmoInMag, Target.Reserve);
	MarkChanged();
	return Count - 1;
}

FCSInventorySlot ACSPlayerInventory::RemoveFromSlot(int32 Slot, int32 Count)
{
	FCSInventorySlot Removed;
	CS_AUTHORITY_ONLY_RET(this, Removed);

	if (!Slots.IsValidIndex(Slot) || Slots[Slot].IsEmpty() || Count <= 0)
	{
		return Removed;
	}

	FCSInventorySlot& Source = Slots[Slot];
	const int32 Taken = FMath::Min(Count, Source.Count);

	Removed = Source;
	Removed.Count = Taken;

	Source.Count -= Taken;
	if (Source.Count <= 0)
	{
		Source.Clear();
		if (EquippedSlot == Slot)
		{
			// What was in hand is gone: the next best thing comes up.
			EquippedSlot = GetBestWeaponSlot();
		}
	}

	MarkChanged();
	return Removed;
}

bool ACSPlayerInventory::SetEquippedSlot(int32 Slot)
{
	CS_AUTHORITY_ONLY_RET(this, false);

	if (!HasItemInSlot(Slot))
	{
		return false;
	}
	if (EquippedSlot != Slot)
	{
		EquippedSlot = Slot;
		MarkChanged();
	}
	return true;
}

void ACSPlayerInventory::SetSlotAmmo(int32 Slot, int32 AmmoInMag, int32 Reserve)
{
	CS_AUTHORITY_ONLY(this);

	if (HasItemInSlot(Slot))
	{
		Slots[Slot].AmmoInMag = FMath::Max(0, AmmoInMag);
		Slots[Slot].Reserve = FMath::Max(0, Reserve);
		MarkChanged();
	}
}

TArray<FCSInventorySlot> ACSPlayerInventory::TakeAll()
{
	TArray<FCSInventorySlot> Taken;
	CS_AUTHORITY_ONLY_RET(this, Taken);

	for (FCSInventorySlot& Slot : Slots)
	{
		if (!Slot.IsEmpty())
		{
			Taken.Add(Slot);
			Slot.Clear();
		}
	}
	EquippedSlot = CSLoadout::Knife;
	MarkChanged();
	return Taken;
}

void ACSPlayerInventory::MarkChanged()
{
	// The writer does not receive its own OnRep, so raise the event locally.
	OnInventoryChanged.Broadcast();
}

void ACSPlayerInventory::OnRep_Inventory()
{
	OnInventoryChanged.Broadcast();
}
