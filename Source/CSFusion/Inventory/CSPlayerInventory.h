// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// One player's authoritative inventory.
//
// Where it lives, and why:
//   * Not on the pawn or PlayerState - the player owns those under Fusion
//     (PlayerAttached), so a modified client could write "I now have an
//     AK-47" and it would replicate.
//   * Not inside ACSMatchDirector - Fusion caps a networked array at 64
//     elements, and 16 players x 6 slots is 96.
//   * So: one actor per player, owned by the Master Client
//     (EFusionObjectOwnerFlags::MasterClient). Only the authority can write
//     it, and ownership follows master migration automatically.
//
// The authority spawns it when a player is registered and destroys it when
// the player leaves. Every mutation is CS_AUTHORITY_ONLY.
//
// The starter pistol never appears here. It is always available and is not
// an inventory item. EquippedSlot == INDEX_NONE means "holding the starter".

#pragma once

#include "CoreMinimal.h"
#include "Core/CSFusionCompat.h"
#include "GameFramework/Actor.h"

// Required because Slots uses the FusionArraySize meta tag.
#include "CSPlayerInventory.fusion.h"
#include "CSPlayerInventory.generated.h"

class UCSItemDefinition;
class UCSWeaponDefinition;
class UFusionActorComponent;

/**
 * One inventory slot. No FUSION_BODY and no Replicated on members - see the
 * notes on FCSPlayerCombatRecord: the plugin does not emit FUSION_BODY for
 * structs, UHT rejects Replicated on struct members, and Fusion serialises
 * every UPROPERTY of a nested struct anyway.
 */
USTRUCT(BlueprintType)
struct CSFUSION_API FCSInventorySlot
{
	GENERATED_BODY()

	/** Index into UCSItemSettings::Items. INDEX_NONE means empty. */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Inventory")
	int32 ItemIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "CS|Inventory")
	int32 Count = 0;

	/** Rounds loaded, for weapon items. Travels with the weapon when dropped. */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Inventory")
	int32 AmmoInMag = 0;

	bool IsEmpty() const { return ItemIndex == INDEX_NONE || Count <= 0; }

	void Clear()
	{
		ItemIndex = INDEX_NONE;
		Count = 0;
		AmmoInMag = 0;
	}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCSInventoryChanged);

UCLASS()
class CSFUSION_API ACSPlayerInventory : public AActor
{
	GENERATED_BODY()
	FUSION_BODY();

public:
	ACSPlayerInventory();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Finds the inventory of a player. Null if none (yet). */
	static ACSPlayerInventory* Find(const UObject* WorldContextObject, int32 PlayerId);

	// --- Reads (every peer) ------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "CS|Inventory")
	int32 GetOwnerPlayerId() const { return OwnerPlayerId; }

	UFUNCTION(BlueprintPure, Category = "CS|Inventory")
	const TArray<FCSInventorySlot>& GetSlots() const { return Slots; }

	UFUNCTION(BlueprintPure, Category = "CS|Inventory")
	int32 GetEquippedSlot() const { return EquippedSlot; }

	UFUNCTION(BlueprintPure, Category = "CS|Inventory")
	bool GetSlot(int32 Slot, FCSInventorySlot& OutSlot) const;

	/** Total of an item across all stacks. Reserve ammo is read this way. */
	UFUNCTION(BlueprintPure, Category = "CS|Inventory")
	int32 CountItem(int32 ItemIndex) const;

	/** Weapon in the equipped slot, or null when holding the starter pistol. */
	const UCSWeaponDefinition* GetEquippedWeapon() const;

	/** Item definition in a slot, or null. */
	const UCSItemDefinition* GetItemInSlot(int32 Slot) const;

	/** True if at least part of this item would fit. */
	bool CanAccept(int32 ItemIndex, int32 Count) const;

	// --- Authority-only writes ---------------------------------------------

	void InitializeFor(int32 PlayerId);

	/**
	 * Adds an item: tops up existing stacks first, then fills empty slots.
	 * Returns how many did NOT fit (0 = everything was added).
	 */
	int32 AddItem(int32 ItemIndex, int32 Count, int32 AmmoInMag);

	/** Takes up to Count from a slot. Returns what was actually removed. */
	FCSInventorySlot RemoveFromSlot(int32 Slot, int32 Count);

	/** Consumes up to Count of an item across stacks. Returns amount consumed. */
	int32 ConsumeItem(int32 ItemIndex, int32 Count);

	/** INDEX_NONE equips the starter pistol. Non-weapon slots are refused. */
	bool SetEquippedSlot(int32 Slot);

	/** Changes the loaded rounds of the weapon in a slot. */
	void SetSlotAmmo(int32 Slot, int32 AmmoInMag);

	/** Empties everything. Stage 4 drops the contents as loot first. */
	TArray<FCSInventorySlot> TakeAll();

	UPROPERTY(BlueprintAssignable, Category = "CS|Inventory")
	FCSInventoryChanged OnInventoryChanged;

protected:
	void MarkChanged();

	UFUNCTION()
	void OnRep_Inventory();

	UPROPERTY(Replicated)
	int32 OwnerPlayerId = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Inventory, meta = (FusionArraySize = 8))
	TArray<FCSInventorySlot> Slots;

	UPROPERTY(ReplicatedUsing = OnRep_Inventory)
	int32 EquippedSlot = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category = "CS|Components")
	TObjectPtr<UFusionActorComponent> FusionActor;
};
