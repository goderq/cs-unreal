// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// In-match HUD: HP, armor, crosshair, ammo, current weapon, quick slots,
// kill feed, interaction text, hit marker, damage direction and match timer.
//
// Drawn on the Canvas rather than with widgets: it redraws every frame from
// replicated state anyway, Canvas has no layout or invalidation cost, and a
// HUD made of a few rectangles and strings is exactly what it is good at.
// Everything is laid out for 1080p and multiplied by ClipY / 1080, so it
// keeps its proportions at 1440p and 4K.
//
// Everything shown is READ from replicated authoritative state (the
// Master-Client-owned ACSMatchDirector, ACSPlayerInventory and ACSGameState)
// or from the cosmetic combat events the director broadcasts. The HUD never
// computes gameplay values of its own.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CSHUD.generated.h"

class ACSMatchDirector;
struct FCSCombatEvent;

UCLASS()
class CSFUSION_API ACSHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Number of kill feed entries currently shown (self-tests read this). */
	int32 GetKillFeedCount() const { return KillFeed.Num(); }

	/** Seconds since the last hit marker, or a large number. */
	double GetSecondsSinceHitMarker() const;

	// --- Round flow (v1.0), read by the -cstestround self-test ---
	const FString& GetBannerTitle() const { return BannerTitle; }
	const FString& GetBannerSubtitle() const { return BannerSubtitle; }
	bool WasScoreboardDrawn() const { return bScoreboardDrawn; }
	int32 GetScoreboardRowCount() const { return ScoreboardRows; }

	/** Records with a player, best first: most kills, then fewest deaths, then id. */
	static TArray<struct FCSPlayerCombatRecord> SortedScores(const ACSMatchDirector* Director);

protected:
	void BindToDirector();
	void HandleCombatEvent(const FCSCombatEvent& Event);

	void DrawCrosshair();
	void DrawScope();
	void DrawHitMarker();
	void DrawDamageIndicators();
	void DrawVitals();
	void DrawAmmo();
	void DrawMatchInfo();
	void DrawKillFeed();
	void DrawDeathOverlay(float SecondsToRespawn);
	void DrawInteractionPrompt();
	void DrawQuickSlots();
	void DrawFpsCounter();
	double SmoothedFrameMs = 0.0;

	/** Phase banner and scoreboard, drawn last so they sit on top. */
	void DrawRoundOverlays();
	bool ShouldShowScoreboard() const;
	void UpdatePhaseBanner();
	void DrawPhaseBanner();
	void DrawScoreboard();

	uint8 LastSeenPhase = 0xFF;
	double BannerTime = -1000.0;
	FString BannerTitle;
	FString BannerSubtitle;
	bool bScoreboardDrawn = false;
	int32 ScoreboardRows = 0;

	/** Text with a soft shadow. X/Y and Scale are in 1080p units. */
	void DrawLabel(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale = 1.f,
		bool bLarge = false, float AlignX = 0.f);

	/** Rectangle in 1080p units. */
	void DrawBox(const FLinearColor& Color, float X, float Y, float W, float H);

	float TextWidth(const FString& Text, float Scale, bool bLarge) const;

	FString PlayerLabel(int32 PlayerId) const;
	int32 GetLocalPlayerId() const;

	UPROPERTY(EditDefaultsOnly, Category = "CS|HUD")
	FLinearColor CrosshairColor = FLinearColor(0.2f, 1.f, 0.45f, 0.95f);

	UPROPERTY(EditDefaultsOnly, Category = "CS|HUD")
	float CrosshairGap = 6.f;

	UPROPERTY(EditDefaultsOnly, Category = "CS|HUD")
	float CrosshairLength = 9.f;

	UPROPERTY(EditDefaultsOnly, Category = "CS|HUD")
	float KillFeedSeconds = 6.f;

	UPROPERTY(EditDefaultsOnly, Category = "CS|HUD")
	int32 KillFeedMaxEntries = 5;

private:
	struct FKillFeedEntry
	{
		int32 KillerId = 0;
		int32 VictimId = 0;
		FString Weapon;
		bool bHeadshot = false;
		double Time = 0.0;
	};

	struct FDamageIndicator
	{
		FVector From = FVector::ZeroVector;
		float Damage = 0.f;
		double Time = 0.0;
	};

	TArray<FKillFeedEntry> KillFeed;
	TArray<FDamageIndicator> DamageIndicators;

	double HitMarkerTime = -1000.0;
	bool bHitMarkerKill = false;
	bool bHitMarkerHead = false;

	/** Current frame's scale: ClipY / 1080. */
	float S = 1.f;

	TWeakObjectPtr<ACSMatchDirector> BoundDirector;
	FDelegateHandle CombatEventHandle;
};
