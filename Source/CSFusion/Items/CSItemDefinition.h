// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Static description of an inventory item. One asset per item type.
//
// Like UCSWeaponDefinition, this never changes during a match, so every peer
// can read it locally and the authority validates against its own copy.
//
// Items travel over the network as an int32 index into UCSItemSettings::Items
// (see UCSItemSettings::FindItemIndex), never as a name or an object path:
// the list is the same on every peer because it comes from DefaultGame.ini,
// and an index costs one word on Fusion's wire.
//
// v2.0: every weapon a player can hold is an item - the pistol, the knife and
// both grenade types included - and LoadoutRole says which slot it fills.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CSItemDefinition.generated.h"

class UCSWeaponDefinition;
class USoundBase;
class UStaticMesh;
class UTexture2D;

UENUM(BlueprintType)
enum class ECSItemType : uint8
{
	Weapon		UMETA(DisplayName = "Weapon"),
	Ammo		UMETA(DisplayName = "Ammo"),
	Grenade		UMETA(DisplayName = "Grenade"),
	Armor		UMETA(DisplayName = "Armor"),
	Medkit		UMETA(DisplayName = "Medkit"),
	Misc		UMETA(DisplayName = "Misc")
};

/** v2.0 loadout: which of the five slots (keys 1-5) an item lives in. */
UENUM(BlueprintType)
enum class ECSLoadoutRole : uint8
{
	None		UMETA(DisplayName = "Not carried"),
	Primary		UMETA(DisplayName = "Primary (1)"),
	Pistol		UMETA(DisplayName = "Pistol (2)"),
	Knife		UMETA(DisplayName = "Knife (3)"),
	Frag		UMETA(DisplayName = "Frag grenade (4)"),
	Flash		UMETA(DisplayName = "Flashbang (5)")
};

UENUM(BlueprintType)
enum class ECSItemRarity : uint8
{
	Common		UMETA(DisplayName = "Common"),
	Uncommon	UMETA(DisplayName = "Uncommon"),
	Rare		UMETA(DisplayName = "Rare"),
	Epic		UMETA(DisplayName = "Epic")
};

UCLASS(BlueprintType)
class CSFUSION_API UCSItemDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// --- Identity ----------------------------------------------------------

	/** Stable id. Never reuse one; saved stats and drops key off it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FName ItemId = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity", meta = (MultiLine = "true"))
	FText Description;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	ECSItemType ItemType = ECSItemType::Misc;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	ECSItemRarity Rarity = ECSItemRarity::Common;

	/** v2.0: the loadout slot this item goes into. None for armor (worn at once). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	ECSLoadoutRole LoadoutRole = ECSLoadoutRole::None;

	// --- Stacking ----------------------------------------------------------

	/** Weapons never stack: each carries its own magazine. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stacking")
	bool bStackable = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stacking", meta = (ClampMin = "1", EditCondition = "bStackable"))
	int32 MaxStack = 1;

	/** How many a freshly spawned map pickup contains. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stacking", meta = (ClampMin = "1"))
	int32 DefaultPickupCount = 1;

	/** Reserved for a future weight limit; not enforced yet. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stacking", meta = (ClampMin = "0.0"))
	float Weight = 0.f;

	// --- Type-specific -----------------------------------------------------

	/** For ItemType == Weapon: the ballistics this item fires with. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TSoftObjectPtr<UCSWeaponDefinition> Weapon;

	// v2.0: spare rounds live with the weapon (FCSInventorySlot::Reserve), so
	// there is no separate ammo item any more; UCSWeaponDefinition::ReserveAmmo
	// is the most a weapon can carry.

	/** For ItemType == Armor: armor points granted on use. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumable", meta = (ClampMin = "0.0"))
	float ArmorAmount = 0.f;

	/** For ItemType == Medkit: health restored on use. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumable", meta = (ClampMin = "0.0"))
	float HealAmount = 0.f;

	// --- Presentation ------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UTexture2D> Icon;

	/** Mesh shown while the item lies in the world. Placeholder until Stage 6. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UStaticMesh> WorldMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	FVector WorldMeshScale = FVector(1.f);

	/** Tint for the placeholder mesh, so item types are told apart at a glance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	FLinearColor PlaceholderColor = FLinearColor::White;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<USoundBase> PickupSound;

	UFUNCTION(BlueprintPure, Category = "CS|Item")
	bool IsWeapon() const { return ItemType == ECSItemType::Weapon; }

	UFUNCTION(BlueprintPure, Category = "CS|Item")
	int32 GetMaxStack() const { return bStackable ? FMath::Max(1, MaxStack) : 1; }

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("CSItem"), GetFName());
	}
};
