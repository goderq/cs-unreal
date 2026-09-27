// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Environment/CSPropCluster.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

namespace
{
	const FName ClusterTag(TEXT("CSCluster"));
}

ACSPropCluster::ACSPropCluster()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	SetRootComponent(Root);
}

void ACSPropCluster::Rebuild()
{
	TArray<UHierarchicalInstancedStaticMeshComponent*> Old;
	GetComponents(Old);
	for (UHierarchicalInstancedStaticMeshComponent* Comp : Old)
	{
		if (Comp && Comp->ComponentHasTag(ClusterTag))
		{
			RemoveInstanceComponent(Comp);
			Comp->DestroyComponent();
		}
	}

	for (int32 g = 0; g < Groups.Num(); ++g)
	{
		const FCSClusterGroup& Group = Groups[g];
		if (!Group.Mesh || Group.Instances.Num() == 0)
		{
			continue;
		}
		const FName Name = MakeUniqueObjectName(this, UHierarchicalInstancedStaticMeshComponent::StaticClass(),
			*FString::Printf(TEXT("HISM_%s"), *Group.Mesh->GetName()));
		UHierarchicalInstancedStaticMeshComponent* Comp = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, Name, RF_Transactional);
		Comp->ComponentTags.Add(ClusterTag);
		Comp->SetMobility(EComponentMobility::Static);
		Comp->SetupAttachment(GetRootComponent());
		Comp->SetStaticMesh(Group.Mesh);
		for (int32 m = 0; m < Group.Materials.Num(); ++m)
		{
			if (Group.Materials[m])
			{
				Comp->SetMaterial(m, Group.Materials[m]);
			}
		}
		if (Group.CullDistance > 0.f)
		{
			Comp->SetCullDistances(FMath::RoundToInt(Group.CullDistance * 0.8f), FMath::RoundToInt(Group.CullDistance));
		}
		Comp->SetCollisionProfileName(Group.bCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
		Comp->SetCanEverAffectNavigation(Group.bCollision);
		Comp->SetCastShadow(Group.bCastShadow);
		AddInstanceComponent(Comp);
		Comp->RegisterComponent();
		Comp->AddInstances(Group.Instances, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ true);
	}
}

int32 ACSPropCluster::GetInstanceCount() const
{
	int32 Count = 0;
	for (const FCSClusterGroup& Group : Groups)
	{
		Count += Group.Instances.Num();
	}
	return Count;
}
