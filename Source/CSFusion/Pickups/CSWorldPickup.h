// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// An item lying in the world.
//
// Owned by the Master Client, like every piece of state a cheater must not
// control. Picking it up is a request to the authority, never a client-side
// grab: the authority processes requests one at a time, so when two players
// press E on the same item in the same frame the first request claims and
// destroys it and the second finds it gone. Exactly one player gets it.
//
// The pickup carries the full item state (count, rounds in the magazine), so
// a dropped AK keeps the ammo it was dropped with.
//
// Deliberately has NO collision on ECC_Visibility: pickups must never stop a
// bullet. Interaction targeting is done by UCSWeaponComponent-independent
// proximity + view-cone selection on the client (see ACSCharacter), and the
// authority re-checks distance itself.

#pragma once

#include "CoreMinimal.h"
#include "Core/CSFusionCompat.h"
#include "GameFramework/Actor.h"
#include "CSWorldPickup.generated.h"

class UCSItemDefinition;
class UFusionActorComponent;
class UStaticMeshComponent;

UCLASS()
class CSFUSION_API ACSWorldPickup : public AActor
{
	GENERATED_BODY()

public:
	ACSWorldPickup();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// --- Reads (every peer) ------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "CS|Pickup")
	int32 GetItemIndex() const { return ItemIndex; }

	UFUNCTION(BlueprintPure, Category = "CS|Pickup")
	int32 GetCount() const { return Count; }

	UFUNCTION(BlueprintPure, Category = "CS|Pickup")
	int32 GetAmmoInMag() const { return AmmoInMag; }

	const UCSItemDefinition* GetItemDefinition() const;

	/** "AK-47" or "Rifle Ammo x30", for the interaction prompt. */
	FText GetPromptName() const;

	/** False once claimed; also false while the item index is not resolved. */
	bool IsAvailable() const { return !bClaimed && ItemIndex != INDEX_NONE && Count > 0; }

	// --- Authority-only ----------------------------------------------------

	/** Sets what this pickup contains. Dropped pickups expire; map pickups do not. */
	void InitializeItem(int32 InItemIndex, int32 InCount, int32 InAmmoInMag, bool bInDropped);

	/** Claims and removes the pickup. Returns false if already claimed. */
	bool Claim();

	/** Puts a remainder back (the picker's inventory could only take part). */
	void SetRemainingCount(int32 NewCount);

	/** Number of pickups currently alive in a world. */
	static int32 CountAlive(const UObject* WorldContextObject);

	/**
	 * Authority only: frees room for Incoming new drops under MaxWorldPickups
	 * by removing the OLDEST dropped pickups. Map-placed loot is never evicted.
	 */
	static void MakeRoomForDrops(const UObject* WorldContextObject, int32 Incoming);

	/**
	 * Where the item visually flies out from (the death or drop point). Every
	 * peer animates the mesh from here to the pickup's real location, so the
	 * scatter looks physical while the network only carries two positions.
	 */
	void SetDropOrigin(const FVector& Origin);

	bool IsDropped() const { return ExpiresAtNetworkTime > 0.0; }
	double GetSpawnNetworkTime() const { return SpawnNetworkTime; }

protected:
	UFUNCTION()
	void OnRep_Item();

	void ApplyVisuals();

	UPROPERTY(VisibleAnywhere, Category = "CS|Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "CS|Components")
	TObjectPtr<UFusionActorComponent> FusionActor;

	UPROPERTY(ReplicatedUsing = OnRep_Item)
	int32 ItemIndex = INDEX_NONE;

	UPROPERTY(Replicated)
	int32 Count = 0;

	UPROPERTY(Replicated)
	int32 AmmoInMag = 0;

	/** Network time at which a dropped item vanishes. 0 = never. */
	UPROPERTY(Replicated)
	double ExpiresAtNetworkTime = 0.0;

	UPROPERTY(Replicated)
	double SpawnNetworkTime = 0.0;

	/** Start of the cosmetic fly-out arc. Zero vector = no arc. */
	UPROPERTY(Replicated)
	FVector DropOrigin = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "CS|Components")
	TObjectPtr<USceneComponent> Root;

	/** Authority-local; set the instant a request wins, before destruction. */
	bool bClaimed = false;

	/** Local cosmetic spin/bob. */
	float VisualTime = 0.f;

	/** v1.0 weapon model presentation: spin angle, lying pose and centring offset. */
	float SpinYaw = 0.f;
	FQuat ModelLie = FQuat::Identity;
	FVector ModelCentre = FVector::ZeroVector;
	FVector MeshBaseOffset = FVector(0.f, 0.f, 20.f);
};
