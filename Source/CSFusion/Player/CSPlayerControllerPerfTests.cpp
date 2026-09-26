// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Stage 8 performance sample (-cstestperf). Records 20 s of frame times after
// a warm-up and logs average / p95 / worst frame plus the game-thread,
// render-thread and GPU times the engine already measures (the same numbers
// `stat unit` shows). Run it with -bots=8 for the budget check in TESTS.md.
//
// v2.0 phase 5: latency on its own line, apart from FPS. The engine measures
// input sampled -> vblank only where the driver reports flip timing (usually
// exclusive fullscreen); where it does not, only the estimate is given: the
// game thread, render thread and GPU work of one frame back to back - how long
// a frame's input takes to reach the screen, not counting the display.
//
// v2.0 phase 7: video memory in use against the budget the driver gives the
// game (DXGI), and whether anything had to be moved out of it ("demoted").
// -cstestperfcmds="cmd, cmd" runs console commands right before the sample:
// -ExecCmds runs before the game applies its own graphics settings, which then
// override every cvar of a scalability group (runner -PerfCmds, A/B checks).
//
// The sample is taken through a fixed camera at the level's first player start
// (by name), eye height, looking the way the start faces. The player's own view
// moved with every death and respawn - the same build measured 43 to 57 FPS on
// Warehouse depending on where it looked. The bots fight on as before.

#include "Player/CSPlayerController.h"

#include "Characters/CSCharacter.h"
#include "Core/CSLog.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/CoreDelegates.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "RenderCore.h"
#include "RHI.h"
#include "RHICommandList.h"   // GInputLatencyTime
#include "DynamicRHI.h"        // RHIGetMemoryStats
#include "RHIStats.h"
#include "TimerManager.h"

namespace
{
	constexpr float PerfSampleSeconds = 20.f;
	double PerfGameMs = 0.0;
	double PerfRenderMs = 0.0;
	double PerfGpuMs = 0.0;
	double PerfLatencyMs = 0.0;
	int32 PerfLatencySamples = 0;
	TWeakObjectPtr<ACameraActor> PerfCamera;

	/** The benchmark view: the first player start by name, at eye height. */
	ACameraActor* SpawnPerfCamera(APlayerController* PC)
	{
		APlayerStart* First = nullptr;
		for (TActorIterator<APlayerStart> It(PC->GetWorld()); It; ++It)
		{
			if (!First || It->GetName() < First->GetName())
			{
				First = *It;
			}
		}
		if (!First)
		{
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		ACameraActor* Camera = PC->GetWorld()->SpawnActor<ACameraActor>(
			First->GetActorLocation() + FVector(0.f, 0.f, 64.f), First->GetActorRotation(), Params);
		if (Camera && PC->PlayerCameraManager)
		{
			Camera->GetCameraComponent()->SetFieldOfView(PC->PlayerCameraManager->GetFOVAngle());
		}
		UE_LOG(LogCS, Log, TEXT("PERF TEST: fixed view at %s"), *First->GetName());
		return Camera;
	}
}

void ACSPlayerController::CSTestPerf()
{
	CS_SELF_TEST_ONLY();
	PerfFrameMs.Reset();
	PerfGameMs = PerfRenderMs = PerfGpuMs = 0.0;
	PerfLatencyMs = 0.0;
	PerfLatencySamples = 0;
	FCoreDelegates::OnEndFrame.Remove(TestPerfTickHandle);

	FString Commands;
	if (GEngine && FParse::Value(FCommandLine::Get(), TEXT("cstestperfcmds="), Commands))
	{
		TArray<FString> Parts;
		Commands.ParseIntoArray(Parts, TEXT(","));
		for (const FString& Part : Parts)
		{
			UE_LOG(LogCS, Log, TEXT("PERF TEST: %s"), *Part.TrimStartAndEnd());
			GEngine->Exec(GetWorld(), *Part.TrimStartAndEnd());
		}
	}

	TWeakObjectPtr<ACSPlayerController> WeakThis(this);
	TestPerfTickHandle = FCoreDelegates::OnEndFrame.AddLambda([WeakThis]()
	{
		if (ACSPlayerController* PC = WeakThis.Get())
		{
			// A respawn gives the view back to the pawn: keep the benchmark view.
			if (PerfCamera.IsValid() && PC->GetViewTarget() != PerfCamera.Get())
			{
				PC->SetViewTarget(PerfCamera.Get());
			}
			PC->PerfFrameMs.Add(static_cast<float>(FApp::GetDeltaTime() * 1000.0));
			PerfGameMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
			PerfRenderMs += FPlatformTime::ToMilliseconds(GRenderThreadTime);
			PerfGpuMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
			if (GInputLatencyTime > 0)
			{
				PerfLatencyMs += FPlatformTime::ToMilliseconds64(GInputLatencyTime);
				++PerfLatencySamples;
			}
		}
	});
	PerfCamera = SpawnPerfCamera(this);
	if (PerfCamera.IsValid())
	{
		SetViewTarget(PerfCamera.Get());
	}
	UE_LOG(LogCS, Log, TEXT("PERF TEST: sampling %.0f s..."), PerfSampleSeconds);
	// Phase 7: the sample as a trace region, so Scripts/analyze_trace.ps1 can
	// take its statistics over exactly these seconds (runner -Trace).
	TRACE_BEGIN_REGION(TEXT("CSPerfSample"));

	GetWorldTimerManager().SetTimer(TestPerfTimer, [this]()
	{
		FCoreDelegates::OnEndFrame.Remove(TestPerfTickHandle);
		TestPerfTickHandle.Reset();
		TRACE_END_REGION(TEXT("CSPerfSample"));
		// What was measured (Saved/CSTest/perf_<map>.png); the view stays for it.
		if (PerfCamera.IsValid())
		{
			TestScreenshot(FString::Printf(TEXT("perf_%s"), *UWorld::RemovePIEPrefix(GetWorld()->GetMapName())));
		}
		const int32 N = PerfFrameMs.Num();
		if (N == 0)
		{
			UE_LOG(LogCS, Warning, TEXT("PERF TEST RESULT: no frames sampled."));
			return;
		}

		double Sum = 0.0;
		for (const float Ms : PerfFrameMs)
		{
			Sum += Ms;
		}
		TArray<float> Sorted = PerfFrameMs;
		Sorted.Sort();
		const float P95 = Sorted[FMath::Min(N - 1, FMath::FloorToInt(N * 0.95f))];
		const float P99 = Sorted[FMath::Min(N - 1, FMath::FloorToInt(N * 0.99f))];
		const double Avg = Sum / N;

		int32 Characters = 0;
		int32 Bots = 0;
		for (TActorIterator<ACSCharacter> It(GetWorld()); It; ++It)
		{
			++Characters;
			Bots += It->IsBot() ? 1 : 0;
		}

		UE_LOG(LogCS, Log, TEXT("PERF TEST RESULT: %d frames, avg %.2f ms (%.0f FPS), p95 %.2f ms, p99 %.2f ms, worst %.2f ms | game %.2f ms, render %.2f ms, GPU %.2f ms | %d characters (%d bots)"),
			N, Avg, 1000.0 / Avg, P95, P99, Sorted.Last(), PerfGameMs / N, PerfRenderMs / N, PerfGpuMs / N, Characters, Bots);
		FRHIMemoryStats Memory;
		RHIGetMemoryStats(Memory);
		if (Memory.IsValid())
		{
			constexpr double MB = 1024.0 * 1024.0;
			UE_LOG(LogCS, Log, TEXT("PERF TEST RESULT: video memory %.0f MB used of %.0f MB budget, %.0f MB demoted -> %s"),
				Memory.UsedLocal / MB, Memory.BudgetLocal / MB, Memory.DemotedLocal / MB,
				Memory.IsOverBudget() ? TEXT("VRAM OVER BUDGET") : TEXT("VRAM OK"));
		}
		else
		{
			UE_LOG(LogCS, Log, TEXT("PERF TEST RESULT: video memory not reported by this RHI"));
		}
		const double Estimate = (PerfGameMs + PerfRenderMs + PerfGpuMs) / N;
		UE_LOG(LogCS, Log, TEXT("PERF TEST RESULT: latency estimate %.1f ms (game + render + GPU of one frame); engine input-to-vblank %s"),
			Estimate, PerfLatencySamples > 0 ? *FString::Printf(TEXT("%.1f ms (%d frames)"), PerfLatencyMs / PerfLatencySamples, PerfLatencySamples)
				: TEXT("not reported by the driver in this window mode"));
	}, PerfSampleSeconds, false);
}
