// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Local, per-machine player preferences that UGameUserSettings does not
// cover: mouse sensitivity, field of view, audio levels and key bindings.
// Graphics (resolution, window mode, frame limit, quality) are persisted by
// UGameUserSettings in GameUserSettings.ini, where the engine expects them.
//
// Nothing gameplay-relevant is ever stored here. The inventory, health and
// ammo live only in the Master Client's replicated state; a client that edits
// this file can change how its own game looks and feels, nothing more.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "InputCoreTypes.h"
#include "CSSettingsSave.generated.h"

USTRUCT(BlueprintType)
struct CSFUSION_API FCSPlayerPreferences
{
	GENERATED_BODY()

	/** Multiplier on raw mouse delta. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings", meta = (ClampMin = "0.05", ClampMax = "5.0"))
	float MouseSensitivity = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings")
	bool bInvertY = false;

	/** Stage 8: frame-rate and frame-time counter in the HUD corner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings")
	bool bShowFps = false;

	/** Horizontal field of view in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings", meta = (ClampMin = "70.0", ClampMax = "120.0"))
	float FieldOfView = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MasterVolume = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MusicVolume = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EffectsVolume = 1.f;

	/**
	 * Keys the player rebound, by binding id (see UCSInputConfig::GetRebindableBindings).
	 * Only overrides are stored; everything else uses the input config default.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings")
	TMap<FName, FKey> KeyOverrides;

	// --- v2.0 phase 6 (AUDIT C11), schema 2 -----------------------------------

	/** Extra multiplier while aiming down sights (on top of the zoom scaling). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings", meta = (ClampMin = "0.2", ClampMax = "2.0"))
	float AimSensitivity = 1.f;

	/** Aim: false = hold the button, true = press once to aim, again to stop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings")
	bool bToggleAim = false;

	/** Crosshair: 0 cross, 1 cross with a dot, 2 dot only, 3 circle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings")
	int32 CrosshairStyle = 0;

	/** Index into CSUI::CrosshairColors (0 green .. 5 red). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings")
	int32 CrosshairColor = 0;

	/** Arm length, px at 1080p. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings", meta = (ClampMin = "2", ClampMax = "20"))
	float CrosshairSize = 8.f;

	/** Gap from the centre, px at 1080p. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings", meta = (ClampMin = "0", ClampMax = "16"))
	float CrosshairGap = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings", meta = (ClampMin = "1", ClampMax = "5"))
	float CrosshairThickness = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings")
	bool bCrosshairOutline = true;

	/** The crosshair opens with the weapon's spread. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings")
	bool bCrosshairDynamic = true;

	/** 0 off, 1 protanopia, 2 deuteranopia, 3 tritanopia (engine colour correction). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings")
	int32 ColorVision = 0;

	/** Menus and HUD size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings", meta = (ClampMin = "0.8", ClampMax = "1.3"))
	float UIScale = 1.f;

	/** Weapon and camera motion (breathing, strafe tilt, lag, landing dip): 0 = still, 1 = full. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CameraMotion = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings")
	bool bMotionBlur = false;

	/** Flashbangs white out to a grey, not a full white (photosensitivity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings")
	bool bReduceFlash = false;

	/** Ping, jitter and FPS in a corner of the HUD. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CS|Settings")
	bool bShowNetStats = false;
};

UCLASS()
class CSFUSION_API UCSSettingsSave : public USaveGame
{
	GENERATED_BODY()

public:
	/**
	 * Bump when a field changes meaning, so old files can be migrated or discarded.
	 * 2 (v2.0 phase 6): the C11 options; a schema-1 file keeps every value it
	 * had and gets the new ones at their defaults.
	 */
	static constexpr int32 CurrentSchema = 2;

	UPROPERTY()
	int32 SchemaVersion = CurrentSchema;

	UPROPERTY()
	FCSPlayerPreferences Preferences;
};
