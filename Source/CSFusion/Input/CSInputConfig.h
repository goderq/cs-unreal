// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// One data asset holding every Enhanced Input binding the character needs, so
// gameplay code never hard-references individual asset paths.
//
// Create the asset in the editor:
//   Content/Input/DA_CSInputConfig  (Miscellaneous > Data Asset > CSInputConfig)
//
// Scripts/bootstrap_content.py generates it along with the Input Actions and
// the Input Mapping Context.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CSInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

UCLASS(BlueprintType)
class CSFUSION_API UCSInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Applied to the local player on possession. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Input", meta = (ClampMin = "0"))
	int32 MappingPriority = 0;

	/** Axis2D: X = right/left, Y = forward/back. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Input")
	TObjectPtr<UInputAction> IA_Move;

	/** Axis2D: X = yaw, Y = pitch. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Input")
	TObjectPtr<UInputAction> IA_Look;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Input")
	TObjectPtr<UInputAction> IA_Jump;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Input")
	TObjectPtr<UInputAction> IA_Sprint;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Input")
	TObjectPtr<UInputAction> IA_Crouch;

	// --- Reserved for Stage 2+ (declared now so the asset does not churn) ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Input|Combat")
	TObjectPtr<UInputAction> IA_Fire;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Input|Combat")
	TObjectPtr<UInputAction> IA_Aim;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Input|Combat")
	TObjectPtr<UInputAction> IA_Reload;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Input|Inventory")
	TObjectPtr<UInputAction> IA_Interact;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Input|Inventory")
	TObjectPtr<UInputAction> IA_ToggleInventory;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Input|UI")
	TObjectPtr<UInputAction> IA_PauseMenu;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CS|Input|UI")
	TObjectPtr<UInputAction> IA_Scoreboard;
};
