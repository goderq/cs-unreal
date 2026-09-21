// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// One place that loads, applies and saves every local setting.
//
//   Graphics  -> UGameUserSettings (GameUserSettings.ini, engine-managed)
//   Everything else -> UCSSettingsSave in the "CSSettings" save slot
//
// Consumers read from here and listen to OnPreferencesChanged instead of
// caching values: the character re-reads FOV and sensitivity, and rebuilds
// its input mapping when a key is rebound.

#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericWindow.h"
#include "Settings/CSSettingsSave.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CSSettingsSubsystem.generated.h"

/** Snapshot of the graphics options the settings screen exposes. */
struct CSFUSION_API FCSGraphicsSettings
{
	FIntPoint Resolution = FIntPoint(1920, 1080);
	EWindowMode::Type WindowMode = EWindowMode::WindowedFullscreen;

	/** 0 = unlimited. */
	float FrameRateLimit = 0.f;

	/** Overall scalability 0 (Low) .. 3 (Epic). */
	int32 Quality = 3;

	bool bVSync = false;
};

DECLARE_MULTICAST_DELEGATE(FCSPreferencesChanged);

UCLASS()
class CSFUSION_API UCSSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	static UCSSettingsSubsystem* Get(const UObject* WorldContextObject);

	// --- Preferences (save slot) --------------------------------------------

	const FCSPlayerPreferences& GetPreferences() const { return Preferences; }

	/** Replaces, applies and (optionally) saves the preferences. */
	void SetPreferences(const FCSPlayerPreferences& NewPreferences, bool bSave = true);

	/** Key for a rebindable binding: the player's override, else Default. */
	FKey GetKeyFor(FName BindingId, const FKey& Default) const;

	/** Re-applies volumes to the current world (call when a map starts). */
	void ReapplyAudio() const { ApplyAudio(); }

	FCSPreferencesChanged OnPreferencesChanged;

	// --- Graphics (UGameUserSettings) ---------------------------------------

	FCSGraphicsSettings GetGraphics() const;
	void ApplyGraphics(const FCSGraphicsSettings& Graphics);

	/** Resolutions the monitor supports, smallest first, de-duplicated. */
	static TArray<FIntPoint> GetSupportedResolutions();

	/** Choices the frame-limit selector offers (0 = unlimited). */
	static const TArray<float>& GetFrameRateChoices();

	// --- Defaults ------------------------------------------------------------

	static FCSPlayerPreferences DefaultPreferences() { return FCSPlayerPreferences(); }

	/** Clamps every field to its legal range and drops invalid key overrides. */
	static void SanitizePreferences(FCSPlayerPreferences& InOut);
	static FCSGraphicsSettings DefaultGraphics();

	/** Name of the save slot, for tests and docs. */
	static const TCHAR* SlotName() { return TEXT("CSSettings"); }

private:
	void LoadPreferences();
	void SavePreferences() const;
	void ApplyAudio() const;

	FCSPlayerPreferences Preferences;
};
