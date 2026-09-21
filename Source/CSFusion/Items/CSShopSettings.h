// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v1.1 shop: what can be bought with the in-match currency, for how much.
// The list order is the wire format (a buy request carries the index), so
// every peer must run the same config - like the item registry.
//
// When the shop is open is a game-mode rule (UCSModeSettings):
//   Deathmatch / Team Deathmatch  while the player is spawn-protected
//   5 vs 5                        the first BuySeconds of every round

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CSShopSettings.generated.h"

UENUM(BlueprintType)
enum class ECSShopCategory : uint8
{
	Rifles		UMETA(DisplayName = "Rifles"),
	SMGs		UMETA(DisplayName = "SMGs"),
	Heavy		UMETA(DisplayName = "Heavy"),
	Snipers		UMETA(DisplayName = "Snipers"),
	Gear		UMETA(DisplayName = "Gear"),
	Ammo		UMETA(DisplayName = "Ammo")
};

USTRUCT(BlueprintType)
struct CSFUSION_API FCSShopEntry
{
	GENERATED_BODY()

	/** Item registry id (UCSItemDefinition::ItemId). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Shop") FName ItemId;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Shop") int32 Price = 0;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Shop") ECSShopCategory Category = ECSShopCategory::Rifles;

	/** Weapons: spare magazines of their ammo that come with them. Ammo: stacks given. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Shop") int32 Bundle = 2;

	/** One-line description in the shop. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Shop") FString Blurb;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "CS Shop"))
class CSFUSION_API UCSShopSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UCSShopSettings();

	UPROPERTY(Config, EditAnywhere, Category = "Shop")
	TArray<FCSShopEntry> Entries;

	static const UCSShopSettings* Get() { return GetDefault<UCSShopSettings>(); }
	const FCSShopEntry* GetEntry(int32 Index) const { return Entries.IsValidIndex(Index) ? &Entries[Index] : nullptr; }
	static FText CategoryName(ECSShopCategory Category);
};

/** Why a buy request was refused (or Ok). Shown in the shop. */
UENUM(BlueprintType)
enum class ECSBuyResult : uint8
{
	Ok,
	ShopClosed,
	NotEnoughMoney,
	InventoryFull,
	/** Weapon already carried, or armor already full. */
	AlreadyOwned,
	Dead,
	Invalid
};
