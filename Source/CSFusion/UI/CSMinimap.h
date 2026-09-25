// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v2.0 minimap (local, cosmetic).
//
// The level is photographed once from straight above with an orthographic
// scene capture into a render target - a real top-down picture of the map
// with its lighting, not a hand-drawn plan, so every map gets a minimap
// without authoring one. The HUD then draws a round window onto that picture
// through M_Minimap, centred on the player and turned with the view (forward
// is always up), and puts icons on top: the player, teammates, enemies the
// player can see, and every ammo machine.
//
// The capture repeats once a few seconds later, after texture streaming has
// caught up, so the picture is sharp.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CSMinimap.generated.h"

class ACSCharacter;
class ACSMatchDirector;
class UCanvas;
class UMaterialInstanceDynamic;
class UTextureRenderTarget2D;

UCLASS()
class CSFUSION_API UCSMinimap : public UObject
{
	GENERATED_BODY()

public:
	/** Local: find the map's extent and take the top-down picture. */
	void Initialize(UWorld* World);

	/**
	 * Draws the minimap with its top-left corner at (X, Y), Size across, in
	 * canvas pixels. Scale is the HUD's DPI scale for icon sizes.
	 */
	void Draw(UCanvas* Canvas, float X, float Y, float Size, float Scale,
		const ACSCharacter* Viewer, const ACSMatchDirector* Director);

	bool IsReady() const { return bCaptured; }

	/** Radius of the world area shown around the player, cm. */
	float ViewRadius = 2600.f;

private:
	void ComputeBounds(UWorld* World);
	void Capture(UWorld* World);
	/** World XY -> 0..1 across the captured picture. */
	FVector2D ToUV(const FVector& World) const;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Target;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	FVector2D Center = FVector2D::ZeroVector;
	float Extent = 5000.f;	// half the side of the captured square, cm
	float TopZ = 3000.f;
	bool bCaptured = false;
	int32 Captures = 0;
	double NextCaptureTime = 0.0;
};
