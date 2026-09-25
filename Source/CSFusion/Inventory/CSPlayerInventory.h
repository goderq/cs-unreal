// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// One player's authoritative loadout (v2.0: replaces the free-form inventory).
//
// Five fixed slots, one per key:
//   1  Primary   rifle, SMG, shotgun or sniper - bought, droppable
//   2  Pistol    everybody spawns with one - droppable
//   3  Knife     always there, can never be dropped
//   4  Frag      HE grenades
//   5  Flash     flashbangs
// There is no starter pistol any more and no bag of loose items: ammunition
// lives with the weapon it belongs to (rounds in the magazine plus a reserve),
// armor is worn the moment it is bought.
//
// Where it lives, and why:
//   * Not on the pawn or PlayerState - the player owns those under Fusion
//     (PlayerAttached), so a modified client could write "I now have an
//     AK-47" and it would replicate.
//   * Not inside ACSMatchDirector - Fusion caps a networked array at 64
//     elements, and 16 players x 5 slots is 80.
//   * So: one actor per player, owned by the Master Client
//     (EFusionObjectOwnerFlags::MasterClient). Only the authority can write
//     it, and ownership follows master migration automatically.
//
// The authority spawns it when a player is registered and destroys it when
// the player leaves. Every mutation is CS_AUTHORITY_ONLY.

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

/** The five loadout slots; the value is the slot index and key minus one. */
namespace CSLoadout
{
	constexpr int32 Primary = 0;
	constexpr int32 Pistol = 1;
	constexpr int32 Knife = 2;
	constexpr int32 Frag = 3;
	constexpr int32 Flash = 4;
	constexpr int32 NumSlots = 5;

	/** Primary and pistol can be dropped; the knife and grenades cannot. */
	inline bool IsDroppable(int32 Slot) { return Slot == Primary || Slot == Pistol; }
	inline bool IsGrenadeSlot(int32 Slot) { return Slot == Frag || Slot == Flash; }
}

/**
 * One loadout slot. No FUSION_BODY and no Replicated on members - see the
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

	/** 1 for weapons and the knife; how many for grenades. */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Inventory")
	int32 Count = 0;

	/** Rounds loaded, for firearms. Travels with the weapon when dropped. */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Inventory")
	int32 AmmoInMag = 0;

	/** Spare rounds for this weapon. Travels with it when dropped. */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Inventory")
	int32 Reserve = 0;

	bool IsEmpty() const { return ItemIndex == INDEX_NONE || Count <= 0; }

	void Clear()
	{
		ItemIndex = INDEX_NONE;
		Count = 0;
		AmmoInMag = 0;
		Reserve = 0;
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

	/** Finds the loadout of a player. Null if none (yet). */
	static ACSPlayerInventory* Find(const UObject* WorldContextObject, int32 PlayerId);

	/** Which slot an item belongs in (INDEX_NONE for items that are not carried, e.g. armor). */
	static int32 SlotForItem(const UCSItemDefinition* Item);

	// --- Reads (every peer) ------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "CS|Inventory")
	int32 GetOwnerPlayerId() const { return OwnerPlayerId; }

	UFUNCTION(BlueprintPure, Category = "CS|Inventory")
	const TArray<FCSInventorySlot>& GetSlots() const { return Slots; }

	UFUNCTION(BlueprintPure, Category = "CS|Inventory")
	int32 GetEquippedSlot() const { return EquippedSlot; }

	UFUNCTION(BlueprintPure, Category = "CS|Inventory")
	bool GetSlot(int32 Slot, FCSInventorySlot& OutSlot) const;

	bool HasItemInSlot(int32 Slot) const { return Slots.IsValidIndex(Slot) && !Slots[Slot].IsEmpty(); }

	/** Weapon (firearm, knife or grenade stand-in) in the equipped slot, or null. */
	const UCSWeaponDefinition* GetEquippedWeapon() const;

	/** Item definition in a slot, or null. */
	const UCSItemDefinition* GetItemInSlot(int32 Slot) const;

	/** The best slot to hold after losing the one in hand: primary, pistol, knife. */
	int32 GetBestWeaponSlot() const;

	// --- Authority-only writes ---------------------------------------------

	void InitializeFor(int32 PlayerId);

	/**
	 * Puts an item into its slot. Firearms replace nothing: the caller drops
	 * whatever occupied the slot first (see ACSMatchDirector::GiveItem).
	 * Grenades stack up to their MaxStack. Returns how many were NOT taken.
	 */
	int32 AddItem(int32 ItemIndex, int32 Count, int32 AmmoInMag, int32 Reserve);

	/** Takes up to Count from a slot. Returns what was actually removed. */
	FCSInventorySlot RemoveFromSlot(int32 Slot, int32 Count);

	/** Refuses empty slots. */
	bool SetEquippedSlot(int32 Slot);

	/** Changes the loaded and spare rounds of the firearm in a slot. */
	void SetSlotAmmo(int32 Slot, int32 AmmoInMag, int32 Reserve);

	/** Empties everything, knife included (a match reset). */
	TArray<FCSInventorySlot> TakeAll();

	UPROPERTY(BlueprintAssignable, Category = "CS|Inventory")
	FCSInventoryChanged OnInventoryChanged;

protected:
	void MarkChanged();

	UFUNCTION()
	void OnRep_Inventory();

	UPROPERTY(Replicated)
	int32 OwnerPlayerId = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Inventory, meta = (FusionArraySize = 5))
	TArray<FCSInventorySlot> Slots;

	UPROPERTY(ReplicatedUsing = OnRep_Inventory)
	int32 EquippedSlot = CSLoadout::Knife;

	UPROPERTY(VisibleAnywhere, Category = "CS|Components")
	TObjectPtr<UFusionActorComponent> FusionActor;
};
