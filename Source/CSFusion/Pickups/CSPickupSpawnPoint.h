// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Level-design marker: "an item of this kind starts here".
//
// Intentionally NOT networked and owns no gameplay state. Every peer has it
// because it is part of the map, but only the authority acts on it - at the
// start of a round it spawns a Master-Client-owned ACSWorldPickup here, which
// is what actually replicates. Keeping the marker local means level
// designers place loot without touching networking at all.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSPickupSpawnPoint.generated.h"

class UBillboardComponent;

UCLASS()
class CSFUSION_API ACSPickupSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	ACSPickupSpawnPoint();

	/** Which item, by UCSItemDefinition::ItemId. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Pickup")
	FName ItemId = NAME_None;

	/** 0 uses the item's DefaultPickupCount. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CS|Pickup", meta = (ClampMin = "0"))
	int32 Count = 0;

	/** Authority only: spawn this point's pickup. Returns null on failure. */
	class ACSWorldPickup* SpawnPickup() const;

protected:
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> Sprite;
#endif
};
