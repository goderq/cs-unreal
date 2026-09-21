// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// In-match HUD (redesigned in v1.1): score bar with the timer on top, money,
// health and armor bottom left, ammo bottom right, quick slots, kill feed,
// spawn protection / buy time indicator, round and match banners, death
// screen and the team scoreboard.
//
// Drawn on the Canvas rather than with widgets: it redraws every frame from
// replicated state anyway, and Canvas has no layout or invalidation cost.
// Text goes through Slate fonts (FCanvasTextItem + FSlateFontInfo), so it is
// crisp Roboto at any size instead of a scaled-up bitmap debug font.
// Everything is laid out for 1080p and multiplied by ClipY / 1080.
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
class ACSGameState;
struct FCSCombatEvent;
struct FSlateFontInfo;

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

	/** Short centred message ("The shop is closed"). */
	void FlashNotice(const FText& Text);

	// --- Round flow, read by the -cstestround / -cstestmodes self-tests ---
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
	void DrawVitals(const struct FCSPlayerCombatRecord& Record);
	void DrawMoney(const FCSPlayerCombatRecord& Record);
	void DrawAmmo();
	void DrawScoreBar();
	void DrawModeTag();
	void DrawKillFeed();
	void DrawDeathOverlay(const FCSPlayerCombatRecord& Record);
	void DrawInteractionPrompt();
	void DrawQuickSlots();
	void DrawShopStatus(const FCSPlayerCombatRecord& Record);
	void DrawNotice();
	void DrawFpsCounter();
	double SmoothedFrameMs = 0.0;

	/** Phase banner and scoreboard, drawn last so they sit on top. */
	void DrawRoundOverlays();
	bool ShouldShowScoreboard() const;
	void UpdatePhaseBanner();
	void DrawPhaseBanner();
	void DrawScoreboard();
	/** One table (a team, or everybody in free for all). Returns the height used. */
	float DrawScoreTable(const TArray<FCSPlayerCombatRecord>& Rows, float X, float Y, float Width,
		const FString& Title, const FLinearColor& Color, bool bShowMoney, int32 TeamScore);

	uint8 LastSeenPhase = 0xFF;
	int32 LastSeenRound = -1;
	bool bLastRoundDecided = false;
	double BannerTime = -1000.0;
	FString BannerTitle;
	FString BannerSubtitle;
	FLinearColor BannerColor = FLinearColor::White;
	bool bScoreboardDrawn = false;
	int32 ScoreboardRows = 0;

	// --- Drawing helpers (all positions and sizes in 1080p units) ---

	/** Roboto at Size pixels (1080p), scaled to the current resolution. */
	FSlateFontInfo HudFont(float Size, bool bBold) const;

	/** Text with a soft shadow. AlignX: 0 left, 0.5 centre, 1 right. */
	void DrawLabel(const FString& Text, float X, float Y, const FLinearColor& Color, float Size = 18.f,
		bool bBold = false, float AlignX = 0.f);

	float TextWidth(const FString& Text, float Size, bool bBold) const;

	void DrawBox(const FLinearColor& Color, float X, float Y, float W, float H);

	/** 1px-ish outline. */
	void DrawFrame(const FLinearColor& Color, float X, float Y, float W, float H, float T = 1.5f);

	FString PlayerLabel(int32 PlayerId) const;
	int32 GetLocalPlayerId() const;
	const ACSGameState* GetCSGameState() const;
	/** Colour a player is drawn in: team colour, or you / others in free for all. */
	FLinearColor PlayerColor(int32 PlayerId) const;

	UPROPERTY(EditDefaultsOnly, Category = "CS|HUD")
	FLinearColor CrosshairColor = FLinearColor(0.35f, 1.f, 0.55f, 0.95f);

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

	/** Who killed the local player last, for the death screen. */
	int32 LastKillerId = 0;
	FString LastKillerWeapon;
	bool bLastDeathHeadshot = false;

	/** Money change pop-up ("+$300"). */
	int32 LastMoney = INT32_MIN;
	int32 MoneyDelta = 0;
	double MoneyDeltaTime = -1000.0;

	FText NoticeText;
	double NoticeTime = -1000.0;

	/** Current frame's scale: ClipY / 1080. */
	float S = 1.f;

	TWeakObjectPtr<ACSMatchDirector> BoundDirector;
	FDelegateHandle CombatEventHandle;
};
