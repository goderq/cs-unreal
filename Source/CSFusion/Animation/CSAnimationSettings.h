// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Which animation clips the character uses. Project Settings > Game >
// CS Animation, stored in DefaultGame.ini. Defaults point at Epic's
// Mannequin animation pack (Content/Characters/Mannequins), licensed under
// the Unreal Engine EULA - see docs/ASSETS.md.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Weapons/CSWeaponDefinition.h"
#include "CSAnimationSettings.generated.h"

class UAnimSequence;

/** One weapon stance: locomotion, aim and upper-body actions. */
USTRUCT(BlueprintType)
struct CSFUSION_API FCSStanceAnimSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category = "Locomotion")
	TSoftObjectPtr<UAnimSequence> Idle;

	/** 8 directions, clockwise from forward: F, FR, R, BR, B, BL, L, FL. */
	UPROPERTY(EditAnywhere, Config, Category = "Locomotion", meta = (EditFixedSize))
	TArray<TSoftObjectPtr<UAnimSequence>> Walk;

	/** Same order as Walk. */
	UPROPERTY(EditAnywhere, Config, Category = "Locomotion", meta = (EditFixedSize))
	TArray<TSoftObjectPtr<UAnimSequence>> Jog;

	UPROPERTY(EditAnywhere, Config, Category = "Locomotion")
	TSoftObjectPtr<UAnimSequence> FallLoop;

	/** Mesh-space additive poses aiming up / down (the Mannequin AO keys). */
	UPROPERTY(EditAnywhere, Config, Category = "Aim")
	TSoftObjectPtr<UAnimSequence> AimUp;

	UPROPERTY(EditAnywhere, Config, Category = "Aim")
	TSoftObjectPtr<UAnimSequence> AimDown;

	/** Additive recoil. */
	UPROPERTY(EditAnywhere, Config, Category = "Actions")
	TSoftObjectPtr<UAnimSequence> Fire;

	/** Full upper-body poses. */
	UPROPERTY(EditAnywhere, Config, Category = "Actions")
	TSoftObjectPtr<UAnimSequence> Reload;

	UPROPERTY(EditAnywhere, Config, Category = "Actions")
	TSoftObjectPtr<UAnimSequence> Equip;

	UPROPERTY(EditAnywhere, Config, Category = "Actions")
	TSoftObjectPtr<UAnimSequence> DryFire;

	/** Ground speed (cm/s) the walk / jog clips were authored at. */
	UPROPERTY(EditAnywhere, Config, Category = "Locomotion")
	float WalkClipSpeed = 180.f;

	UPROPERTY(EditAnywhere, Config, Category = "Locomotion")
	float JogClipSpeed = 420.f;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "CS Animation"))
class CSFUSION_API UCSAnimationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UCSAnimationSettings();

	static const UCSAnimationSettings* Get() { return GetDefault<UCSAnimationSettings>(); }

	const FCSStanceAnimSet& GetStance(ECSWeaponStance Stance) const
	{
		return Stance == ECSWeaponStance::Pistol ? Pistol : Rifle;
	}

	UPROPERTY(EditAnywhere, Config, Category = "Stances")
	FCSStanceAnimSet Pistol;

	UPROPERTY(EditAnywhere, Config, Category = "Stances")
	FCSStanceAnimSet Rifle;

	/** Local-space additive hit reactions. */
	UPROPERTY(EditAnywhere, Config, Category = "Reactions")
	TSoftObjectPtr<UAnimSequence> HitReactFront;

	UPROPERTY(EditAnywhere, Config, Category = "Reactions")
	TSoftObjectPtr<UAnimSequence> HitReactBack;

	/** Death clips: front, back, left, right (by where the killing shot came from). */
	UPROPERTY(EditAnywhere, Config, Category = "Reactions", meta = (EditFixedSize))
	TArray<TSoftObjectPtr<UAnimSequence>> Death;

	/** Character meshes: Manny for even player ids, Quinn for odd. */
	UPROPERTY(EditAnywhere, Config, Category = "Meshes")
	TArray<TSoftObjectPtr<USkeletalMesh>> CharacterMeshes;
};
