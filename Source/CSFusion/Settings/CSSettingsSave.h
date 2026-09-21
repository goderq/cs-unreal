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
};

UCLASS()
class CSFUSION_API UCSSettingsSave : public USaveGame
{
	GENERATED_BODY()

public:
	/** Bump when a field changes meaning, so old files can be migrated or discarded. */
	UPROPERTY()
	int32 SchemaVersion = 1;

	UPROPERTY()
	FCSPlayerPreferences Preferences;
};
