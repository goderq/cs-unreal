// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// The item registry and inventory/pickup tuning.
//
// Items is an ORDERED list read from DefaultGame.ini. Its order is the wire
// format: an item is sent as its index in this array. Every peer loads the
// same ini, so index N means the same item everywhere. Appending is safe;
// reordering or removing entries breaks compatibility between builds, which
// is exactly what Fusion Settings > AppVersion is for - bump it when you do.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CSItemSettings.generated.h"

class UCSItemDefinition;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "CS Items"))
class CSFUSION_API UCSItemSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	static const UCSItemSettings* Get() { return GetDefault<UCSItemSettings>(); }

	/** The registry. Order is the network encoding - append only. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Registry")
	TArray<TSoftObjectPtr<UCSItemDefinition>> Items;

	/** Inventory slots per player. Also bounded by the FusionArraySize below. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Inventory", meta = (ClampMin = "1", ClampMax = "8"))
	int32 InventorySlots = 6;

	/**
	 * How far a player may be from a pickup, measured by the AUTHORITY from
	 * its own view of the pawn. Looser than the client's own reach so honest
	 * latency is not punished.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Pickups", meta = (ClampMin = "50.0"))
	float MaxPickupDistance = 350.f;

	/** Client-side reach used for the "Press E" prompt. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Pickups", meta = (ClampMin = "50.0"))
	float InteractReach = 250.f;

	/** Cap on simultaneously alive pickups, so drops cannot flood the room. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Pickups", meta = (ClampMin = "8"))
	int32 MaxWorldPickups = 96;

	/** Dropped items despawn after this long. Map-spawned items never do. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Pickups", meta = (ClampMin = "0.0"))
	float DroppedItemLifetimeSeconds = 120.f;

	// --- Registry lookups (valid on every peer) ----------------------------

	/** INDEX_NONE if not in the registry. */
	int32 FindItemIndex(FName ItemId) const;

	/** Synchronous load; the registry is small and used on every peer. */
	const UCSItemDefinition* GetItem(int32 Index) const;

	bool IsValidIndex(int32 Index) const { return Items.IsValidIndex(Index); }
};
