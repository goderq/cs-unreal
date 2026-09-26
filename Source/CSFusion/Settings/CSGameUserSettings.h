// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v2.0 phase 5: the engine's GameUserSettings plus the graphics options it
// does not have - preset, upscaler, render resolution, hardware ray tracing.
// Saved in GameUserSettings.ini with the rest (DefaultEngine.ini points
// GameUserSettingsClassName here), applied with every ApplySettings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "CSGameUserSettings.generated.h"

UCLASS(Config = GameUserSettings, ConfigDoNotCheckDefaults)
class CSFUSION_API UCSGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	/** 0 Low .. 4 Cinematic; 5 = Custom (the groups were set one by one). */
	UPROPERTY(Config)
	int32 GraphicsPreset = 2;

	/** ECSUpscaler: 0 DLSS, 1 TSR, 2 TAA. */
	UPROPERTY(Config)
	uint8 Upscaler = 1;

	/** ECSRenderScale: 0 Native .. 4 Ultra Performance. */
	UPROPERTY(Config)
	uint8 RenderScale = 0;

	UPROPERTY(Config)
	bool bRayTracing = false;

	/** Set once the first-run hardware check has picked defaults for this PC. */
	UPROPERTY(Config)
	bool bAutoDetected = false;

	static UCSGameUserSettings* Get();

	virtual void ApplyNonResolutionSettings() override;
	virtual void SetToDefaults() override;
	virtual void ValidateSettings() override;

	/**
	 * First run on this PC: the engine's hardware benchmark picks the
	 * scalability levels, then DLSS where it works (Quality), otherwise TSR,
	 * and ray tracing only with 8 GB of video memory or more.
	 */
	void AutoDetect();

private:
	/**
	 * SetToDefaults ran (first start, or a GameUserSettings.ini without this
	 * class's section - v1.x). The engine then reloads the file, which takes
	 * the scalability levels back from the running engine (Epic on a fresh
	 * start) - ValidateSettings puts High back.
	 */
	bool bDefaultsPending = false;
};
