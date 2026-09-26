// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Settings/CSGameUserSettings.h"

#include "Core/CSLog.h"
#include "Engine/Engine.h"
#include "Graphics/CSGraphics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

UCSGameUserSettings* UCSGameUserSettings::Get()
{
	return GEngine ? Cast<UCSGameUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

void UCSGameUserSettings::ApplyNonResolutionSettings()
{
	// The render resolution rides on the resolution scale the engine applies
	// with the scalability groups (r.ScreenPercentage).
	const ECSRenderScale Scale = static_cast<ECSRenderScale>(FMath::Min<uint8>(RenderScale, 4));
	ScalabilityQuality.ResolutionQuality = CSGraphics::ScreenPercentage(Scale);
#if !UE_BUILD_SHIPPING
	// Two-client self-tests (-cstestlowgfx, Scripts/run_tests.ps1): two games
	// with working Lumen on one 4 GB laptop GPU ran at 13 FPS and broke every
	// timing check. Low quality for this run only - the saved settings are put
	// back before anything can write them.
	if (FParse::Param(FCommandLine::Get(), TEXT("cstestlowgfx")))
	{
		const Scalability::FQualityLevels Saved = ScalabilityQuality;
		ScalabilityQuality.SetFromSingleQualityLevel(0);
		ScalabilityQuality.ResolutionQuality = 100.f;
		Super::ApplyNonResolutionSettings();
		ScalabilityQuality = Saved;
		CSGraphics::ApplyRendering(ECSUpscaler::TSR, ECSRenderScale::Native, false);
		return;
	}
#endif
	Super::ApplyNonResolutionSettings();
	CSGraphics::ApplyRendering(static_cast<ECSUpscaler>(FMath::Min<uint8>(Upscaler, 2)), Scale, bRayTracing);
}

void UCSGameUserSettings::SetToDefaults()
{
	Super::SetToDefaults();
	// High, not the engine's Epic: with working Lumen, Epic ran at 23 FPS in
	// 1080p on the owner's RTX 3050 Laptop and High at 54 (docs/GRAPHICS.md).
	ScalabilityQuality.SetFromSingleQualityLevel(2);
	GraphicsPreset = 2;
	Upscaler = static_cast<uint8>(ECSUpscaler::TSR);
	RenderScale = static_cast<uint8>(ECSRenderScale::Native);
	bRayTracing = false;
}

void UCSGameUserSettings::AutoDetect()
{
	RunHardwareBenchmark();
	ApplyHardwareBenchmarkResults();   // sets the scalability groups and applies them

	const FCSGraphicsCaps& Caps = CSGraphics::GetCaps();
	// The preset from the groups alone: the engine's overall level also wants the
	// resolution scale to match, which the render scale below replaces anyway.
	const Scalability::FQualityLevels& Q = ScalabilityQuality;
	const int32 Levels[] = { Q.ViewDistanceQuality, Q.AntiAliasingQuality, Q.ShadowQuality, Q.GlobalIlluminationQuality,
		Q.ReflectionQuality, Q.PostProcessQuality, Q.TextureQuality, Q.EffectsQuality, Q.ShadingQuality };
	int32 Overall = Levels[0];
	for (const int32 Level : Levels)
	{
		Overall = Level == Overall ? Overall : -1;
	}
	GraphicsPreset = Overall >= 0 ? FMath::Clamp(Overall, 0, 4) : 5;
	// Cinematic overfills a 4 GB card (13 FPS on the owner's RTX 3050 Laptop,
	// docs/GRAPHICS.md); below 6 GB the benchmark's pick stops at Epic.
	if (Caps.VideoMemoryMB > 0 && Caps.VideoMemoryMB < 6000 && GraphicsPreset == 4)
	{
		SetOverallScalabilityLevel(3);
		GraphicsPreset = 3;
	}
	if (Caps.bDLSS)
	{
		Upscaler = static_cast<uint8>(ECSUpscaler::DLSS);
		RenderScale = static_cast<uint8>(ECSRenderScale::Quality);
	}
	else
	{
		Upscaler = static_cast<uint8>(ECSUpscaler::TSR);
		RenderScale = static_cast<uint8>(Overall >= 3 || Overall < 0 ? ECSRenderScale::Native : ECSRenderScale::Quality);
	}
	bRayTracing = Caps.bRayTracing && Caps.VideoMemoryMB >= 8000;
	bAutoDetected = true;
	ApplySettings(false);
	SaveSettings();
	UE_LOG(LogCS, Log, TEXT("Graphics auto-detect: preset %d, upscaler %d, render scale %d, ray tracing %s (%s)."),
		GraphicsPreset, Upscaler, RenderScale, bRayTracing ? TEXT("on") : TEXT("off"), *CSGraphics::Describe(Caps));
}
