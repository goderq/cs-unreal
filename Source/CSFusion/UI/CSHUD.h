// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Minimal Canvas HUD for playtesting combat: crosshair, health, armor, ammo,
// kills/deaths, match phase and timer, and a death overlay.
//
// Deliberately plain and cheap. Stage 5 replaces it with a UMG HUD; until then
// this is what makes Stage 2 testable - without it a player cannot tell
// whether a shot landed, how much health is left, or why they cannot fire.
//
// Everything shown is READ from replicated authoritative state (the
// Master-Client-owned ACSMatchDirector and ACSGameState). The HUD never
// computes gameplay values of its own.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CSHUD.generated.h"

UCLASS()
class CSFUSION_API ACSHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

protected:
	void DrawCrosshair();
	void DrawStatusPanel();
	void DrawMatchInfo();
	void DrawDeathOverlay(float SecondsToRespawn);
	void DrawInteractionPrompt();
	void DrawQuickSlots();

	void DrawShadowedText(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale = 1.f);

	UPROPERTY(EditDefaultsOnly, Category = "CS|HUD")
	FLinearColor CrosshairColor = FLinearColor(0.2f, 1.f, 0.4f, 0.9f);

	UPROPERTY(EditDefaultsOnly, Category = "CS|HUD")
	float CrosshairGap = 6.f;

	UPROPERTY(EditDefaultsOnly, Category = "CS|HUD")
	float CrosshairLength = 9.f;
};
