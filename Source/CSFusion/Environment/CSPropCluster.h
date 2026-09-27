// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v2.1 map rework (docs/MAPS_REWORK.md): repeated map detail - windows,
// frames, railings, pallets, grass, stones, litter - as hierarchical instanced
// static meshes, one component per mesh. The map build script
// (Scripts/maps/*.py) fills Groups and calls Rebuild(); the components are
// instance components, saved with the level, so the cooked game loads them
// as they are. Each group carries its own cull distance (small detail
// disappears beyond 15-40 m) and collision (decor: none; cover: blocking).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CSPropCluster.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

USTRUCT(BlueprintType)
struct FCSClusterGroup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster")
	TObjectPtr<UStaticMesh> Mesh = nullptr;

	/** Per slot; empty or null entries keep the mesh's own material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster")
	TArray<TObjectPtr<UMaterialInterface>> Materials;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster")
	TArray<FTransform> Instances;

	/** Instances fade out beyond this distance (cm); 0 = never culled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster")
	float CullDistance = 0.f;

	/** Blocking collision (walls, cover); false for decor players walk through. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster")
	bool bCollision = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster")
	bool bCastShadow = true;
};

UCLASS()
class CSFUSION_API ACSPropCluster : public AActor
{
	GENERATED_BODY()

public:
	ACSPropCluster();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster")
	TArray<FCSClusterGroup> Groups;

	/** Replaces the instanced components with one per group (editor and build scripts). */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Cluster")
	void Rebuild();

	/** Instances in all groups (map statistics). */
	UFUNCTION(BlueprintCallable, Category = "Cluster")
	int32 GetInstanceCount() const;
};
