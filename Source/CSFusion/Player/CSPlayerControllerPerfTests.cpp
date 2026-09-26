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

#include "Player/CSPlayerController.h"

#include "Characters/CSCharacter.h"
#include "Core/CSLog.h"
#include "EngineUtils.h"
#include "Misc/CoreDelegates.h"
#include "RenderCore.h"
#include "RHI.h"
#include "RHICommandList.h"   // GInputLatencyTime
#include "TimerManager.h"

namespace
{
	constexpr float PerfSampleSeconds = 20.f;
	double PerfGameMs = 0.0;
	double PerfRenderMs = 0.0;
	double PerfGpuMs = 0.0;
	double PerfLatencyMs = 0.0;
	int32 PerfLatencySamples = 0;
}

void ACSPlayerController::CSTestPerf()
{
	CS_SELF_TEST_ONLY();
	PerfFrameMs.Reset();
	PerfGameMs = PerfRenderMs = PerfGpuMs = 0.0;
	PerfLatencyMs = 0.0;
	PerfLatencySamples = 0;
	FCoreDelegates::OnEndFrame.Remove(TestPerfTickHandle);

	TWeakObjectPtr<ACSPlayerController> WeakThis(this);
	TestPerfTickHandle = FCoreDelegates::OnEndFrame.AddLambda([WeakThis]()
	{
		if (ACSPlayerController* PC = WeakThis.Get())
		{
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
	UE_LOG(LogCS, Log, TEXT("PERF TEST: sampling %.0f s..."), PerfSampleSeconds);

	GetWorldTimerManager().SetTimer(TestPerfTimer, [this]()
	{
		FCoreDelegates::OnEndFrame.Remove(TestPerfTickHandle);
		TestPerfTickHandle.Reset();
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
		const double Estimate = (PerfGameMs + PerfRenderMs + PerfGpuMs) / N;
		UE_LOG(LogCS, Log, TEXT("PERF TEST RESULT: latency estimate %.1f ms (game + render + GPU of one frame); engine input-to-vblank %s"),
			Estimate, PerfLatencySamples > 0 ? *FString::Printf(TEXT("%.1f ms (%d frames)"), PerfLatencyMs / PerfLatencySamples, PerfLatencySamples)
				: TEXT("not reported by the driver in this window mode"));
	}, PerfSampleSeconds, false);
}
