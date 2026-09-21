// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Inventory/CSPlayerInventory.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CSPlayerInventory.fusion)

#include "Core/CSAuthority.h"
#include "Core/CSLog.h"
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

	for (TActorIterator<ACSPlayerInventory> It(World); It; ++It)
	{
		if (It->OwnerPlayerId == PlayerId && !It->IsActorBeingDestroyed())
		{
			return *It;
		}
	}
	return nullptr;
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

int32 ACSPlayerInventory::CountItem(int32 ItemIndex) const
{
	int32 Total = 0;
	for (const FCSInventorySlot& Slot : Slots)
	{
		if (Slot.ItemIndex == ItemIndex)
		{
			Total += Slot.Count;
		}
	}
	return Total;
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
	return (Item && Item->IsWeapon()) ? Item->Weapon.LoadSynchronous() : nullptr;
}

bool ACSPlayerInventory::CanAccept(int32 ItemIndex, int32 Count) const
{
	const UCSItemDefinition* Item = UCSItemSettings::Get()->GetItem(ItemIndex);
	if (!Item || Count <= 0)
	{
		return false;
	}

	for (const FCSInventorySlot& Slot : Slots)
	{
		if (Slot.IsEmpty())
		{
			return true;
		}
		if (Slot.ItemIndex == ItemIndex && Slot.Count < Item->GetMaxStack())
		{
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// Authority-only writes
// ---------------------------------------------------------------------------

void ACSPlayerInventory::InitializeFor(int32 PlayerId)
{
	CS_AUTHORITY_ONLY(this);

	OwnerPlayerId = PlayerId;
	Slots.SetNum(UCSItemSettings::Get()->InventorySlots);
	for (FCSInventorySlot& Slot : Slots)
	{
		Slot.Clear();
	}
	EquippedSlot = INDEX_NONE;
	MarkChanged();
}

int32 ACSPlayerInventory::AddItem(int32 ItemIndex, int32 Count, int32 AmmoInMag)
{
	CS_AUTHORITY_ONLY_RET(this, Count);

	const UCSItemDefinition* Item = UCSItemSettings::Get()->GetItem(ItemIndex);
	if (!Item || Count <= 0)
	{
		return Count;
	}

	const int32 MaxStack = Item->GetMaxStack();
	int32 Remaining = Count;

	// Top up existing stacks first.
	if (Item->bStackable)
	{
		for (FCSInventorySlot& Slot : Slots)
		{
			if (Remaining <= 0)
			{
				break;
			}
			if (Slot.ItemIndex == ItemIndex && Slot.Count < MaxStack)
			{
				const int32 Added = FMath::Min(MaxStack - Slot.Count, Remaining);
				Slot.Count += Added;
				Remaining -= Added;
			}
		}
	}

	// Then empty slots.
	for (FCSInventorySlot& Slot : Slots)
	{
		if (Remaining <= 0)
		{
			break;
		}
		if (Slot.IsEmpty())
		{
			const int32 Added = FMath::Min(MaxStack, Remaining);
			Slot.ItemIndex = ItemIndex;
			Slot.Count = Added;
			Slot.AmmoInMag = Item->IsWeapon() ? AmmoInMag : 0;
			Remaining -= Added;
		}
	}

	if (Remaining != Count)
	{
		UE_LOG(LogCSInventory, Log, TEXT("Player %d +%d x %s (%d did not fit)"),
			OwnerPlayerId, Count - Remaining, *Item->ItemId.ToString(), Remaining);
		MarkChanged();
	}
	return Remaining;
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

	Removed.ItemIndex = Source.ItemIndex;
	Removed.Count = Taken;
	Removed.AmmoInMag = Source.AmmoInMag;

	Source.Count -= Taken;
	if (Source.Count <= 0)
	{
		Source.Clear();
		if (EquippedSlot == Slot)
		{
			// The weapon in hand is gone; fall back to the starter pistol,
			// which by design can never be lost.
			EquippedSlot = INDEX_NONE;
		}
	}

	MarkChanged();
	return Removed;
}

int32 ACSPlayerInventory::ConsumeItem(int32 ItemIndex, int32 Count)
{
	CS_AUTHORITY_ONLY_RET(this, 0);

	int32 Consumed = 0;
	for (int32 i = 0; i < Slots.Num() && Consumed < Count; ++i)
	{
		if (Slots[i].ItemIndex == ItemIndex)
		{
			Consumed += RemoveFromSlot(i, Count - Consumed).Count;
		}
	}
	return Consumed;
}

bool ACSPlayerInventory::SetEquippedSlot(int32 Slot)
{
	CS_AUTHORITY_ONLY_RET(this, false);

	if (Slot != INDEX_NONE)
	{
		const UCSItemDefinition* Item = GetItemInSlot(Slot);
		if (!Item || !Item->IsWeapon())
		{
			return false;
		}
	}

	if (EquippedSlot != Slot)
	{
		EquippedSlot = Slot;
		MarkChanged();
	}
	return true;
}

void ACSPlayerInventory::SetSlotAmmo(int32 Slot, int32 AmmoInMag)
{
	CS_AUTHORITY_ONLY(this);

	if (Slots.IsValidIndex(Slot) && !Slots[Slot].IsEmpty())
	{
		Slots[Slot].AmmoInMag = FMath::Max(0, AmmoInMag);
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
	EquippedSlot = INDEX_NONE;
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
