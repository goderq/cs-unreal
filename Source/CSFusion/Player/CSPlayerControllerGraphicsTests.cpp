// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v2.0 phase 5 graphics self-test, enabled with -cstestgraphics
// (Scripts/run_tests.ps1 suite "graphics"). Offline, on the real level:
//
//   1. what the PC offers: SM6, hardware ray tracing, DLSS (and why not);
//   2. every preset Low..Cinematic at native resolution with TSR;
//   3. at Epic: TAA, TSR, TSR Performance, and DLSS Quality / Performance
//      where DLSS runs - the plugin itself must report it enabled;
//   4. at Epic: hardware ray tracing on and off where the GPU allows it,
//      otherwise the fallback to software Lumen.
//
// Each case goes through UCSGameUserSettings::ApplyNonResolutionSettings, the
// path the settings screen uses, then waits, samples frame and GPU times and
// takes a screenshot (Saved/CSTest/gfx_<case>.png). The window size and the
// saved GameUserSettings.ini are left alone: the saved settings are read back
// at the end.
//
// Result lines: "GRAPHICS TEST RESULT: <case> ... -> <CHECK> OK" or a failure word.

#include "Player/CSPlayerController.h"

#include "Core/CSLog.h"
#include "Graphics/CSGraphics.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "RenderUtils.h"
#include "RHI.h"
#include "SceneManagement.h"   // DoesProjectSupportDistanceFields
#include "Settings/CSGameUserSettings.h"
#include "TimerManager.h"

namespace
{
	struct FGraphicsCase
	{
		FString Label;
		int32 Preset;
		ECSUpscaler Upscaler;
		ECSRenderScale Scale;
		bool bRayTracing;
	};

	TArray<FGraphicsCase> GCases;
	TArray<float> GFrames;
	double GGpuMs = 0.0;

	int32 CVarInt(const TCHAR* Name)
	{
		const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
		return CVar ? CVar->GetInt() : -1;
	}

	float CVarFloat(const TCHAR* Name)
	{
		const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
		return CVar ? CVar->GetFloat() : -1.f;
	}
}

void ACSPlayerController::CSTestGraphics()
{
	CS_SELF_TEST_ONLY();
#if !UE_BUILD_SHIPPING
	UCSGameUserSettings* Settings = UCSGameUserSettings::Get();
	if (TestRetryUntil(Settings != nullptr && GetPawn() != nullptr, TestGraphicsTimer, &ACSPlayerController::CSTestGraphics, TEXT("the game user settings")))
	{
		return;
	}
	if (!Settings)
	{
		UE_LOG(LogCS, Log, TEXT("GRAPHICS TEST RESULT: GameUserSettings is not UCSGameUserSettings -> SETTINGS CLASS MISSING"));
		UE_LOG(LogCS, Log, TEXT("GRAPHICS TEST: done."));
		return;
	}

	const FCSGraphicsCaps& Caps = CSGraphics::GetCaps();
	UE_LOG(LogCS, Log, TEXT("GRAPHICS TEST RESULT: %s -> %s"), *CSGraphics::Describe(Caps), Caps.bSM6 ? TEXT("SM6 OK") : TEXT("SM6 MISSING"));
	// Software Lumen needs mesh distance fields: without them GI and reflections
	// have nothing to trace whenever hardware ray tracing is off.
	const bool bDistanceFields = DoesProjectSupportDistanceFields();
	UE_LOG(LogCS, Log, TEXT("GRAPHICS TEST RESULT: mesh distance fields for software Lumen %s -> %s"),
		bDistanceFields ? TEXT("built") : TEXT("NOT built"), bDistanceFields ? TEXT("LUMEN DATA OK") : TEXT("LUMEN DATA MISSING"));

	GCases.Reset();
	const TCHAR* PresetNames[] = { TEXT("low"), TEXT("medium"), TEXT("high"), TEXT("epic"), TEXT("cinematic") };
	// Cinematic runs last: on a 4 GB GPU it overfills video memory, and the
	// cases after it measured the aftermath (TAA at 18 FPS once).
	for (int32 p = 0; p < 4; ++p)
	{
		GCases.Add({ FString::Printf(TEXT("preset_%s"), PresetNames[p]), p, ECSUpscaler::TSR, ECSRenderScale::Native, false });
	}
	GCases.Add({ TEXT("taa"), 3, ECSUpscaler::TAA, ECSRenderScale::Native, false });
	GCases.Add({ TEXT("tsr_performance"), 3, ECSUpscaler::TSR, ECSRenderScale::Performance, false });
	if (Caps.bDLSS)
	{
		GCases.Add({ TEXT("dlss_quality"), 3, ECSUpscaler::DLSS, ECSRenderScale::Quality, false });
		GCases.Add({ TEXT("dlss_performance"), 3, ECSUpscaler::DLSS, ECSRenderScale::Performance, false });
	}
	else
	{
		// Asked for DLSS without it: must end up on TSR, never on nothing.
		GCases.Add({ TEXT("dlss_fallback"), 3, ECSUpscaler::DLSS, ECSRenderScale::Quality, false });
	}
	GCases.Add({ TEXT("raytracing"), 3, ECSUpscaler::TSR, ECSRenderScale::Native, true });
	GCases.Add({ FString::Printf(TEXT("preset_%s"), PresetNames[4]), 4, ECSUpscaler::TSR, ECSRenderScale::Native, false });
	GraphicsCase = -1;

	TWeakObjectPtr<ACSPlayerController> WeakThis(this);
	FCoreDelegates::OnEndFrame.Remove(GraphicsFrameHandle);
	GraphicsFrameHandle = FCoreDelegates::OnEndFrame.AddLambda([WeakThis]()
	{
		if (WeakThis.IsValid() && WeakThis->bGraphicsSampling)
		{
			GFrames.Add(static_cast<float>(FApp::GetDeltaTime() * 1000.0));
			GGpuMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
		}
	});
	GraphicsTestStep();
#endif
}

void ACSPlayerController::GraphicsTestStep()
{
#if !UE_BUILD_SHIPPING
	UCSGameUserSettings* Settings = UCSGameUserSettings::Get();
	if (!Settings)
	{
		return;
	}

	// Judge the case that just ran.
	if (GCases.IsValidIndex(GraphicsCase))
	{
		bGraphicsSampling = false;
		const FGraphicsCase& C = GCases[GraphicsCase];
		const int32 N = GFrames.Num();
		TArray<float> Sorted = GFrames;
		Sorted.Sort();
		double Sum = 0.0;
		for (const float Ms : GFrames)
		{
			Sum += Ms;
		}
		const float Avg = N ? static_cast<float>(Sum / N) : 0.f;
		const float P95 = N ? Sorted[FMath::Min(N - 1, FMath::FloorToInt(N * 0.95f))] : 0.f;
		const FString Timing = FString::Printf(TEXT("%.0f FPS (avg %.2f ms, p95 %.2f ms, GPU %.2f ms, %d frames)"),
			Avg > 0.f ? 1000.f / Avg : 0.f, Avg, P95, N ? GGpuMs / N : 0.0, N);

		const FCSGraphicsCaps& Caps = CSGraphics::GetCaps();
		const ECSUpscaler Used = CSGraphics::Effective(C.Upscaler);
		const int32 Method = CVarInt(TEXT("r.AntiAliasingMethod"));
		const float Percent = CVarFloat(TEXT("r.ScreenPercentage"));
		const bool bMethodOk = Method == (Used == ECSUpscaler::TAA ? 2 : 4);
		const bool bPercentOk = FMath::IsNearlyEqual(Percent, CSGraphics::ScreenPercentage(C.Scale), 0.6f);
		// Group by group: the engine's overall level also needs the render
		// resolution to match the preset's default, so it reads -1 at 67 %.
		const Scalability::FQualityLevels& Q = Settings->ScalabilityQuality;
		const int32 Levels[] = { Q.ViewDistanceQuality, Q.AntiAliasingQuality, Q.ShadowQuality, Q.GlobalIlluminationQuality,
			Q.ReflectionQuality, Q.PostProcessQuality, Q.TextureQuality, Q.EffectsQuality, Q.ShadingQuality };
		bool bPresetOk = true;
		for (const int32 Level : Levels)
		{
			bPresetOk &= Level == C.Preset;
		}
		const bool bRTWanted = C.bRayTracing && Caps.bRayTracing;
		const bool bRTOk = IsRayTracingEnabled() == bRTWanted && (CVarInt(TEXT("r.Lumen.HardwareRayTracing")) == 1) == bRTWanted;
		const bool bDLSSRunning = CSGraphics::IsDLSSRunning();
		const bool bDLSSOk = bDLSSRunning == (Used == ECSUpscaler::DLSS);
		const bool bOk = bMethodOk && bPercentOk && bPresetOk && bRTOk && bDLSSOk && N > 0;

		FString Verdict;
		if (C.Label.StartsWith(TEXT("dlss_fallback")))
		{
			Verdict = (bOk && Used == ECSUpscaler::TSR) ? TEXT("DLSS FALLBACK OK") : TEXT("DLSS FALLBACK BROKEN");
		}
		else if (C.Upscaler == ECSUpscaler::DLSS)
		{
			Verdict = bOk ? TEXT("DLSS OK") : TEXT("DLSS BROKEN");
		}
		else if (C.bRayTracing)
		{
			Verdict = bOk ? (Caps.bRayTracing ? TEXT("RAY TRACING OK") : TEXT("RAY TRACING FALLBACK OK"))
				: TEXT("RAY TRACING BROKEN");
		}
		else
		{
			Verdict = bOk ? TEXT("GRAPHICS OK") : TEXT("GRAPHICS BROKEN");
		}
		UE_LOG(LogCS, Log, TEXT("GRAPHICS TEST RESULT: %s: preset %d (groups %s), AA method %d, render %.0f%%, DLSS running %s, ray tracing %s (Lumen HWRT %d), %s -> %s"),
			*C.Label, C.Preset, bPresetOk ? TEXT("match") : TEXT("DIFFER"), Method, Percent, bDLSSRunning ? TEXT("yes") : TEXT("no"),
			IsRayTracingEnabled() ? TEXT("on") : TEXT("off"), CVarInt(TEXT("r.Lumen.HardwareRayTracing")), *Timing, *Verdict);
	}

	++GraphicsCase;
	if (!GCases.IsValidIndex(GraphicsCase))
	{
		FCoreDelegates::OnEndFrame.Remove(GraphicsFrameHandle);
		// Back to what this PC had saved.
		Settings->LoadSettings(/*bForceReload*/ true);
		Settings->ApplyNonResolutionSettings();
		UE_LOG(LogCS, Log, TEXT("GRAPHICS TEST: done."));
		return;
	}

	const FGraphicsCase& Next = GCases[GraphicsCase];
	Settings->SetOverallScalabilityLevel(Next.Preset);
	Settings->GraphicsPreset = Next.Preset;
	Settings->Upscaler = static_cast<uint8>(Next.Upscaler);
	Settings->RenderScale = static_cast<uint8>(Next.Scale);
	Settings->bRayTracing = Next.bRayTracing;
	Settings->ApplyNonResolutionSettings();

	// Shaders and streaming settle, then 4 s of frames, the screenshot in the middle.
	const FString Shot = FString::Printf(TEXT("gfx_%s"), *Next.Label);
	FTimerHandle Start;
	GetWorldTimerManager().SetTimer(Start, [this]()
	{
		GFrames.Reset();
		GGpuMs = 0.0;
		bGraphicsSampling = true;
	}, 4.f, false);
	FTimerHandle ShotTimer;
	GetWorldTimerManager().SetTimer(ShotTimer, [this, Shot]() { TestScreenshot(Shot); }, 3.5f, false);
	GetWorldTimerManager().SetTimer(TestGraphicsTimer, this, &ACSPlayerController::GraphicsTestStep, 8.f, false);
#endif
}
