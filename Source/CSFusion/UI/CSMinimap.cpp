// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "UI/CSMinimap.h"

#include "CanvasItem.h"
#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Canvas.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Pickups/CSAmmoMachine.h"
#include "Core/CSLog.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UI/CSUIStyle.h"

namespace
{
	const TCHAR* MinimapMaterialPath = TEXT("/Game/UI/M_Minimap.M_Minimap");
	constexpr int32 PictureSize = 1024;

	void DrawDot(UCanvas* Canvas, const FVector2D& At, float Radius, const FLinearColor& Color)
	{
		// A small square reads as a dot at this size and costs one tile.
		FCanvasTileItem Tile(At - FVector2D(Radius), FVector2D(Radius * 2.f), Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}

	void DrawTriangle(UCanvas* Canvas, const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color)
	{
		FCanvasTriangleItem Triangle(A, B, C, GWhiteTexture);
		Triangle.SetColor(Color);
		Triangle.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Triangle);
	}
}

void UCSMinimap::Initialize(UWorld* World)
{
	if (!World)
	{
		return;
	}
	ComputeBounds(World);
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, MinimapMaterialPath))
	{
		Material = UMaterialInstanceDynamic::Create(Base, this);
	}
	// The first picture a moment after the map appears; the second once
	// texture streaming has caught up.
	NextCaptureTime = World->GetTimeSeconds() + 1.5;
}

void UCSMinimap::ComputeBounds(UWorld* World)
{
	FBox Box(ForceInit);
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		const FBox ActorBox = It->GetComponentsBoundingBox(/*bNonColliding*/ false);
		// Skip sky domes and backdrop cards: they would shrink the map to a dot.
		if (ActorBox.IsValid && ActorBox.GetExtent().GetMax() < 15000.f)
		{
			Box += ActorBox;
		}
	}
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Box += It->GetActorLocation();
	}
	if (!Box.IsValid)
	{
		return;
	}
	const FVector Size = Box.GetSize();
	Center = FVector2D(Box.GetCenter());
	Extent = FMath::Max(Size.X, Size.Y) * 0.5f * 1.04f + 200.f;
	TopZ = Box.Max.Z;
}

void UCSMinimap::Capture(UWorld* World)
{
	if (!Target)
	{
		Target = NewObject<UTextureRenderTarget2D>(this);
		Target->RenderTargetFormat = RTF_RGBA8;
		Target->ClearColor = FLinearColor(0.02f, 0.025f, 0.03f, 1.f);
		Target->InitAutoFormat(PictureSize, PictureSize);
		Target->UpdateResourceImmediate(true);
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// Straight down, yaw 0: world +X is up in the picture and +Y is right.
	ASceneCapture2D* Camera = World->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(),
		FVector(Center.X, Center.Y, TopZ + 4000.f), FRotator(-90.f, 0.f, 0.f), Params);
	if (!Camera)
	{
		return;
	}
	USceneCaptureComponent2D* Capture = Camera->GetCaptureComponent2D();
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	Capture->OrthoWidth = Extent * 2.f;
	Capture->TextureTarget = Target;
	// Base colour: a clean, evenly lit plan that reads well at a glance and
	// does not depend on how the renderer handles an orthographic view.
	Capture->CaptureSource = ESceneCaptureSource::SCS_BaseColor;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetParticles(false);
	Capture->CaptureScene();
	Camera->Destroy();

	if (Material)
	{
		Material->SetTextureParameterValue(TEXT("Map"), Target);
	}
	bCaptured = true;
	++Captures;

	// Self-test aid: -cstestminimap writes the picture next to the other screenshots.
	if (!UE_BUILD_SHIPPING && Captures == 2 && FParse::Param(FCommandLine::Get(), TEXT("cstestminimap")))
	{
		UKismetRenderingLibrary::ExportRenderTarget(World, Target, FPaths::ProjectSavedDir() / TEXT("CSTest"), TEXT("minimap_full.png"));
		UE_LOG(LogCS, Log, TEXT("MINIMAP: picture %.0f x %.0f cm around (%.0f, %.0f) exported."), Extent * 2.f, Extent * 2.f, Center.X, Center.Y);
	}
}

FVector2D UCSMinimap::ToUV(const FVector& World) const
{
	return FVector2D(0.5f + (World.Y - Center.Y) / (2.f * Extent), 0.5f - (World.X - Center.X) / (2.f * Extent));
}

void UCSMinimap::Draw(UCanvas* Canvas, float X, float Y, float Size, float Scale,
	const ACSCharacter* Viewer, const ACSMatchDirector* Director)
{
	UWorld* World = Viewer ? Viewer->GetWorld() : nullptr;
	if (!Canvas || !World)
	{
		return;
	}
	if (Captures < 2 && World->GetTimeSeconds() >= NextCaptureTime)
	{
		Capture(World);
		NextCaptureTime = World->GetTimeSeconds() + 6.0;
	}

	const FVector Here = Viewer->GetActorLocation();
	const float Yaw = FRotator::NormalizeAxis(Viewer->GetControlRotation().Yaw);
	const float YawRad = FMath::DegreesToRadians(Yaw);
	const FVector2D Mid(X + Size * 0.5f, Y + Size * 0.5f);
	const float Radius = Size * 0.5f;

	// The picture, through the round rotating window of M_Minimap.
	if (Material && bCaptured)
	{
		const FVector2D UV = ToUV(Here);
		Material->SetScalarParameterValue(TEXT("CenterU"), UV.X);
		Material->SetScalarParameterValue(TEXT("CenterV"), UV.Y);
		Material->SetScalarParameterValue(TEXT("Span"), ViewRadius / (2.f * Extent));
		Material->SetScalarParameterValue(TEXT("Angle"), YawRad);
		FCanvasTileItem Tile(FVector2D(X, Y), Material->GetRenderProxy(), FVector2D(Size));
		Canvas->DrawItem(Tile);
	}
	else
	{
		DrawDot(Canvas, Mid, Radius * 0.7f, FLinearColor(0.02f, 0.025f, 0.03f, 0.6f));
	}

	// World -> minimap: forward is up, right is right.
	const FVector Forward(FMath::Cos(YawRad), FMath::Sin(YawRad), 0.f);
	const FVector Right(-Forward.Y, Forward.X, 0.f);
	auto Project = [&](const FVector& World, bool bClampToEdge, FVector2D& Out) -> bool
	{
		const FVector Delta = World - Here;
		FVector2D P(FVector::DotProduct(Delta, Right), -FVector::DotProduct(Delta, Forward));
		P /= ViewRadius;
		const float Length = P.Size();
		if (Length > 0.94f)
		{
			if (!bClampToEdge)
			{
				return false;
			}
			P *= 0.94f / Length;
		}
		Out = Mid + P * Radius;
		return true;
	};

	// Ammo machines: always shown, pinned to the rim when out of range.
	for (const ACSAmmoMachine* Machine : ACSAmmoMachine::GetAllSorted(World))
	{
		FVector2D At;
		if (Project(Machine->GetActorLocation(), /*bClampToEdge*/ true, At))
		{
			const float R = 5.f * Scale;
			DrawDot(Canvas, At, R + 1.5f * Scale, FLinearColor(0.f, 0.f, 0.f, 0.8f));
			DrawDot(Canvas, At, R, CSUI::Accent);
			DrawDot(Canvas, At, R * 0.4f, FLinearColor(0.1f, 0.06f, 0.02f, 1.f));
		}
	}

	// Other players: teammates always, enemies only while in the viewer's sight.
	const int32 Me = Viewer->GetOwningPlayerId();
	FVector Eye;
	FVector Unused;
	Viewer->GetAimRay(Eye, Unused);
	FCollisionQueryParams SightParams(SCENE_QUERY_STAT(CSMinimapSight), false, Viewer);
	for (TActorIterator<ACSCharacter> It(World); It; ++It)
	{
		const ACSCharacter* Other = *It;
		if (Other == Viewer || !Other->IsAliveAuthoritative())
		{
			continue;
		}
		const bool bMate = Director && Director->AreTeammates(Me, Other->GetOwningPlayerId());
		if (!bMate)
		{
			SightParams.ClearIgnoredSourceObjects();
			SightParams.AddIgnoredActor(Viewer);
			SightParams.AddIgnoredActor(Other);
			if (World->LineTraceTestByChannel(Eye, Other->GetActorLocation() + FVector(0.f, 0.f, 40.f), ECC_Visibility, SightParams))
			{
				continue;
			}
		}
		FVector2D At;
		if (Project(Other->GetActorLocation(), /*bClampToEdge*/ false, At))
		{
			const FLinearColor Color = bMate
				? CSUI::TeamColor(static_cast<uint8>(Director->GetTeam(Other->GetOwningPlayerId())))
				: CSUI::Danger;
			DrawDot(Canvas, At, 5.f * Scale, FLinearColor(0.f, 0.f, 0.f, 0.8f));
			DrawDot(Canvas, At, 3.8f * Scale, Color);
		}
	}

	// The player: an arrow in the middle, always pointing up.
	const float A = 8.f * Scale;
	DrawTriangle(Canvas, Mid + FVector2D(0.f, -A * 1.2f), Mid + FVector2D(-A * 0.8f, A * 0.8f), Mid + FVector2D(A * 0.8f, A * 0.8f),
		FLinearColor(0.f, 0.f, 0.f, 0.85f));
	DrawTriangle(Canvas, Mid + FVector2D(0.f, -A), Mid + FVector2D(-A * 0.6f, A * 0.6f), Mid + FVector2D(A * 0.6f, A * 0.6f),
		CSUI::Text);
}
