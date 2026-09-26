// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v2.0 phase 5: what this PC's GPU can do, and the rendering options that go
// beyond the engine's scalability groups - the upscaler, the render
// resolution, hardware ray tracing (AUDIT B14).
//
// NVIDIA DLSS is optional (AUDIT K5): its plugin may not be redistributed, so
// it is not in git and this module does not link against it. When the plugin
// is in Plugins/ it registers its Blueprint library, and DLSS is driven through
// that by reflection; without it the project builds and runs the same on TSR
// or TAA, and the settings screen does not offer DLSS at all.

#pragma once

#include "CoreMinimal.h"

/** Anti-aliasing and upscaling, best first: DLSS -> TSR -> TAA. */
enum class ECSUpscaler : uint8
{
	DLSS = 0,
	TSR = 1,
	TAA = 2,
};

/** Render resolution relative to the output (DLSS names; Native with DLSS is DLAA). */
enum class ECSRenderScale : uint8
{
	Native = 0,
	Quality = 1,
	Balanced = 2,
	Performance = 3,
	UltraPerformance = 4,
};

struct CSFUSION_API FCSGraphicsCaps
{
	FString Adapter;
	int64 VideoMemoryMB = 0;

	/** Shader Model 6 in use (needed for hardware ray tracing). */
	bool bSM6 = false;

	/** The GPU, driver and project allow hardware ray tracing. */
	bool bRayTracing = false;

	/** The DLSS plugin is installed and loaded. */
	bool bDLSSPlugin = false;

	/** DLSS Super Resolution works on this PC. */
	bool bDLSS = false;

	/** Why DLSS is not available ("Supported" when it is, "Plugin not installed" without it). */
	FString DLSSStatus;
};

namespace CSGraphics
{
	/** Queried once the RHI is up; cheap afterwards. */
	CSFUSION_API const FCSGraphicsCaps& GetCaps();

	/** Percentage of the output resolution rendered for a scale. */
	CSFUSION_API float ScreenPercentage(ECSRenderScale Scale);

	/** The upscaler actually used: DLSS falls back to TSR where it is not available. */
	CSFUSION_API ECSUpscaler Effective(ECSUpscaler Wanted);

	/**
	 * Applies the upscaler, ray tracing and the anti-aliasing method. The render
	 * resolution itself goes through UGameUserSettings (resolution scale).
	 */
	CSFUSION_API void ApplyRendering(ECSUpscaler Upscaler, ECSRenderScale Scale, bool bRayTracing);

	/** Called once the engine and every plugin are up (FCoreDelegates::OnPostEngineInit). */
	CSFUSION_API void HandlePostEngineInit();

	/** True when the DLSS plugin reports DLSS as running (for the graphics self-test). */
	CSFUSION_API bool IsDLSSRunning();

	/** One line for the log and the settings screen. */
	CSFUSION_API FString Describe(const FCSGraphicsCaps& Caps);
}
