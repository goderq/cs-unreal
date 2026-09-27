// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v2.1 map rework: screenshots from given places (-cstestviews=<Set>). The
// map build scripts write Saved/CSTest/views_<Set>.txt, one view per line:
//
//     label  x  y  z  pitch  yaw          (# starts a comment)
//
// Each view is shown through a fixed camera for 2 s (Lumen and TSR settle)
// and saved as Saved/CSTest/views_<Set>_<label>.png, without the HUD. Eye
// level views (z = floor + 165) are the first-person checks of the maps;
// high views show the composition.

#include "Player/CSPlayerController.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Core/CSLog.h"
#include "Engine/Engine.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

void ACSPlayerController::CSTestViews()
{
	CS_SELF_TEST_ONLY();
#if !UE_BUILD_SHIPPING
	FString Set;
	FParse::Value(FCommandLine::Get(), TEXT("cstestviews="), Set);
	const FString File = FPaths::ProjectSavedDir() / TEXT("CSTest") / FString::Printf(TEXT("views_%s.txt"), *Set);
	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *File);
	ViewPoints.Reset();
	for (const FString& Raw : Lines)
	{
		const FString Line = Raw.TrimStartAndEnd();
		TArray<FString> Parts;
		Line.ParseIntoArrayWS(Parts);
		if (Line.StartsWith(TEXT("#")) || Parts.Num() < 6)
		{
			continue;
		}
		const FVector Location(FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]), FCString::Atof(*Parts[3]));
		const FRotator Rotation(FCString::Atof(*Parts[4]), FCString::Atof(*Parts[5]), 0.f);
		ViewPoints.Add(TPair<FString, FTransform>(Parts[0], FTransform(Rotation, Location)));
	}
	UE_LOG(LogCS, Log, TEXT("VIEWS: %d view(s) from %s"), ViewPoints.Num(), *File);
	if (ViewPoints.Num() == 0)
	{
		UE_LOG(LogCS, Log, TEXT("VIEWS RESULT: no views in %s -> VIEWS MISSING"), *File);
		return;
	}

	// Scenery without the HUD, and optional console commands for debug views
	// (-cstestviewcmds="r.BufferVisualizationTarget BaseColor, ShowFlag.VisualizeBuffer 1").
	if (AHUD* Hud = GetHUD())
	{
		Hud->bShowHUD = false;
	}
	FString Commands;
	if (GEngine && FParse::Value(FCommandLine::Get(), TEXT("cstestviewcmds="), Commands))
	{
		TArray<FString> Parts;
		Commands.ParseIntoArray(Parts, TEXT(","));
		for (const FString& Part : Parts)
		{
			GEngine->Exec(GetWorld(), *Part.TrimStartAndEnd());
		}
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	ACameraActor* Camera = GetWorld()->SpawnActor<ACameraActor>(ViewPoints[0].Value.GetLocation(), ViewPoints[0].Value.Rotator(), Params);
	if (Camera)
	{
		Camera->GetCameraComponent()->SetFieldOfView(90.f);
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
	}
	ViewCamera = Camera;
	ViewIndex = -1;

	GetWorldTimerManager().SetTimer(TestViewsTimer, [this, Set]()
	{
		ACameraActor* Cam = ViewCamera.Get();
		if (!Cam)
		{
			return;
		}
		if (ViewIndex >= 0)
		{
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("CSTest") /
				FString::Printf(TEXT("views_%s_%s.png"), *Set, *ViewPoints[ViewIndex].Key));
			FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
		}
		FTimerHandle Next;
		GetWorldTimerManager().SetTimer(Next, [this]()
		{
			ACameraActor* C = ViewCamera.Get();
			if (!C)
			{
				return;
			}
			++ViewIndex;
			if (ViewIndex < ViewPoints.Num())
			{
				C->SetActorTransform(ViewPoints[ViewIndex].Value);
				SetViewTarget(C);
				return;
			}
			GetWorldTimerManager().ClearTimer(TestViewsTimer);
			UE_LOG(LogCS, Log, TEXT("VIEWS RESULT: %d screenshots -> VIEWS DONE"), ViewPoints.Num());
		}, 0.4f, false);
	}, 2.0f, true);
#endif
}
