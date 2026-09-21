// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v1.0 weapon models: one distinct static mesh per weapon (Quaternius
// "Ultimate Gun Pack", CC0 - see docs/ASSETS.md) and the points on it the
// character needs: where the right hand grips it, where the left hand goes,
// the sight the camera aligns to when aiming, and the muzzle.
//
// Kept in Config/DefaultGame.ini rather than in the weapon data assets so the
// hand placement can be tuned without an editor session, and keyed by the
// weapon data asset's name (DA_Weapon_AK47, ...).
//
// All points are in the model's own units, as read off the side views that
// Scripts/render_weapon_profiles.py draws: X along the barrel, Z up, Y left.
//
// Hand frame. The Mannequin's HandGrip_R socket is where Epic's template
// weapons are held: their barrel runs along +Y, up is +Z, the grip at the
// origin. A model is placed in that same frame - yawed 90 degrees, scaled,
// with Grip at the socket origin - so it sits in the hand exactly as the
// Mannequin animations expect, for every model.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CSWeaponPresentation.generated.h"

class UStaticMesh;

USTRUCT()
struct CSFUSION_API FCSWeaponModel
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, Category = "Model")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** Model units to centimetres. */
	UPROPERTY(Config, EditAnywhere, Category = "Model")
	float Scale = 0.15f;

	/** Right-hand grip: this point goes to the HandGrip_R socket. */
	UPROPERTY(Config, EditAnywhere, Category = "Model")
	FVector Grip = FVector::ZeroVector;

	/** Extra rotation of the model in the hand (pitch = muzzle up), if the grip is raked. */
	UPROPERTY(Config, EditAnywhere, Category = "Model")
	FRotator GripRotation = FRotator::ZeroRotator;

	/** Left-hand target (forend, pump, or the front of the grip for pistols). */
	UPROPERTY(Config, EditAnywhere, Category = "Model")
	FVector Support = FVector::ZeroVector;

	/** The point that sits on the line of sight when aiming (rear sight, scope eyepiece). */
	UPROPERTY(Config, EditAnywhere, Category = "Model")
	FVector Sight = FVector::ZeroVector;

	UPROPERTY(Config, EditAnywhere, Category = "Model")
	FVector Muzzle = FVector::ZeroVector;

	/** Aiming: distance from the eye to the sight point, cm. */
	UPROPERTY(Config, EditAnywhere, Category = "Aim")
	float SightDistance = 18.f;

	/** Aiming: field of view as a fraction of the player's FOV. */
	UPROPERTY(Config, EditAnywhere, Category = "Aim")
	float AimFovScale = 0.8f;

	/** Aiming shows a full-screen scope instead of the model (sniper). */
	UPROPERTY(Config, EditAnywhere, Category = "Aim")
	bool bScope = false;

	/**
	 * Hip fire: where the right-hand grip sits relative to the camera, cm
	 * (X forward, Y right, Z up). Zero = leave the animation's own framing.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Aim")
	FVector HipGrip = FVector::ZeroVector;

	/** Hip fire: model rotation relative to the view (yaw = muzzle inwards). */
	UPROPERTY(Config, EditAnywhere, Category = "Aim")
	FRotator HipRotation = FRotator::ZeroRotator;

	/** How the model lies on the ground as a pickup (roll 90 = on its side). */
	UPROPERTY(Config, EditAnywhere, Category = "Pickup")
	FRotator PickupRotation = FRotator(0.f, 0.f, 90.f);

	/** Model -> HandGrip_R socket space. */
	FTransform GetMeshInHand() const;

	/** A model point (model units) in HandGrip_R socket space, cm. */
	FVector ToHand(const FVector& ModelPoint) const { return GetMeshInHand().TransformPosition(ModelPoint); }
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "CS Weapon Models"))
class CSFUSION_API UCSWeaponPresentationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Keyed by weapon data asset name, e.g. DA_Weapon_AK47. */
	UPROPERTY(Config, EditAnywhere, Category = "Models")
	TMap<FName, FCSWeaponModel> Models;

	/**
	 * First person: the arms mesh is moved this far from its eyes-at-camera
	 * placement (cm, camera space: X forward, Z up), so shoulders and chest sit
	 * below and behind the view while the hands reach the weapon by IK.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "First Person")
	FVector ArmsOffset = FVector(0.f, 0.f, -10.f);

	static const UCSWeaponPresentationSettings* Get() { return GetDefault<UCSWeaponPresentationSettings>(); }

	/** Model for a weapon data asset, or null (then the asset's own skeletal mesh is used). */
	static const FCSWeaponModel* Find(const UObject* WeaponDefinition);
};
