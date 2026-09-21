// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// A short-lived visual effect made of one mesh and an optional light:
// muzzle flash, tracer, impact puff. It scales and fades itself out over its
// lifetime and then destroys itself.
//
// Built from engine primitives and two generated materials rather than
// Niagara systems: a Niagara asset cannot be authored by the bootstrap
// script, and these effects need nothing a scaled, fading mesh cannot do.
// Purely cosmetic and local to each peer - never replicated.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
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

	/** Spawns and starts an effect. Returns null on a server or without a world. */
	static ACSTransientFX* Spawn(UWorld* World, const FTransform& Transform, const FParams& Params);

	virtual void Tick(float DeltaSeconds) override;

private:
	void Start(const FParams& InParams);
	void ApplyAlpha(float Alpha);

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
