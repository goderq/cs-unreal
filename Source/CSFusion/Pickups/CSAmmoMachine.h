// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v2.0 ammo machine: a vending cabinet placed on the maps. Standing at one and
// pressing E tops up the reserve of both guns for UCSShopSettings::
// AmmoMachinePrice. They are the only way to get more rounds besides buying
// a new gun - nothing spawns on the floor any more.
//
// Machines are part of the level, not networked objects: every peer loads the
// same ones. A buy request therefore names a machine by its index in the
// name-sorted list (GetAllSorted), exactly like the player starts - an index
// every peer resolves to the same actor.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSAmmoMachine.generated.h"

class UPointLightComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

UCLASS()
class CSFUSION_API ACSAmmoMachine : public AActor
{
	GENERATED_BODY()

public:
	ACSAmmoMachine();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Every machine in the world, sorted by name so indices agree on every peer. */
	static TArray<ACSAmmoMachine*> GetAllSorted(const UObject* WorldContextObject);
	static ACSAmmoMachine* FindByIndex(const UObject* WorldContextObject, int32 Index);
	int32 GetSortedIndex() const;

	/** Where a player stands to use it (in front of the panel). */
	FVector GetUsePoint() const;

	/** Local cosmetic: blink the screen after a purchase. */
	void PlayVend();

protected:
	UPROPERTY(VisibleAnywhere, Category = "CS|AmmoMachine")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "CS|AmmoMachine")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "CS|AmmoMachine")
	TObjectPtr<UPointLightComponent> Glow;

	UPROPERTY(VisibleAnywhere, Category = "CS|AmmoMachine")
	TObjectPtr<UTextRenderComponent> Label;

private:
	float VendFlash = 0.f;
	float Phase = 0.f;
};
