// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Phase 3 (AUDIT C9) surface self-test, enabled with -cstestsurfaces on a map
// that has metal and wood (Lvl_Depot; Scripts/run_tests.ps1 suite "surfaces").
// Checks on the real level what the unit test cannot:
//
//   1. the footstep trace (CSAudio::SurfaceBelow) reads Metal on top of meshes
//      whose material carries PM_Metal, and Wood on PM_Wood ones - i.e. the
//      physical material reaches the simple collision the trace hits;
//   2. the level blocks the sound-occlusion channel, and the player's own
//      capsule does not (a sound is never muffled by its source or listener).

#include "Player/CSPlayerController.h"

#include "Audio/CSAudio.h"
#include "Characters/CSCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Core/CSLog.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

void ACSPlayerController::CSTestSurfaces()
{
	CS_SELF_TEST_ONLY();
#if !UE_BUILD_SHIPPING
	ACSCharacter* Mine = Cast<ACSCharacter>(GetPawn());
	if (TestRetryUntil(Mine != nullptr, TestSurfacesTimer, &ACSPlayerController::CSTestSurfaces, TEXT("a pawn")))
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!Mine || !World)
	{
		UE_LOG(LogCS, Log, TEXT("SURFACE TEST RESULT: no pawn -> MISSING"));
		return;
	}

	// Meshes by the surface their material declares, probed from just above
	// the top of their bounds. Another mesh may sit on top of one (a crate on
	// a plate), so a probe only has to agree for most, not all, candidates.
	struct FTally { int32 Probed = 0; int32 Agreed = 0; };
	TMap<EPhysicalSurface, FTally> Tally;
	FVector OccluderTop = FVector::ZeroVector;
	bool bHaveOccluder = false;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TInlineComponentArray<UStaticMeshComponent*> Meshes(*It);
		for (const UStaticMeshComponent* Mesh : Meshes)
		{
			const UMaterialInterface* Material = Mesh->GetMaterial(0);
			if (!Material || Mesh->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block
				|| Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
			{
				continue;
			}
			const EPhysicalSurface Declared = UPhysicalMaterial::DetermineSurfaceType(Material->GetPhysicalMaterial());
			if (Declared != CSSurface::Metal && Declared != CSSurface::Wood)
			{
				continue;
			}
			FTally& T = Tally.FindOrAdd(Declared);
			if (T.Probed >= 40)
			{
				continue;
			}
			const FBoxSphereBounds Bounds = Mesh->Bounds;
			const FVector Top(Bounds.Origin.X, Bounds.Origin.Y, Bounds.Origin.Z + Bounds.BoxExtent.Z + 5.f);
			++T.Probed;
			if (CSAudio::SurfaceBelow(this, Top, Mine) == Declared)
			{
				++T.Agreed;
				if (!bHaveOccluder)
				{
					OccluderTop = Top;
					bHaveOccluder = true;
				}
			}
		}
	}
	const FTally Metal = Tally.FindRef(CSSurface::Metal);
	const FTally Wood = Tally.FindRef(CSSurface::Wood);
	const bool bSurfaces = Metal.Agreed > 0 && Wood.Agreed > 0
		&& Metal.Agreed * 2 >= Metal.Probed && Wood.Agreed * 2 >= Wood.Probed;

	// The level blocks occlusion traces: straight down into a probed mesh.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CSTestSurfaces), false, Mine);
	const bool bWorldOccludes = bHaveOccluder && World->LineTraceTestByChannel(
		OccluderTop + FVector(0.f, 0.f, 50.f), OccluderTop - FVector(0.f, 0.f, 50.f), CSCollision::AudioOcclusion, Params);

	// The player's own capsule does not: a trace straight through it.
	FCollisionQueryParams Through(SCENE_QUERY_STAT(CSTestSurfaces), false);
	FHitResult Hit;
	const FVector Center = Mine->GetActorLocation();
	World->LineTraceSingleByChannel(Hit, Center - Mine->GetActorRightVector() * 60.f, Center + Mine->GetActorRightVector() * 60.f,
		CSCollision::AudioOcclusion, Through);
	const bool bPlayersIgnored = Hit.GetActor() != Mine;
	const EPhysicalSurface Underfoot = CSAudio::SurfaceBelow(this,
		Center - FVector(0.f, 0.f, Mine->GetDefaultHalfHeight()), Mine);

	const bool bOk = bSurfaces && bWorldOccludes && bPlayersIgnored;
	UE_LOG(LogCS, Log, TEXT("SURFACE TEST RESULT: metal %d/%d, wood %d/%d, spawn floor surface %d, world occludes %s, player capsule %s -> %s"),
		Metal.Agreed, Metal.Probed, Wood.Agreed, Wood.Probed, static_cast<int32>(Underfoot),
		bWorldOccludes ? TEXT("yes") : TEXT("NO"), bPlayersIgnored ? TEXT("ignored") : TEXT("BLOCKS"),
		bOk ? TEXT("OK") : TEXT("BROKEN"));
#endif
}
