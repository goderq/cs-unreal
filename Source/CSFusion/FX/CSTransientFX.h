// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// A short-lived visual effect made of one mesh and an optional light:
// muzzle flash, tracer, impact puff. It scales and fades itself out over its
// lifetime and then goes back to a per-world pool (UCSTransientFXPool).
//
// Used for what needs a light or an exact shape: muzzle and grenade flashes,
// tracers. Particle effects (sparks, dust, smoke, shells, debris) are Niagara
// systems built by the CSFXBuilder commandlet and started by CSEffects.
// Purely cosmetic and local to each peer - never replicated.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "CSTransientFX.generated.h"

class UPointLightComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UMaterialInterface;

UENUM()
enum class ECSFXShape : uint8
{
	Sphere,
	Cylinder,	// 100 cm tall along Z, 100 cm wide
	Star		// two crossed planes, for muzzle flashes
};

UCLASS(NotBlueprintable)
class CSFUSION_API ACSTransientFX : public AActor
{
	GENERATED_BODY()

public:
	ACSTransientFX();

	struct FParams
	{
		ECSFXShape Shape = ECSFXShape::Sphere;
		bool bAdditive = true;
		FLinearColor Color = FLinearColor::White;
		float Intensity = 1.f;
		float Opacity = 1.f;
		float Lifetime = 0.1f;
		FVector StartScale = FVector(0.1f);
		FVector EndScale = FVector(0.1f);
		float LightIntensity = 0.f;
		float LightRadius = 300.f;
		/** First-person flashes are seen only by their owner, third-person ones by everyone else. */
		const AActor* VisibilityOwner = nullptr;
		bool bOnlyOwnerSee = false;
		bool bOwnerNoSee = false;
	};

	/** Starts an effect, reusing a parked one if the world has any. Null on a server or without a world. */
	static ACSTransientFX* Spawn(UWorld* World, const FTransform& Transform, const FParams& Params);

	virtual void Tick(float DeltaSeconds) override;

private:
	friend class UCSTransientFXPool;

	void Start(const FParams& InParams);
	void ApplyAlpha(float Alpha);
	/** Hidden and idle until the pool hands it out again. */
	void Park();

	/** One dynamic instance per base material, kept across reuses. */
	UPROPERTY(Transient)
	TMap<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInstanceDynamic>> MaterialInstances;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> MeshA;

	/** Second, crossed plane of the star shape. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> MeshB;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaneMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> AdditiveMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> TranslucentMaterial;

	/** Additive with a radial mask: round flash on flat planes. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> FlashMaterial;

	FParams Params;
	float Age = 0.f;
};
/**
 * Reuses finished ACSTransientFX actors (v2.0 phase 3, C10): a burst of fire
 * spawns a flash and a tracer per shot, and spawning / destroying actors at
 * that rate costs more than the effect itself. Finished effects are hidden and
 * parked here; ACSTransientFX::Spawn takes one back before creating a new one.
 */
UCLASS()
class CSFUSION_API UCSTransientFXPool : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** A parked effect moved to Transform, or a new one. */
	ACSTransientFX* Acquire(const FTransform& Transform);

	/** Parks a finished effect (or destroys it when the pool is full). */
	void Release(ACSTransientFX* Effect);

	int32 GetNumCreated() const { return NumCreated; }
	int32 GetNumReused() const { return NumReused; }
	int32 GetNumParked() const { return Parked.Num(); }

	/** Parked actors kept at most; a longer burst creates more, then destroys the extra. */
	static constexpr int32 MaxParked = 48;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<ACSTransientFX>> Parked;

	int32 NumCreated = 0;
	int32 NumReused = 0;
};
