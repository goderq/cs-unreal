// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v1.1 thrown grenade.
//
// Not a networked object. The authority decides a throw (ACSMatchDirector::
// TryThrowGrenade) and tells every peer through one RPC with the start point
// and velocity; each peer then flies its own local copy with the same
// projectile physics, so the arc looks the same everywhere without streaming
// a position. Only the authority's copy counts: when its fuse runs out the
// director applies the blast damage and broadcasts where it went off, and
// every other copy jumps there and explodes. A copy whose explosion message
// never arrives removes itself a little after its own fuse.
//
// After a master migration the new Master Client's copy is the one that
// explodes - authority is checked when the fuse ends, not when thrown.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSGrenade.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;

/** v2.0: what a grenade does when it goes off. Sent over the wire as an int32. */
enum class ECSGrenadeType : uint8
{
	Frag = 0,
	Flash = 1
};

UCLASS(NotBlueprintable)
class CSFUSION_API ACSGrenade : public AActor
{
	GENERATED_BODY()

public:
	ACSGrenade();

	/** Every peer: start flying. FuseSeconds counts from now. */
	void Launch(int32 InSerial, int32 InThrowerId, const FVector& Velocity, float FuseSeconds, ECSGrenadeType InType = ECSGrenadeType::Frag);

	/** Before Launch or Explode, for copies that only ever see the blast. */
	void SetType(ECSGrenadeType InType);
	ECSGrenadeType GetType() const { return Type; }

	/** Every peer: the blast, at Location (the authority's copy decides where). */
	void Explode(const FVector& Location);

	int32 GetSerial() const { return Serial; }
	int32 GetThrowerId() const { return ThrowerId; }

	static ACSGrenade* FindBySerial(const UObject* WorldContextObject, int32 Serial, bool bIncludeExploded = false);

	virtual void Tick(float DeltaSeconds) override;

protected:
	/** The flashbang pop, and the local player's blindness. */
	void ExplodeFlash(const FVector& Location);

	UFUNCTION()
	void HandleBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity);

	UPROPERTY(VisibleAnywhere, Category = "CS|Grenade")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "CS|Grenade")
	TObjectPtr<UStaticMeshComponent> Model;

	UPROPERTY(VisibleAnywhere, Category = "CS|Grenade")
	TObjectPtr<UProjectileMovementComponent> Movement;

private:
	int32 Serial = 0;
	int32 ThrowerId = 0;
	ECSGrenadeType Type = ECSGrenadeType::Frag;
	double FuseEnd = 0.0;
	bool bExploded = false;
	double LastBounceSound = -10.0;
	FRotator Spin = FRotator::ZeroRotator;
};
