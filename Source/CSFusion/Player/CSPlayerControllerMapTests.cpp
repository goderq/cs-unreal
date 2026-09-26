// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v2.0 phase 4 map audit, enabled with -cstestmapaudit (Scripts/run_tests.ps1
// suites mapdepot, mapoldtown, mapwarehouse). Offline, on the real level, with
// a 1 m grid of navmesh points (every floor, roof and catwalk the navmesh covers):
//
//   spawns     every start has room for a player, stands on the navmesh and
//              reaches the first Alpha start; no Alpha start sees a Bravo
//              start; free-for-all starts are spread out
//   routes     distinct walking routes between the two bases (lanes and
//              flanks): the shortest route, then again with the ground near
//              the routes already found made expensive; a route counts when it
//              crosses the middle at least 8 m from the others and is at most
//              1.8 times the shortest
//   sightlines per reachable point, the longest clear line at eye height,
//              so how much of the map is close, mid and long range
//   height     share of reachable points above the ground floor
//   machines   ammo machines (C19): at least two, all reachable
//   reverb     audio volumes with a reverb effect (interiors)
//   light      one sun, one sky light, an unbound post-process volume
//   budget     draw calls and primitives from three views, texture memory
//
// Result lines: "MAP AUDIT RESULT: <check> ... -> <CHECK> OK" or a failure word.

#include "Player/CSPlayerController.h"

#include "Characters/CSCharacter.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ContentStreaming.h"
#include "Core/CSLog.h"
#include "DynamicRHI.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Pickups/CSAmmoMachine.h"
#include "RHIStats.h"
#include "Sound/AudioVolume.h"
#include "TimerManager.h"

namespace
{
	constexpr float GridStep = 100.f;     // 1 m: a 2 m grid missed the 1.5-2 m passages players use
	constexpr float EyeHeight = 150.f;     // above a navmesh point, which lies on the floor
	constexpr float RunSpeed = 420.f;      // UCSCharacterMovementComponent::WalkSpeed
	constexpr float SightCap = 12000.f;
	constexpr float Elevated = 100.f;      // above the ground floor counts as "up"

	// Budgets for the owner's laptop RTX 3050 (4 GB), docs/MAPS.md.
	constexpr int32 DrawBudget = 3000;
	constexpr int32 PrimBudget = 4000000;

	struct FMapNode
	{
		FVector P;
		bool bReach = false;
		bool bAlpha = false;
		bool bBravo = false;
		TArray<TPair<int32, float>> Links;
	};

	TArray<FMapNode> GNodes;
	FVector GAlphaEye = FVector::ZeroVector;
	FVector GBravoEye = FVector::ZeroVector;
	FVector GCentreEye = FVector::ZeroVector;

	float Percentile(TArray<float> Values, float Q)
	{
		if (Values.Num() == 0)
		{
			return 0.f;
		}
		Values.Sort();
		return Values[FMath::Clamp(FMath::FloorToInt(Q * (Values.Num() - 1)), 0, Values.Num() - 1)];
	}

	/** Walking length of the path, or -1 when there is none (or only a partial one). */
	float PathLength(UWorld* World, const FVector& From, const FVector& To)
	{
		if (FVector::Dist(From, To) < 50.f)
		{
			return 0.f;   // no path is built from a point to itself
		}
		const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, From, To);
		return (Path && Path->IsValid() && !Path->IsPartial()) ? Path->GetPathLength() : -1.f;
	}

	/** Cheapest route from any Alpha node to any Bravo node; node costs multiplied where Penalty is set. */
	TArray<int32> CheapestRoute(const TArray<uint8>& Penalty)
	{
		const int32 N = GNodes.Num();
		TArray<float> Cost;
		TArray<int32> Prev;
		Cost.Init(TNumericLimits<float>::Max(), N);
		Prev.Init(INDEX_NONE, N);
		using FEntry = TPair<float, int32>;
		TArray<FEntry> Heap;
		const auto Less = [](const FEntry& A, const FEntry& B) { return A.Key < B.Key; };
		for (int32 i = 0; i < N; ++i)
		{
			if (GNodes[i].bAlpha)
			{
				Cost[i] = 0.f;
				Heap.HeapPush(FEntry(0.f, i), Less);
			}
		}
		int32 End = INDEX_NONE;
		while (Heap.Num() > 0)
		{
			FEntry Top;
			Heap.HeapPop(Top, Less);
			if (Top.Key > Cost[Top.Value])
			{
				continue;
			}
			if (GNodes[Top.Value].bBravo)
			{
				End = Top.Value;
				break;
			}
			for (const TPair<int32, float>& Link : GNodes[Top.Value].Links)
			{
				const float Next = Top.Key + Link.Value * (Penalty[Link.Key] ? 8.f : 1.f);
				if (Next < Cost[Link.Key])
				{
					Cost[Link.Key] = Next;
					Prev[Link.Key] = Top.Value;
					Heap.HeapPush(FEntry(Next, Link.Key), Less);
				}
			}
		}
		TArray<int32> Route;
		for (int32 At = End; At != INDEX_NONE; At = Prev[At])
		{
			Route.Insert(At, 0);
		}
		return Route;
	}
}

void ACSPlayerController::CSTestMapAudit()
{
	CS_SELF_TEST_ONLY();
#if !UE_BUILD_SHIPPING
	UWorld* World = GetWorld();
	ACSCharacter* Me = Cast<ACSCharacter>(GetPawn());
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const bool bReady = Me && Nav && Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) && !Nav->IsNavigationBuildInProgress();
	if (TestRetryUntil(bReady, TestMapTimer, &ACSPlayerController::CSTestMapAudit, TEXT("the navmesh")))
	{
		return;
	}
	if (!bReady)
	{
		UE_LOG(LogCS, Log, TEXT("MAP AUDIT RESULT: no navmesh -> NAVMESH MISSING"));
		UE_LOG(LogCS, Log, TEXT("MAP AUDIT: done."));
		return;
	}
	const FString Map = World->GetMapName().Replace(TEXT("UEDPIE_0_"), TEXT(""));
	const double Started = FPlatformTime::Seconds();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CSMapAudit), false, Me);

	// --- Starts -------------------------------------------------------------
	TArray<APlayerStart*> Alpha, Bravo, Free;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		const FName Tag = It->PlayerStartTag;
		(Tag == TEXT("Alpha") ? Alpha : Tag == TEXT("Bravo") ? Bravo : Free).Add(*It);
	}
	TArray<APlayerStart*> All = Alpha;
	All.Append(Bravo);
	All.Append(Free);
	if (All.Num() == 0)
	{
		UE_LOG(LogCS, Log, TEXT("MAP AUDIT RESULT: %s has no player starts -> SPAWNS MISSING"), *Map);
		UE_LOG(LogCS, Log, TEXT("MAP AUDIT: done."));
		return;
	}
	const FVector Extent(60.f, 60.f, 150.f);
	auto OnNav = [Nav, &Extent](const FVector& Point, FVector& Out)
	{
		FNavLocation Loc;
		if (Nav->ProjectPointToNavigation(Point, Loc, Extent))
		{
			Out = Loc.Location;
			return true;
		}
		return false;
	};
	// A PlayerStart stands 100 cm above the floor (capsule centre height).
	auto Feet = [](const AActor* Start) { return Start->GetActorLocation() - FVector(0.f, 0.f, 90.f); };
	FVector Origin = FVector::ZeroVector;
	const APlayerStart* First = Alpha.Num() ? Alpha[0] : All[0];
	if (!OnNav(Feet(First), Origin))
	{
		UE_LOG(LogCS, Log, TEXT("MAP AUDIT RESULT: %s start %s is off the navmesh -> SPAWNS BROKEN"), *Map, *First->GetName());
		UE_LOG(LogCS, Log, TEXT("MAP AUDIT: done."));
		return;
	}
	int32 NoRoom = 0, OffNav = 0, Unreachable = 0;
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(34.f, 86.f);
	for (const APlayerStart* Start : All)
	{
		if (World->OverlapBlockingTestByChannel(Start->GetActorLocation() + FVector(0.f, 0.f, 4.f), FQuat::Identity, ECC_Pawn, Capsule, Params))
		{
			++NoRoom;
			UE_LOG(LogCS, Log, TEXT("MAP AUDIT: %s at %s is inside geometry"), *Start->GetName(), *Start->GetActorLocation().ToCompactString());
		}
		FVector P;
		if (!OnNav(Feet(Start), P))
		{
			++OffNav;
			UE_LOG(LogCS, Log, TEXT("MAP AUDIT: %s at %s is off the navmesh"), *Start->GetName(), *Start->GetActorLocation().ToCompactString());
		}
		else if (PathLength(World, Origin, P) < 0.f)
		{
			++Unreachable;
			UE_LOG(LogCS, Log, TEXT("MAP AUDIT: %s at %s cannot be reached"), *Start->GetName(), *Start->GetActorLocation().ToCompactString());
		}
	}
	const bool bTeams = Alpha.Num() >= 5 && Bravo.Num() >= 5;
	UE_LOG(LogCS, Log, TEXT("MAP AUDIT RESULT: %s starts %d (Alpha %d, Bravo %d, free %d), inside geometry %d, off navmesh %d, unreachable %d -> %s"),
		*Map, All.Num(), Alpha.Num(), Bravo.Num(), Free.Num(), NoRoom, OffNav, Unreachable,
		(bTeams && Free.Num() >= 12 && NoRoom == 0 && OffNav == 0 && Unreachable == 0) ? TEXT("SPAWNS OK") : TEXT("SPAWNS BROKEN"));

	// Spawn safety: no enemy base in sight, free starts apart.
	int32 SeenPairs = 0;
	float SeenClosest = 0.f;
	for (const APlayerStart* A : Alpha)
	{
		for (const APlayerStart* B : Bravo)
		{
			const FVector EyeA = A->GetActorLocation() + FVector(0.f, 0.f, 60.f);
			const FVector EyeB = B->GetActorLocation() + FVector(0.f, 0.f, 60.f);
			if (!World->LineTraceTestByChannel(EyeA, EyeB, ECC_Visibility, Params))
			{
				SeenClosest = SeenPairs++ ? FMath::Min(SeenClosest, float(FVector::Dist(EyeA, EyeB))) : float(FVector::Dist(EyeA, EyeB));
			}
		}
	}
	float FreeSpacing = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Free.Num(); ++i)
	{
		for (int32 j = i + 1; j < Free.Num(); ++j)
		{
			FreeSpacing = FMath::Min(FreeSpacing, float(FVector::Dist(Free[i]->GetActorLocation(), Free[j]->GetActorLocation())));
		}
	}
	if (Free.Num() < 2)
	{
		FreeSpacing = 0.f;
	}
	UE_LOG(LogCS, Log, TEXT("MAP AUDIT RESULT: %s Alpha-Bravo start pairs in sight %d (closest %.0f m), free starts at least %.1f m apart -> %s"),
		*Map, SeenPairs, SeenClosest / 100.f, FreeSpacing / 100.f,
		(SeenPairs == 0 && FreeSpacing >= 500.f) ? TEXT("SPAWN SAFETY OK") : TEXT("SPAWN SAFETY BROKEN"));

	// --- Navmesh grid -------------------------------------------------------
	FBox Bounds(ForceInit);
	for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
	{
		Bounds += It->GetComponentsBoundingBox(true);
	}
	GNodes.Reset();
	TMap<FIntPoint, TArray<int32>> Cells;
	const FVector GridExtent(GridStep * 0.3f, GridStep * 0.3f, 100.f);
	for (float X = Bounds.Min.X + GridStep * 0.5f; X < Bounds.Max.X; X += GridStep)
	{
		for (float Y = Bounds.Min.Y + GridStep * 0.5f; Y < Bounds.Max.Y; Y += GridStep)
		{
			const FIntPoint Cell(FMath::FloorToInt((X - Bounds.Min.X) / GridStep), FMath::FloorToInt((Y - Bounds.Min.Y) / GridStep));
			for (float Z = FMath::Max(Bounds.Min.Z, -100.f); Z < Bounds.Max.Z; Z += 200.f)
			{
				FNavLocation Loc;
				if (!Nav->ProjectPointToNavigation(FVector(X, Y, Z), Loc, GridExtent))
				{
					continue;
				}
				TArray<int32>& InCell = Cells.FindOrAdd(Cell);
				const bool bKnown = InCell.ContainsByPredicate([&Loc](int32 i) { return FMath::Abs(GNodes[i].P.Z - Loc.Location.Z) < 80.f; });
				if (!bKnown)
				{
					InCell.Add(GNodes.Num());
					GNodes.Add({ Loc.Location });
				}
			}
		}
	}
	float FloorZ = TNumericLimits<float>::Max();
	int32 Reached = 0;
	for (FMapNode& Node : GNodes)
	{
		Node.bReach = PathLength(World, Origin, Node.P) >= 0.f;
		if (Node.bReach)
		{
			++Reached;
			FloorZ = FMath::Min(FloorZ, float(Node.P.Z));
		}
	}
	// Links between reachable neighbours (ramps included: navmesh raycast plus a clear line at knee height).
	for (const TPair<FIntPoint, TArray<int32>>& Cell : Cells)
	{
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				if ((DX == 0 && DY == 0) || DX < 0 || (DX == 0 && DY < 0))
				{
					continue;
				}
				const TArray<int32>* Other = Cells.Find(Cell.Key + FIntPoint(DX, DY));
				if (!Other)
				{
					continue;
				}
				for (int32 A : Cell.Value)
				{
					for (int32 B : *Other)
					{
						FMapNode& NA = GNodes[A];
						FMapNode& NB = GNodes[B];
						FVector Hit;
						if (!NA.bReach || !NB.bReach || FMath::Abs(NA.P.Z - NB.P.Z) > 100.f
							|| UNavigationSystemV1::NavigationRaycast(World, NA.P, NB.P, Hit)
							|| World->LineTraceTestByChannel(NA.P + FVector(0.f, 0.f, 60.f), NB.P + FVector(0.f, 0.f, 60.f), ECC_Visibility, Params))
						{
							continue;
						}
						const float Len = FVector::Dist(NA.P, NB.P);
						NA.Links.Add({ B, Len });
						NB.Links.Add({ A, Len });
					}
				}
			}
		}
	}

	// Base regions: reachable ground within 6 m of the team starts.
	auto Centroid = [](const TArray<APlayerStart*>& Starts)
	{
		FVector Sum = FVector::ZeroVector;
		for (const APlayerStart* S : Starts)
		{
			Sum += S->GetActorLocation();
		}
		return Starts.Num() ? Sum / Starts.Num() : Sum;
	};
	const TArray<APlayerStart*>& SideA = Alpha.Num() ? Alpha : All;
	const TArray<APlayerStart*>& SideB = Bravo.Num() ? Bravo : All;
	const FVector CA = Centroid(SideA);
	const FVector CB = Centroid(SideB);
	for (FMapNode& Node : GNodes)
	{
		for (const APlayerStart* S : SideA)
		{
			Node.bAlpha |= Node.bReach && FVector::Dist2D(Node.P, S->GetActorLocation()) < 600.f && FMath::Abs(Node.P.Z - Feet(S).Z) < 150.f;
		}
		for (const APlayerStart* S : SideB)
		{
			Node.bBravo |= Node.bReach && FVector::Dist2D(Node.P, S->GetActorLocation()) < 600.f && FMath::Abs(Node.P.Z - Feet(S).Z) < 150.f;
		}
	}

	// --- Routes ---------------------------------------------------------------
	const FVector Axis = (CB - CA).GetSafeNormal2D();
	const FVector Side(-Axis.Y, Axis.X, 0.f);
	const FVector Mid = (CA + CB) * 0.5f;
	TArray<uint8> Penalty;
	Penalty.Init(0, GNodes.Num());
	TArray<FString> RouteText;
	TArray<float> Crossings;   // where each kept route crosses the middle, cm to the side
	float Shortest = -1.f;
	for (int32 Attempt = 0; Attempt < 8 && RouteText.Num() < 5; ++Attempt)
	{
		const TArray<int32> Route = CheapestRoute(Penalty);
		if (Route.Num() < 2)
		{
			break;
		}
		float Length = 0.f, Top = 0.f;
		int32 Inner = 0, Reused = 0;
		for (int32 k = 0; k < Route.Num(); ++k)
		{
			const FMapNode& Node = GNodes[Route[k]];
			Length += k ? float(FVector::Dist(GNodes[Route[k - 1]].P, Node.P)) : 0.f;
			Top = FMath::Max(Top, float(Node.P.Z) - FloorZ);
			if (!Node.bAlpha && !Node.bBravo)
			{
				++Inner;
				Reused += Penalty[Route[k]];
			}
		}
		// Where the route crosses the middle of the map, to the side of the base-to-base line.
		float Offset = 0.f, Best = TNumericLimits<float>::Max();
		for (int32 Index : Route)
		{
			const float Along = FMath::Abs(FVector::DotProduct(GNodes[Index].P - Mid, Axis));
			if (Along < Best)
			{
				Best = Along;
				Offset = FVector::DotProduct(GNodes[Index].P - Mid, Side);
			}
		}
		const bool bFirst = Shortest < 0.f;
		// A lane is where a route crosses the middle of the map: every route shares
		// the way out of its base, so overlap near the bases says nothing.
		const bool bDistinct = bFirst || (Length <= Shortest * 1.8f
			&& !Crossings.ContainsByPredicate([Offset](float C) { return FMath::Abs(C - Offset) < 800.f; }));
		if (bFirst)
		{
			Shortest = Length;
		}
		UE_LOG(LogCS, Log, TEXT("MAP AUDIT: %s route search %d: %.0f m, crosses the middle %+.0f m, %d of %d points near earlier routes -> %s"),
			*Map, Attempt + 1, Length / 100.f, Offset / 100.f, Reused, Inner, bDistinct ? TEXT("kept") : TEXT("same lane"));
		if (FParse::Param(FCommandLine::Get(), TEXT("mapauditpaths")))
		{
			FString Points;
			for (int32 k = 0; k < Route.Num(); k += 4)
			{
				Points += FString::Printf(TEXT(" (%.0f,%.0f,%.0f)"), GNodes[Route[k]].P.X, GNodes[Route[k]].P.Y, GNodes[Route[k]].P.Z);
			}
			UE_LOG(LogCS, Log, TEXT("MAP AUDIT: %s route search %d points:%s"), *Map, Attempt + 1, *Points);
		}
		if (bDistinct)
		{
			Crossings.Add(Offset);
			RouteText.Add(FString::Printf(TEXT("%.0f m / %.0f s, crosses the middle %+.0f m to the side%s"),
				Length / 100.f, Length / RunSpeed, Offset / 100.f, Top > Elevated ? *FString::Printf(TEXT(", up to %.1f m high"), Top / 100.f) : TEXT("")));
		}
		// Whether kept or not, the next search avoids this ground.
		for (int32 i = 0; i < GNodes.Num(); ++i)
		{
			if (Penalty[i] || GNodes[i].bAlpha || GNodes[i].bBravo)
			{
				continue;
			}
			for (int32 Index : Route)
			{
				if (FVector::Dist2D(GNodes[i].P, GNodes[Index].P) < 450.f && FMath::Abs(GNodes[i].P.Z - GNodes[Index].P.Z) < 200.f)
				{
					Penalty[i] = 1;
					break;
				}
			}
		}
	}
	for (int32 i = 0; i < RouteText.Num(); ++i)
	{
		UE_LOG(LogCS, Log, TEXT("MAP AUDIT: %s route %d: %s"), *Map, i + 1, *RouteText[i]);
	}
	UE_LOG(LogCS, Log, TEXT("MAP AUDIT RESULT: %s %d grid points, %d reachable, %d distinct routes between the bases, shortest %.0f s -> %s"),
		*Map, GNodes.Num(), Reached, RouteText.Num(), Shortest / RunSpeed, RouteText.Num() >= 3 ? TEXT("ROUTES OK") : TEXT("ROUTES BROKEN"));

	// --- Sightlines and height -------------------------------------------------
	struct FLine { float Dist; FVector Eye; int32 Dir; };
	TArray<FLine> Open;
	TArray<float> Longest;
	int32 Close = 0, MidRange = 0, Long = 0, VeryLong = 0, Up = 0;
	for (const FMapNode& Node : GNodes)
	{
		if (!Node.bReach)
		{
			continue;
		}
		Up += (Node.P.Z - FloorZ) > Elevated;
		const FVector Eye = Node.P + FVector(0.f, 0.f, EyeHeight);
		float Max = 0.f;
		for (int32 d = 0; d < 16; ++d)
		{
			const float Angle = d * UE_TWO_PI / 16.f;
			const FVector Dir(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
			FHitResult Hit;
			const float Dist = World->LineTraceSingleByChannel(Hit, Eye, Eye + Dir * SightCap, ECC_Visibility, Params) ? Hit.Distance : SightCap;
			Max = FMath::Max(Max, Dist);
			if (Dist > 5000.f)
			{
				Open.Add({ Dist, Eye, d });
			}
		}
		Longest.Add(Max);
		(Max < 1500.f ? Close : Max <= 3500.f ? MidRange : Long)++;
		VeryLong += Max > 5000.f;
	}
	const int32 Samples = FMath::Max(1, Longest.Num());
	const float VeryLongShare = 100.f * VeryLong / Samples;
	const float MaxSight = Percentile(Longest, 1.f);
	// The longest lines, one per corridor: where to put the next piece of cover.
	Open.Sort([](const FLine& A, const FLine& B) { return A.Dist > B.Dist; });
	TArray<FLine> Shown;
	for (const FLine& Line : Open)
	{
		const FVector Dir(FMath::Cos(Line.Dir * UE_TWO_PI / 16.f), FMath::Sin(Line.Dir * UE_TWO_PI / 16.f), 0.f);
		const bool bSame = Shown.ContainsByPredicate([&](const FLine& S)
		{
			if ((S.Dir % 8) != (Line.Dir % 8))
			{
				return false;
			}
			const FVector Delta = Line.Eye - S.Eye;
			return (Delta - Dir * FVector::DotProduct(Delta, Dir)).Size2D() < 600.f;
		});
		if (!bSame)
		{
			Shown.Add(Line);
			const FVector End = Line.Eye + Dir * Line.Dist;
			UE_LOG(LogCS, Log, TEXT("MAP AUDIT: %s open line %.0f m from (%.0f, %.0f, %.0f) to (%.0f, %.0f)"),
				*Map, Line.Dist / 100.f, Line.Eye.X, Line.Eye.Y, Line.Eye.Z - EyeHeight, End.X, End.Y);
		}
		if (Shown.Num() >= 10)
		{
			break;
		}
	}
	UE_LOG(LogCS, Log, TEXT("MAP AUDIT RESULT: %s longest line of sight per point: median %.0f m, p90 %.0f m, max %.0f m; close (<15 m) %.0f%%, mid %.0f%%, long (>35 m) %.0f%%, over 50 m %.0f%% -> %s"),
		*Map, Percentile(Longest, 0.5f) / 100.f, Percentile(Longest, 0.9f) / 100.f, MaxSight / 100.f,
		100.f * Close / Samples, 100.f * MidRange / Samples, 100.f * Long / Samples, VeryLongShare,
		(MaxSight <= 6500.f && VeryLongShare <= 25.f) ? TEXT("SIGHTLINES OK") : TEXT("SIGHTLINES BROKEN"));
	int32 UpAll = 0;
	float Highest = 0.f;
	for (const FMapNode& Node : GNodes)
	{
		UpAll += (Node.P.Z - FloorZ) > Elevated;
		Highest = Node.bReach ? FMath::Max(Highest, float(Node.P.Z - FloorZ)) : Highest;
	}
	UE_LOG(LogCS, Log, TEXT("MAP AUDIT: %s navmesh points above the ground floor %d, reachable %d, highest reachable %.1f m"),
		*Map, UpAll, Up, Highest / 100.f);
	const float UpShare = 100.f * Up / Samples;
	UE_LOG(LogCS, Log, TEXT("MAP AUDIT RESULT: %s reachable points above the ground floor %.1f%% -> %s"),
		*Map, UpShare, UpShare >= 3.f ? TEXT("HEIGHT OK") : TEXT("HEIGHT BROKEN"));

	// --- Ammo machines (C19) ---------------------------------------------------
	const TArray<ACSAmmoMachine*> Machines = ACSAmmoMachine::GetAllSorted(this);
	int32 MachinesReached = 0;
	float NearA = -1.f, NearB = -1.f;
	FVector BaseA = FVector::ZeroVector, BaseB = FVector::ZeroVector;
	const bool bBaseA = OnNav(CA - FVector(0.f, 0.f, 90.f), BaseA);
	const bool bBaseB = OnNav(CB - FVector(0.f, 0.f, 90.f), BaseB);
	for (const ACSAmmoMachine* Machine : Machines)
	{
		FVector Use;
		if (!OnNav(Machine->GetUsePoint(), Use))
		{
			UE_LOG(LogCS, Log, TEXT("MAP AUDIT: %s stands off the navmesh"), *Machine->GetName());
			continue;
		}
		const float FromA = bBaseA ? PathLength(World, BaseA, Use) : -1.f;
		const float FromB = bBaseB ? PathLength(World, BaseB, Use) : -1.f;
		if (FromA >= 0.f && FromB >= 0.f)
		{
			++MachinesReached;
			NearA = NearA < 0.f ? FromA : FMath::Min(NearA, FromA);
			NearB = NearB < 0.f ? FromB : FMath::Min(NearB, FromB);
		}
	}
	UE_LOG(LogCS, Log, TEXT("MAP AUDIT RESULT: %s ammo machines %d, reachable from both bases %d, nearest %.0f s from Alpha, %.0f s from Bravo -> %s"),
		*Map, Machines.Num(), MachinesReached, NearA / RunSpeed, NearB / RunSpeed,
		Machines.Num() < 2 ? TEXT("MACHINES MISSING") : (MachinesReached == Machines.Num() ? TEXT("MACHINES OK") : TEXT("MACHINES BROKEN")));

	// --- Reverb and light --------------------------------------------------------
	int32 Reverbs = 0;
	for (TActorIterator<AAudioVolume> It(World); It; ++It)
	{
		Reverbs += (It->GetReverbSettings().bApplyReverb && It->GetReverbSettings().ReverbEffect) ? 1 : 0;
	}
	UE_LOG(LogCS, Log, TEXT("MAP AUDIT RESULT: %s reverb volumes %d -> %s"), *Map, Reverbs, Reverbs > 0 ? TEXT("REVERB OK") : TEXT("REVERB MISSING"));

	int32 Suns = 0, Skies = 0, Points = 0, Shadowed = 0, Posts = 0, MeshComps = 0;
	TSet<const UStaticMesh*> Meshes;
	TSet<const UMaterialInterface*> Materials;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->IsA<APawn>() || (It->GetOwner() && It->GetOwner()->IsA<APawn>()))
		{
			continue;
		}
		Posts += (It->IsA<APostProcessVolume>() && Cast<APostProcessVolume>(*It)->bUnbound) ? 1 : 0;
		TInlineComponentArray<UActorComponent*> Comps(*It);
		for (const UActorComponent* Comp : Comps)
		{
			Suns += Comp->IsA<UDirectionalLightComponent>();
			Skies += Comp->IsA<USkyLightComponent>();
			if (const UPointLightComponent* Light = Cast<UPointLightComponent>(Comp))
			{
				++Points;
				Shadowed += Light->CastShadows;
			}
			if (const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Comp); Mesh && Mesh->GetStaticMesh())
			{
				++MeshComps;
				Meshes.Add(Mesh->GetStaticMesh());
				for (int32 m = 0; m < Mesh->GetNumMaterials(); ++m)
				{
					Materials.Add(Mesh->GetMaterial(m));
				}
			}
		}
	}
	UE_LOG(LogCS, Log, TEXT("MAP AUDIT RESULT: %s sun %d, sky light %d, unbound post-process %d, point lights %d (%d cast shadows) -> %s"),
		*Map, Suns, Skies, Posts, Points, Shadowed, (Suns == 1 && Skies == 1 && Posts >= 1) ? TEXT("LIGHT OK") : TEXT("LIGHT BROKEN"));
	UE_LOG(LogCS, Log, TEXT("MAP AUDIT: %s level has %d mesh components, %d distinct meshes, %d distinct materials; analysis took %.1f s"),
		*Map, MeshComps, Meshes.Num(), Materials.Num(), FPlatformTime::Seconds() - Started);

	// --- Budget: three views, the worst frame each ----------------------------
	auto EyeAt = [&OnNav](const FVector& Point)
	{
		FVector P;
		return (OnNav(Point, P) ? P : Point) + FVector(0.f, 0.f, 100.f);
	};
	GAlphaEye = EyeAt(CA - FVector(0.f, 0.f, 90.f));
	GBravoEye = EyeAt(CB - FVector(0.f, 0.f, 90.f));
	GCentreEye = EyeAt(Mid - FVector(0.f, 0.f, 90.f));
	MapBudgetViews.Reset();
	MapBudgetViews.Add(FTransform((GBravoEye - GAlphaEye).Rotation(), GAlphaEye));
	MapBudgetViews.Add(FTransform((GAlphaEye - GBravoEye).Rotation(), GBravoEye));
	MapBudgetViews.Add(FTransform((GAlphaEye - GCentreEye).Rotation() + FRotator(0.f, 60.f, 0.f), GCentreEye));
	MapBudgetStop = -1;
	MapBudgetWorstDraws = MapBudgetWorstPrims = 0;
	TWeakObjectPtr<ACSPlayerController> WeakThis(this);
	FCoreDelegates::OnEndFrame.Remove(MapBudgetFrameHandle);
	MapBudgetFrameHandle = FCoreDelegates::OnEndFrame.AddLambda([WeakThis]()
	{
		if (ACSPlayerController* PC = WeakThis.Get())
		{
			PC->MapBudgetDraws = FMath::Max(PC->MapBudgetDraws, GNumDrawCallsRHI[0]);
			PC->MapBudgetPrims = FMath::Max(PC->MapBudgetPrims, GNumPrimitivesDrawnRHI[0]);
		}
	});
	MapAuditBudgetStep();
#endif
}

void ACSPlayerController::MapAuditBudgetStep()
{
#if !UE_BUILD_SHIPPING
	const FString Map = GetWorld()->GetMapName().Replace(TEXT("UEDPIE_0_"), TEXT(""));
	if (MapBudgetStop >= 0)
	{
		UE_LOG(LogCS, Log, TEXT("MAP AUDIT: %s view %d: %d draw calls, %d primitives"), *Map, MapBudgetStop + 1, MapBudgetDraws, MapBudgetPrims);
		MapBudgetWorstDraws = FMath::Max(MapBudgetWorstDraws, MapBudgetDraws);
		MapBudgetWorstPrims = FMath::Max(MapBudgetWorstPrims, MapBudgetPrims);
	}
	++MapBudgetStop;
	ACSCharacter* Me = Cast<ACSCharacter>(GetPawn());
	if (Me && MapBudgetViews.IsValidIndex(MapBudgetStop))
	{
		const FTransform& View = MapBudgetViews[MapBudgetStop];
		Me->SetActorLocation(View.GetLocation(), false, nullptr, ETeleportType::TeleportPhysics);
		SetControlRotation(FRotator(-4.f, View.Rotator().Yaw, 0.f));
		// Let streaming settle, then take the worst of the next second.
		FTimerHandle Reset;
		GetWorldTimerManager().SetTimer(Reset, [this]() { MapBudgetDraws = MapBudgetPrims = 0; }, 2.f, false);
		// Well before the move on: a screenshot is written at the end of its frame.
		FTimerHandle Shot;
		const FString Name = FString::Printf(TEXT("map_%s_view%d"), *Map, MapBudgetStop + 1);
		GetWorldTimerManager().SetTimer(Shot, [this, Name]() { TestScreenshot(Name); }, 2.5f, false);
		GetWorldTimerManager().SetTimer(TestMapTimer, this, &ACSPlayerController::MapAuditBudgetStep, 3.f, false);
		return;
	}
	FCoreDelegates::OnEndFrame.Remove(MapBudgetFrameHandle);

	FTextureMemoryStats Tex;
	RHIGetTextureMemoryStats(Tex);
	const IRenderAssetStreamingManager& Streaming = IStreamingManager::Get().GetTextureStreamingManager();
	const int64 Pool = Streaming.GetPoolSize();
	const int64 Wanted = Streaming.GetRequiredPoolSize();
	const bool bTexOk = Pool <= 0 || Wanted <= Pool;
	const bool bOk = bTexOk && MapBudgetWorstDraws > 0 && MapBudgetWorstDraws <= DrawBudget && MapBudgetWorstPrims <= PrimBudget;
	UE_LOG(LogCS, Log, TEXT("MAP AUDIT RESULT: %s worst view %d draw calls (budget %d), %d primitives (budget %d); textures: streaming wants %.0f MB of a %.0f MB pool, %.0f MB resident -> %s"),
		*Map, MapBudgetWorstDraws, DrawBudget, MapBudgetWorstPrims, PrimBudget,
		Wanted / 1048576.0, Pool / 1048576.0, (Tex.StreamingMemorySize + Tex.NonStreamingMemorySize) / 1048576.0,
		bOk ? TEXT("BUDGET OK") : TEXT("BUDGET BROKEN"));
	GNodes.Reset();
	UE_LOG(LogCS, Log, TEXT("MAP AUDIT: done."));
#endif
}
