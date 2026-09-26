// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "UI/CSHUD.h"

#include "AI/CSBotManager.h"
#include "Camera/PlayerCameraManager.h"
#include "CanvasItem.h"
#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
#include "Core/CSAuthority.h"
#include "Core/CSCombatSettings.h"
#include "Core/CSLog.h"
#include "Core/CSModeSettings.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/CSGameState.h"
#include "Input/CSInputConfig.h"
#include "Inventory/CSPlayerInventory.h"
#include "Items/CSItemDefinition.h"
#include "Items/CSItemSettings.h"
#include "Items/CSShopSettings.h"
#include "Pickups/CSAmmoMachine.h"
#include "Pickups/CSWorldPickup.h"
#include "Player/CSPlayerController.h"
#include "Rendering/SlateRenderer.h"
#include "Settings/CSSettingsSubsystem.h"
#include "Styling/CoreStyle.h"
#include "UI/CSMinimap.h"
#include "UI/CSUIStyle.h"
#include "Weapons/CSWeaponComponent.h"
#include "Weapons/CSWeaponDefinition.h"

namespace
{
	// Glass panels: dark, slightly blue, translucent.
	const FLinearColor GPanel(0.010f, 0.014f, 0.024f, 0.62f);
	const FLinearColor GPanelStrong(0.008f, 0.011f, 0.020f, 0.82f);
	const FLinearColor GTrack(1.f, 1.f, 1.f, 0.10f);
	const FLinearColor GHealth(0.95f, 0.96f, 0.97f, 1.f);
	const FLinearColor GArmor(0.42f, 0.70f, 1.f, 1.f);

	constexpr double GHitMarkerSeconds = 0.22;
	constexpr double GKillMarkerSeconds = 0.45;
	constexpr double GDamageIndicatorSeconds = 1.3;

	FLinearColor WithAlpha(const FLinearColor& C, float A)
	{
		return FLinearColor(C.R, C.G, C.B, C.A * A);
	}

	FString Clock(float Seconds)
	{
		const int32 Total = FMath::Max(0, FMath::CeilToInt(Seconds));
		return FString::Printf(TEXT("%d:%02d"), Total / 60, Total % 60);
	}

	FString TeamName(ECSTeam Team)
	{
		return Team == ECSTeam::Alpha ? TEXT("ALPHA") : (Team == ECSTeam::Bravo ? TEXT("BRAVO") : TEXT(""));
	}
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void ACSHUD::BindToDirector()
{
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	if (!Director || BoundDirector.Get() == Director)
	{
		return;
	}
	if (ACSMatchDirector* Old = BoundDirector.Get())
	{
		Old->OnCombatEvent.Remove(CombatEventHandle);
	}
	BoundDirector = Director;
	CombatEventHandle = Director->OnCombatEvent.AddUObject(this, &ACSHUD::HandleCombatEvent);
}

void ACSHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ACSMatchDirector* Director = BoundDirector.Get())
	{
		Director->OnCombatEvent.Remove(CombatEventHandle);
	}
	Super::EndPlay(EndPlayReason);
}

int32 ACSHUD::GetLocalPlayerId() const
{
	const ACSCharacter* Pawn = Cast<ACSCharacter>(GetOwningPawn());
	return Pawn ? Pawn->GetOwningPlayerId() : UCSAuthority::GetLocalPlayerId(this);
}

const ACSGameState* ACSHUD::GetCSGameState() const
{
	return GetWorld() ? GetWorld()->GetGameState<ACSGameState>() : nullptr;
}

double ACSHUD::GetSecondsSinceHitMarker() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetRealTimeSeconds() - HitMarkerTime : 1000.0;
}

void ACSHUD::FlashNotice(const FText& Text)
{
	NoticeText = Text;
	NoticeTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
}

void ACSHUD::HandleCombatEvent(const FCSCombatEvent& Event)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetRealTimeSeconds();
	const int32 Me = GetLocalPlayerId();

	if (Event.bKilled)
	{
		FKillFeedEntry& Entry = KillFeed.AddDefaulted_GetRef();
		Entry.KillerId = Event.InstigatorId;
		Entry.VictimId = Event.VictimId;
		Entry.Weapon = Event.WeaponName;
		Entry.bHeadshot = Event.Zone == ECSHitZone::Head;
		Entry.Time = Now;
		while (KillFeed.Num() > KillFeedMaxEntries)
		{
			KillFeed.RemoveAt(0);
		}

		if (Event.VictimId == Me)
		{
			LastKillerId = Event.InstigatorId;
			LastKillerWeapon = Event.WeaponName;
			bLastDeathHeadshot = Event.Zone == ECSHitZone::Head;
		}
	}

	if (Event.InstigatorId == Me && Event.VictimId != Me)
	{
		HitMarkerTime = Now;
		bHitMarkerKill = Event.bKilled;
		bHitMarkerHead = Event.Zone == ECSHitZone::Head;
	}

	if (Event.VictimId == Me && Event.InstigatorId != Me)
	{
		FDamageIndicator& Indicator = DamageIndicators.AddDefaulted_GetRef();
		Indicator.From = Event.FromLocation;
		Indicator.Damage = Event.Damage;
		Indicator.Time = Now;
	}
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

FSlateFontInfo ACSHUD::HudFont(float Size, bool bBold) const
{
	// Slate sizes are points; 0.75 turns the intended pixel height at 1080p
	// into points (Slate renders 1pt as 96/72 px).
	return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", FMath::Max(5, FMath::RoundToInt(Size * 0.75f * S)));
}

void ACSHUD::DrawBox(const FLinearColor& Color, float X, float Y, float W, float H)
{
	DrawRect(Color, X * S, Y * S, W * S, H * S);
}

void ACSHUD::DrawFrame(const FLinearColor& Color, float X, float Y, float W, float H, float T)
{
	DrawBox(Color, X, Y, W, T);
	DrawBox(Color, X, Y + H - T, W, T);
	DrawBox(Color, X, Y + T, T, H - 2.f * T);
	DrawBox(Color, X + W - T, Y + T, T, H - 2.f * T);
}

float ACSHUD::TextWidth(const FString& Text, float Size, bool bBold) const
{
	if (Text.IsEmpty() || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
	{
		return 0.f;
	}
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	return Measure->Measure(Text, HudFont(Size, bBold)).X / FMath::Max(S, 0.01f);
}

void ACSHUD::DrawLabel(const FString& Text, float X, float Y, const FLinearColor& Color, float Size, bool bBold, float AlignX)
{
	if (Text.IsEmpty() || !Canvas)
	{
		return;
	}
	if (AlignX != 0.f)
	{
		X -= TextWidth(Text, Size, bBold) * AlignX;
	}
	FCanvasTextItem Item(FVector2D(X * S, Y * S), FText::FromString(Text), HudFont(Size, bBold), Color);
	// The Slate font needs a runtime-cache UFont next to it, or the item counts
	// as having no font and draws nothing (FCanvasSimpleTextItem::HasValidText).
	static TWeakObjectPtr<UFont> RuntimeFont;
	if (!RuntimeFont.IsValid())
	{
		RuntimeFont = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
	}
	Item.Font = RuntimeFont.IsValid() ? RuntimeFont.Get() : (GEngine ? GEngine->GetMediumFont() : nullptr);
	Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.55f * Color.A), FVector2D(FMath::Max(1.f, 1.5f * S)));
	Canvas->DrawItem(Item);
}

FString ACSHUD::PlayerLabel(int32 PlayerId) const
{
	if (PlayerId == GetLocalPlayerId())
	{
		return TEXT("You");
	}
	if (CSBots::IsBotId(PlayerId))
	{
		return ACSBotManager::GetBotName(PlayerId);
	}
	// v1.2: the name from their account, once their pawn has told us.
	if (const ACSCharacter* Pawn = ACSMatchDirector::FindPawnForPlayer(this, PlayerId))
	{
		if (!Pawn->GetDisplayNickname().IsEmpty())
		{
			return Pawn->GetDisplayNickname();
		}
	}
	return FString::Printf(TEXT("Player %d"), PlayerId);
}

FLinearColor ACSHUD::PlayerColor(int32 PlayerId) const
{
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	const ECSTeam Team = Director ? Director->GetTeam(PlayerId) : ECSTeam::None;
	if (Team != ECSTeam::None)
	{
		return CSUI::TeamColor(static_cast<uint8>(Team));
	}
	return PlayerId == GetLocalPlayerId() ? CSUI::Accent : CSUI::Text;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void ACSHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	S = Canvas->ClipY / 1080.f;
	if (const UCSSettingsSubsystem* UISettings = UCSSettingsSubsystem::Get(this))
	{
		S *= UISettings->GetPreferences().UIScale;   // UI scale (C11)
	}
	BindToDirector();

	const double Now = GetWorld()->GetRealTimeSeconds();
	KillFeed.RemoveAll([this, Now](const FKillFeedEntry& E) { return Now - E.Time > KillFeedSeconds; });
	DamageIndicators.RemoveAll([Now](const FDamageIndicator& D) { return Now - D.Time > GDamageIndicatorSeconds; });

	DrawScoreBar();
	DrawModeTag();
	DrawKillFeed();
	DrawFpsCounter();
	DrawNetStats();

	const ACSCharacter* Pawn = Cast<ACSCharacter>(GetOwningPawn());
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	const float W = Canvas->ClipX / S;
	FCSPlayerCombatRecord Record;
	if (!Pawn || !Director || !Director->GetRecord(Pawn->GetOwningPlayerId(), Record))
	{
		const FString Waiting = (!Pawn || !Director) ? TEXT("Waiting for match state...") : TEXT("Joining match...");
		DrawLabel(Waiting, W * 0.5f, 600.f, CSUI::Text, 24.f, false, 0.5f);
		DrawRoundOverlays();
		return;
	}

	if (!Record.bAlive)
	{
		// The scoreboard takes the centre of the screen; the death screen would show through it.
		if (!ShouldShowScoreboard())
		{
			DrawDeathOverlay(Record);
		}
	}
	else
	{
		DrawCrosshair();
		DrawHitMarker();
		DrawInteractionPrompt();
		DrawShopStatus(Record);
	}

	// v2.0 minimap, top left under the mode tag.
	if (Record.bAlive)
	{
		if (!Minimap)
		{
			Minimap = NewObject<UCSMinimap>(this);
			Minimap->Initialize(GetWorld());
		}
		Minimap->Draw(Canvas, 24.f * S, 46.f * S, 230.f * S, S, Pawn, Director);
	}

	DrawDamageIndicators();
	DrawMoney(Record);
	DrawVitals(Record);
	DrawAmmo();
	DrawQuickSlots();
	DrawNotice();
	DrawRoundOverlays();

	// v2.0 flashbang: the screen goes white and fades back.
	if (const ACSCharacter* Viewer = Cast<ACSCharacter>(GetOwningPawn()))
	{
		const float Flash = Viewer->GetFlashAmount();
		if (Flash > 0.f)
		{
			// Reduced (C11, photosensitivity): a mid grey that never fully covers the screen.
			const UCSSettingsSubsystem* FlashSettings = UCSSettingsSubsystem::Get(this);
			const bool bReduce = FlashSettings && FlashSettings->GetPreferences().bReduceFlash;
			const float Grey = bReduce ? 0.45f : 1.f;
			const float MaxAlpha = bReduce ? 0.85f : 1.f;
			DrawRect(FLinearColor(Grey, Grey, Grey, FMath::Clamp(Flash * 1.05f, 0.f, MaxAlpha)), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
		}
	}
}

// ---------------------------------------------------------------------------
// Round flow: banners and scoreboard
// ---------------------------------------------------------------------------

TArray<FCSPlayerCombatRecord> ACSHUD::SortedScores(const ACSMatchDirector* Director)
{
	TArray<FCSPlayerCombatRecord> Out;
	if (!Director)
	{
		return Out;
	}
	for (const FCSPlayerCombatRecord& R : Director->GetAllRecords())
	{
		if (R.PlayerId != 0)
		{
			Out.Add(R);
		}
	}
	Out.Sort([](const FCSPlayerCombatRecord& A, const FCSPlayerCombatRecord& B)
	{
		if (A.GetScore() != B.GetScore()) { return A.GetScore() > B.GetScore(); }
		if (A.Kills != B.Kills) { return A.Kills > B.Kills; }
		if (A.Deaths != B.Deaths) { return A.Deaths < B.Deaths; }
		return A.PlayerId < B.PlayerId;
	});
	return Out;
}

bool ACSHUD::ShouldShowScoreboard() const
{
	const ACSGameState* GS = GetCSGameState();
	const ACSPlayerController* PC = Cast<ACSPlayerController>(GetOwningPlayerController());
	const bool bPostMatch = GS && GS->GetMatchPhase() == ECSMatchPhase::PostMatch;
	const bool bMenuOpen = PC && (PC->IsPauseMenuOpen() || PC->IsShopOpen());
	return !bMenuOpen && (bPostMatch || (PC && PC->IsScoreboardHeld()));
}

void ACSHUD::DrawRoundOverlays()
{
	UpdatePhaseBanner();
	DrawPhaseBanner();

	bScoreboardDrawn = ShouldShowScoreboard();
	if (bScoreboardDrawn)
	{
		DrawScoreboard();
	}
}

void ACSHUD::UpdatePhaseBanner()
{
	const ACSGameState* GS = GetCSGameState();
	if (!GS)
	{
		return;
	}
	const FCSModeRules& Rules = GS->GetRules();
	const uint8 Phase = static_cast<uint8>(GS->GetMatchPhase());
	const bool bRoundDecided = GS->GetMatchPhase() == ECSMatchPhase::InProgress && Rules.bRounds
		&& (GS->GetWinnerTeam() != ECSTeam::None || GS->GetWinnerPlayerId() == -1);
	const int32 Round = GS->GetRoundNumber();

	const bool bPhaseChanged = Phase != LastSeenPhase;
	const bool bRoundChanged = Round != LastSeenRound;
	const bool bDecidedChanged = bRoundDecided != bLastRoundDecided;
	if (!bPhaseChanged && !bRoundChanged && !bDecidedChanged)
	{
		return;
	}
	// The first state seen after joining is the state of an ongoing match, not a change.
	const bool bFirstSight = LastSeenPhase == 0xFF;
	LastSeenPhase = Phase;
	LastSeenRound = Round;
	bLastRoundDecided = bRoundDecided;
	if (bFirstSight)
	{
		return;
	}

	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	const ECSTeam MyTeam = Director ? Director->GetTeam(GetLocalPlayerId()) : ECSTeam::None;
	BannerColor = CSUI::Accent;

	switch (GS->GetMatchPhase())
	{
	case ECSMatchPhase::Warmup:
		BannerTitle = TEXT("WARMUP");
		BannerSubtitle = TEXT("Kills during warmup do not count");
		break;

	case ECSMatchPhase::InProgress:
		if (bRoundDecided)
		{
			const ECSTeam Winner = GS->GetWinnerTeam();
			BannerTitle = Winner == ECSTeam::None ? TEXT("ROUND DRAW") : FString::Printf(TEXT("%s WINS THE ROUND"), *TeamName(Winner));
			BannerSubtitle = FString::Printf(TEXT("ALPHA  %d  :  %d  BRAVO"), GS->GetTeamScore(ECSTeam::Alpha), GS->GetTeamScore(ECSTeam::Bravo));
			BannerColor = Winner == ECSTeam::None ? CSUI::TextDim : CSUI::TeamColor(static_cast<uint8>(Winner));
		}
		else if (Rules.bRounds)
		{
			BannerTitle = FString::Printf(TEXT("ROUND %d"), FMath::Max(1, Round));
			BannerSubtitle = FString::Printf(TEXT("Buy time %.0f s  -  press B to open the shop"), Rules.BuySeconds);
			BannerColor = MyTeam != ECSTeam::None ? CSUI::TeamColor(static_cast<uint8>(MyTeam)) : CSUI::Accent;
		}
		else
		{
			BannerTitle = TEXT("MATCH STARTED");
			BannerSubtitle = Rules.bTeams
				? FString::Printf(TEXT("You are on team %s  -  first team to %d kills"), *TeamName(MyTeam), Rules.ScoreLimit)
				: FString::Printf(TEXT("First to %d kills wins"), Rules.ScoreLimit);
			BannerColor = MyTeam != ECSTeam::None ? CSUI::TeamColor(static_cast<uint8>(MyTeam)) : CSUI::Accent;
		}
		break;

	case ECSMatchPhase::PostMatch:
	{
		BannerTitle = TEXT("MATCH OVER");
		const ECSTeam WinnerTeam = GS->GetWinnerTeam();
		if (Rules.bTeams)
		{
			BannerSubtitle = WinnerTeam == ECSTeam::None
				? FString::Printf(TEXT("Draw  %d : %d"), GS->GetTeamScore(ECSTeam::Alpha), GS->GetTeamScore(ECSTeam::Bravo))
				: FString::Printf(TEXT("%s wins  %d : %d%s"), *TeamName(WinnerTeam), GS->GetTeamScore(ECSTeam::Alpha),
					GS->GetTeamScore(ECSTeam::Bravo), WinnerTeam == MyTeam ? TEXT("  -  VICTORY") : TEXT("  -  DEFEAT"));
			BannerColor = WinnerTeam == ECSTeam::None ? CSUI::TextDim : CSUI::TeamColor(static_cast<uint8>(WinnerTeam));
		}
		else
		{
			const TArray<FCSPlayerCombatRecord> Scores = SortedScores(Director);
			const int32 Winner = GS->GetWinnerPlayerId();
			const int32 Kills = Scores.Num() > 0 ? Scores[0].Kills : 0;
			if (Winner == 0 || Kills == 0)
			{
				BannerSubtitle = Kills == 0 ? TEXT("No kills this match") : FString::Printf(TEXT("Draw at %d kills"), Kills);
			}
			else
			{
				const bool bMe = Winner == GetLocalPlayerId();
				BannerSubtitle = FString::Printf(TEXT("%s %s with %d kills"), *PlayerLabel(Winner), bMe ? TEXT("win") : TEXT("wins"), Kills);
				BannerColor = bMe ? CSUI::Money : CSUI::Accent;
			}
		}
		break;
	}
	default:
		return;
	}
	BannerTime = GetWorld()->GetRealTimeSeconds();
	UE_LOG(LogCS, Log, TEXT("HUD banner: %s - %s"), *BannerTitle, *BannerSubtitle);
}

void ACSHUD::DrawPhaseBanner()
{
	constexpr double Hold = 2.8;
	constexpr double Fade = 0.6;
	const double Age = GetWorld()->GetRealTimeSeconds() - BannerTime;
	if (Age < 0.0 || Age > Hold + Fade)
	{
		return;
	}
	const float Alpha = Age <= Hold ? 1.f : static_cast<float>(1.0 - (Age - Hold) / Fade);
	// Slides open from the centre in the first 0.2 s.
	const float Open = FMath::Clamp(static_cast<float>(Age / 0.2), 0.f, 1.f);
	const float W = Canvas->ClipX / S;
	const float BandW = FMath::Lerp(200.f, 900.f, Open);
	const float X = W * 0.5f - BandW * 0.5f;
	const float Y = 230.f;

	DrawBox(WithAlpha(GPanelStrong, Alpha), X, Y, BandW, 116.f);
	DrawBox(WithAlpha(BannerColor, 0.95f * Alpha), X, Y, BandW, 4.f);
	DrawBox(WithAlpha(BannerColor, 0.35f * Alpha), X, Y + 112.f, BandW, 4.f);
	if (Open >= 1.f)
	{
		DrawLabel(BannerTitle, W * 0.5f, Y + 14.f, WithAlpha(CSUI::Text, Alpha), 48.f, true, 0.5f);
		DrawLabel(BannerSubtitle, W * 0.5f, Y + 74.f, WithAlpha(CSUI::TextDim, Alpha), 20.f, false, 0.5f);
	}
}

float ACSHUD::DrawScoreTable(const TArray<FCSPlayerCombatRecord>& Rows, float X, float Y, float Width,
	const FString& Title, const FLinearColor& Color, bool bShowMoney, int32 TeamScore)
{
	constexpr float RowH = 36.f;
	const int32 Me = GetLocalPlayerId();

	// Header strip in the team colour.
	DrawBox(WithAlpha(Color, 0.22f), X, Y, Width, 44.f);
	DrawBox(Color, X, Y, 5.f, 44.f);
	DrawLabel(Title, X + 20.f, Y + 9.f, CSUI::Text, 22.f, true);
	if (TeamScore >= 0)
	{
		DrawLabel(FString::FromInt(TeamScore), X + Width - 18.f, Y + 4.f, Color, 32.f, true, 1.f);
	}

	// Phase 6 (D6): assists, score and ping next to kills and deaths.
	const float ColMoney = X + Width - 500.f;
	const float ColK = X + Width - 390.f;
	const float ColA = X + Width - 335.f;
	const float ColD = X + Width - 280.f;
	const float ColScore = X + Width - 210.f;
	const float ColPing = X + Width - 130.f;
	const float ColState = X + Width - 50.f;
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	const int32 HostId = Director ? UCSAuthority::GetOwningPlayerId(Director) : 0;
	float RowY = Y + 52.f;
	DrawLabel(TEXT("PLAYER"), X + 20.f, RowY, CSUI::TextDim, 13.f, true);
	if (bShowMoney)
	{
		DrawLabel(TEXT("MONEY"), ColMoney, RowY, CSUI::TextDim, 13.f, true, 0.5f);
	}
	DrawLabel(TEXT("K"), ColK, RowY, CSUI::TextDim, 13.f, true, 0.5f);
	DrawLabel(TEXT("A"), ColA, RowY, CSUI::TextDim, 13.f, true, 0.5f);
	DrawLabel(TEXT("D"), ColD, RowY, CSUI::TextDim, 13.f, true, 0.5f);
	DrawLabel(TEXT("SCORE"), ColScore, RowY, CSUI::TextDim, 13.f, true, 0.5f);
	DrawLabel(TEXT("PING"), ColPing, RowY, CSUI::TextDim, 13.f, true, 0.5f);
	RowY += 24.f;

	if (Rows.Num() == 0)
	{
		DrawLabel(TEXT("Nobody here"), X + 20.f, RowY + 6.f, CSUI::TextDim, 17.f);
		RowY += RowH;
	}
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		const FCSPlayerCombatRecord& R = Rows[i];
		const bool bMine = R.PlayerId == Me;
		DrawBox(bMine ? WithAlpha(CSUI::Accent, 0.16f) : FLinearColor(1.f, 1.f, 1.f, i % 2 == 0 ? 0.035f : 0.f), X, RowY, Width, RowH - 2.f);
		if (bMine)
		{
			DrawBox(CSUI::Accent, X, RowY, 3.f, RowH - 2.f);
		}
		const FLinearColor TextColor = R.bAlive ? CSUI::Text : CSUI::TextDim;
		DrawLabel(FString::Printf(TEXT("%d"), i + 1), X + 20.f, RowY + 8.f, CSUI::TextDim, 16.f);
		const FString Name = PlayerLabel(R.PlayerId);
		DrawLabel(Name, X + 52.f, RowY + 6.f, bMine ? CSUI::Accent : TextColor, 19.f, bMine);
		// Tags after the name: a bot, or the player whose game runs the match.
		const bool bBot = CSBots::IsBotId(R.PlayerId);
		const FString Tag = bBot ? TEXT("BOT") : (R.PlayerId == HostId ? TEXT("HOST") : TEXT(""));
		if (!Tag.IsEmpty())
		{
			const float TagX = X + 52.f + FMath::Min(TextWidth(Name, 19.f, bMine), 260.f) + 10.f;
			DrawLabel(Tag, TagX, RowY + 10.f, bBot ? CSUI::TextDim : CSUI::Warning, 12.f, true);
		}
		if (bShowMoney)
		{
			DrawLabel(CSUI::MoneyText(R.Money).ToString(), ColMoney, RowY + 7.f, WithAlpha(CSUI::Money, R.bAlive ? 1.f : 0.6f), 17.f, false, 0.5f);
		}
		DrawLabel(FString::FromInt(R.Kills), ColK, RowY + 6.f, TextColor, 19.f, true, 0.5f);
		DrawLabel(FString::FromInt(R.Assists), ColA, RowY + 6.f, TextColor, 19.f, false, 0.5f);
		DrawLabel(FString::FromInt(R.Deaths), ColD, RowY + 6.f, TextColor, 19.f, false, 0.5f);
		DrawLabel(FString::FromInt(R.GetScore()), ColScore, RowY + 6.f, TextColor, 19.f, true, 0.5f);
		// Ping as the player's own game reports it; bots have none.
		FString Ping = TEXT("-");
		FLinearColor PingColor = CSUI::TextDim;
		if (!bBot)
		{
			if (const ACSCharacter* Pawn = ACSMatchDirector::FindPawnForPlayer(this, R.PlayerId); Pawn && Pawn->GetPingMs() > 0)
			{
				Ping = FString::FromInt(Pawn->GetPingMs());
				PingColor = Pawn->GetPingMs() <= 60 ? CSUI::Money : (Pawn->GetPingMs() <= 120 ? CSUI::Warning : CSUI::Danger);
			}
		}
		DrawLabel(Ping, ColPing, RowY + 7.f, PingColor, 17.f, false, 0.5f);
		if (!R.bAlive)
		{
			DrawLabel(TEXT("DEAD"), ColState, RowY + 9.f, CSUI::Danger, 13.f, true, 0.5f);
		}
		RowY += RowH;
	}
	return RowY - Y;
}

void ACSHUD::DrawScoreboard()
{
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	const ACSGameState* GS = GetCSGameState();
	const TArray<FCSPlayerCombatRecord> Scores = SortedScores(Director);
	ScoreboardRows = Scores.Num();
	const FCSModeRules& Rules = GS ? GS->GetRules() : UCSModeSettings::Rules(ECSGameModeType::Deathmatch);
	const ECSGameModeType Mode = GS ? GS->GetGameMode() : ECSGameModeType::Deathmatch;

	const float W = Canvas->ClipX / S;
	const float H = Canvas->ClipY / S;
	constexpr float PanelW = 920.f;
	const float X = W * 0.5f - PanelW * 0.5f;
	float Y = 150.f;

	// Full-screen dim, then the panel.
	DrawBox(FLinearColor(0.f, 0.f, 0.f, 0.35f), 0.f, 0.f, W, H);
	const int32 TableCount = Rules.bTeams ? 2 : 1;
	const float EstimatedH = 110.f + TableCount * 90.f + FMath::Max(1, Scores.Num()) * 36.f + (TableCount - 1) * 26.f + 40.f;
	DrawBox(GPanelStrong, X - 24.f, Y - 24.f, PanelW + 48.f, EstimatedH);
	DrawBox(CSUI::Accent, X - 24.f, Y - 24.f, PanelW + 48.f, 4.f);

	// Title line: mode, map, and the time / result.
	FString Title = UCSModeSettings::ModeName(Mode).ToString().ToUpper();
	if (const FCSMapInfo* Map = UCSModeSettings::Get()->FindMapByWorld(GetWorld()->GetOutermost()->GetName()))
	{
		Title += TEXT("   /   ") + Map->DisplayName.ToUpper();
	}
	DrawLabel(Title, X, Y, CSUI::Text, 28.f, true);
	FString Right;
	if (GS && GS->GetMatchPhase() == ECSMatchPhase::PostMatch)
	{
		Right = FString::Printf(TEXT("NEXT MATCH IN %d"), FMath::Max(0, FMath::CeilToInt(GS->GetPhaseTimeRemaining())));
	}
	else if (GS)
	{
		Right = Rules.bRounds
			? FString::Printf(TEXT("ROUND %d  -  FIRST TO %d"), FMath::Max(1, GS->GetRoundNumber()), Rules.ScoreLimit)
			: FString::Printf(TEXT("FIRST TO %d  -  %s LEFT"), Rules.ScoreLimit, *Clock(GS->GetPhaseTimeRemaining()));
	}
	DrawLabel(Right, X + PanelW, Y + 8.f, CSUI::TextDim, 17.f, true, 1.f);
	Y += 56.f;

	if (!Rules.bTeams)
	{
		DrawScoreTable(Scores, X, Y, PanelW, TEXT("PLAYERS"), CSUI::Accent, /*bShowMoney*/ true, -1);
		return;
	}

	// Money is shown for your own team only, as in CS.
	const ECSTeam MyTeam = Director ? Director->GetTeam(GetLocalPlayerId()) : ECSTeam::None;
	for (const ECSTeam Team : { ECSTeam::Alpha, ECSTeam::Bravo })
	{
		TArray<FCSPlayerCombatRecord> Rows = Scores.FilterByPredicate([Team](const FCSPlayerCombatRecord& R) { return R.GetTeam() == Team; });
		const FString Name = Team == ECSTeam::Alpha ? TEXT("TEAM ALPHA") : TEXT("TEAM BRAVO");
		Y += DrawScoreTable(Rows, X, Y, PanelW, Name, CSUI::TeamColor(static_cast<uint8>(Team)), Team == MyTeam,
			GS ? GS->GetTeamScore(Team) : 0) + 26.f;
	}
}

// ---------------------------------------------------------------------------
// Elements
// ---------------------------------------------------------------------------

void ACSHUD::DrawFpsCounter()
{
	const UCSSettingsSubsystem* Settings = UCSSettingsSubsystem::Get(this);
	if (!Settings || !Settings->GetPreferences().bShowFps)
	{
		return;
	}
	// Exponential smoothing so the number is readable, not a flicker.
	const double FrameMs = FApp::GetDeltaTime() * 1000.0;
	SmoothedFrameMs = SmoothedFrameMs <= 0.0 ? FrameMs : FMath::Lerp(SmoothedFrameMs, FrameMs, 0.05);
	const double Fps = SmoothedFrameMs > 0.0 ? 1000.0 / SmoothedFrameMs : 0.0;
	const FLinearColor Color = Fps >= 60.0 ? CSUI::Money : (Fps >= 30.0 ? CSUI::Warning : CSUI::Danger);
	DrawLabel(FString::Printf(TEXT("%d FPS  %.1f ms"), FMath::RoundToInt(Fps), SmoothedFrameMs), 24.f, 52.f, Color, 14.f, true);
}

void ACSHUD::DrawModeTag()
{
	const ACSGameState* GS = GetCSGameState();
	if (!GS)
	{
		return;
	}
	FString Tag = UCSModeSettings::ModeName(GS->GetGameMode()).ToString().ToUpper();
	if (const FCSMapInfo* Map = UCSModeSettings::Get()->FindMapByWorld(GetWorld()->GetOutermost()->GetName()))
	{
		Tag += TEXT("  /  ") + Map->DisplayName.ToUpper();
	}
	DrawBox(CSUI::Accent, 24.f, 22.f, 3.f, 20.f);
	DrawLabel(Tag, 34.f, 22.f, CSUI::TextDim, 15.f, true);
}

void ACSHUD::DrawScoreBar()
{
	const ACSGameState* GS = GetCSGameState();
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	if (!GS)
	{
		return;
	}
	const FCSModeRules& Rules = GS->GetRules();
	const float W = Canvas->ClipX / S;
	const float CX = W * 0.5f;
	const float Y = 16.f;

	// Timer block. In a round's buy time it counts the buy time down instead.
	float TimeLeft = GS->GetPhaseTimeRemaining();
	FString Sub;
	FLinearColor TimerColor = CSUI::Text;
	switch (GS->GetMatchPhase())
	{
	case ECSMatchPhase::WaitingForPlayers:	Sub = TEXT("WAITING"); TimeLeft = 0.f; break;
	case ECSMatchPhase::Warmup:				Sub = TEXT("WARMUP"); break;
	case ECSMatchPhase::PostMatch:			Sub = TEXT("MATCH OVER"); break;
	case ECSMatchPhase::InProgress:
		if (Rules.bRounds)
		{
			const bool bDecided = GS->GetWinnerTeam() != ECSTeam::None || GS->GetWinnerPlayerId() == -1;
			Sub = FString::Printf(TEXT("ROUND %d"), FMath::Max(1, GS->GetRoundNumber()));
			if (!bDecided && GS->GetBuyTimeRemaining() > 0.f)
			{
				Sub = FString::Printf(TEXT("BUY  %d"), FMath::CeilToInt(GS->GetBuyTimeRemaining()));
				TimerColor = CSUI::Money;
			}
		}
		else
		{
			Sub = FString::Printf(TEXT("FIRST TO %d"), Rules.ScoreLimit);
		}
		break;
	}
	if (GS->GetMatchPhase() == ECSMatchPhase::InProgress && TimeLeft <= 10.f && TimeLeft > 0.f)
	{
		TimerColor = CSUI::Danger;
	}

	constexpr float TimerW = 150.f;
	constexpr float BlockH = 64.f;
	DrawBox(GPanelStrong, CX - TimerW * 0.5f, Y, TimerW, BlockH);
	DrawLabel(Clock(TimeLeft), CX, Y + 2.f, TimerColor, 36.f, true, 0.5f);
	DrawLabel(Sub, CX, Y + 43.f, CSUI::TextDim, 13.f, true, 0.5f);

	// Side blocks: team scores, or your kills vs the leader in free for all.
	constexpr float SideW = 118.f;
	auto Side = [&](float X, const FString& Label, int32 Value, const FLinearColor& Color, bool bLeft)
	{
		DrawBox(WithAlpha(Color, 0.20f), X, Y, SideW, BlockH);
		DrawBox(Color, bLeft ? X : X + SideW - 4.f, Y, 4.f, BlockH);
		DrawLabel(FString::FromInt(Value), X + SideW * 0.5f, Y + 2.f, CSUI::Text, 38.f, true, 0.5f);
		DrawLabel(Label, X + SideW * 0.5f, Y + 45.f, Color, 12.f, true, 0.5f);
	};

	if (Rules.bTeams)
	{
		const float LeftX = CX - TimerW * 0.5f - 4.f - SideW;
		const float RightX = CX + TimerW * 0.5f + 4.f;
		Side(LeftX, TEXT("ALPHA"), GS->GetTeamScore(ECSTeam::Alpha), CSUI::TeamAlpha, true);
		Side(RightX, TEXT("BRAVO"), GS->GetTeamScore(ECSTeam::Bravo), CSUI::TeamBravo, false);

		// Rounds: one pip per team member, bright while alive.
		if (Rules.bRounds && Director)
		{
			for (const ECSTeam Team : { ECSTeam::Alpha, ECSTeam::Bravo })
			{
				const int32 Members = Director->CountMembers(Team);
				const int32 Alive = Director->CountAlive(Team);
				const FLinearColor Color = CSUI::TeamColor(static_cast<uint8>(Team));
				const float PipW = 14.f;
				const float Gap = 5.f;
				const float Total = Members * PipW + FMath::Max(0, Members - 1) * Gap;
				const float StartX = (Team == ECSTeam::Alpha ? LeftX + SideW : RightX) + (Team == ECSTeam::Alpha ? -Total : 0.f);
				for (int32 i = 0; i < Members; ++i)
				{
					DrawBox(i < Alive ? Color : FLinearColor(1.f, 1.f, 1.f, 0.15f), StartX + i * (PipW + Gap), Y + BlockH + 6.f, PipW, 6.f);
				}
			}
		}
	}
	else if (Director)
	{
		const int32 Me = GetLocalPlayerId();
		const TArray<FCSPlayerCombatRecord> Scores = SortedScores(Director);
		int32 MyKills = 0;
		for (const FCSPlayerCombatRecord& R : Scores)
		{
			MyKills = R.PlayerId == Me ? R.Kills : MyKills;
		}
		// The best other player: the leader, or the runner-up if you lead.
		int32 BestOther = 0;
		for (const FCSPlayerCombatRecord& R : Scores)
		{
			if (R.PlayerId != Me)
			{
				BestOther = R.Kills;
				break;
			}
		}
		const bool bLeading = MyKills > BestOther;
		Side(CX - TimerW * 0.5f - 4.f - SideW, TEXT("YOU"), MyKills, CSUI::Accent, true);
		Side(CX + TimerW * 0.5f + 4.f, bLeading ? TEXT("2ND") : TEXT("LEADER"), BestOther, CSUI::TextDim, false);
	}
}

void ACSHUD::DrawScope()
{
	// Black mask with a round window, drawn as horizontal strips, plus a thin
	// reticle. The weapon model is hidden while this is up (ACSCharacter).
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const float CX = W * 0.5f;
	const float CY = H * 0.5f;
	const float R = H * 0.47f;
	const FLinearColor Black(0.f, 0.f, 0.f, 1.f);
	const int32 Strip = FMath::Max(1, FMath::RoundToInt(H / 360.f));
	for (int32 Y = 0; Y < H; Y += Strip)
	{
		const float Dy = (Y + Strip * 0.5f) - CY;
		if (FMath::Abs(Dy) >= R)
		{
			DrawRect(Black, 0.f, Y, W, Strip);
			continue;
		}
		const float Half = FMath::Sqrt(R * R - Dy * Dy);
		DrawRect(Black, 0.f, Y, CX - Half, Strip);
		DrawRect(Black, CX + Half, Y, W - (CX + Half), Strip);
	}
	const FLinearColor Reticle(0.02f, 0.02f, 0.02f, 0.9f);
	const float T = FMath::Max(1.f, 1.5f * S);
	DrawLine(CX - R, CY, CX - 6.f * S, CY, Reticle, T);
	DrawLine(CX + 6.f * S, CY, CX + R, CY, Reticle, T);
	DrawLine(CX, CY + 6.f * S, CX, CY + R, Reticle, T);
	DrawLine(CX, CY - R * 0.6f, CX, CY - 6.f * S, Reticle, T * 0.6f);
	DrawRect(FLinearColor(0.9f, 0.1f, 0.1f, 0.9f), CX - T, CY - T, T * 2.f, T * 2.f);
}

void ACSHUD::DrawCrosshair()
{
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;

	// The player's crosshair (C11): style, colour, size, gap, thickness,
	// outline, and whether it opens with the weapon's spread.
	FCSPlayerPreferences Prefs;
	if (const UCSSettingsSubsystem* Settings = UCSSettingsSubsystem::Get(this))
	{
		Prefs = Settings->GetPreferences();
	}
	float Gap = Prefs.CrosshairGap;
	float Alpha = 1.f;
	if (const ACSCharacter* Pawn = Cast<ACSCharacter>(GetOwningPawn()))
	{
		if (Pawn->IsScopedView())
		{
			DrawScope();
			return;
		}
		// Aiming down sights: the weapon's own sights are the crosshair.
		Alpha = FMath::Clamp(1.f - Pawn->GetAimAlpha() * 2.5f, 0.f, 1.f);
		if (Alpha <= 0.f)
		{
			return;
		}
		if (const UCSWeaponComponent* Weapon = Pawn->GetWeaponComponent(); Weapon && Prefs.bCrosshairDynamic)
		{
			Gap += Weapon->GetCurrentSpreadDegrees() * 6.f;
		}
	}
	const FLinearColor Base = CSUI::CrosshairColors[FMath::Clamp(Prefs.CrosshairColor, 0, 5)];
	const FLinearColor Col(Base.R, Base.G, Base.B, 0.95f * Alpha);
	const FLinearColor Outline(0.f, 0.f, 0.f, (Prefs.bCrosshairOutline ? 0.6f : 0.f) * Alpha);

	const float G = Gap * S;
	const float L = Prefs.CrosshairSize * S;
	const float T = FMath::Max(1.f, Prefs.CrosshairThickness * S);
	const float O = FMath::Max(1.f, 1.f * S);
	auto Bar = [&](float X, float Y, float W, float H)
	{
		DrawRect(Outline, X - O, Y - O, W + 2.f * O, H + 2.f * O);
		DrawRect(Col, X, Y, W, H);
	};
	const int32 Style = FMath::Clamp(Prefs.CrosshairStyle, 0, 3);
	if (Style == 3)
	{
		// Circle: short segments around the gap, plus a centre dot.
		const float R = FMath::Max(G + L * 0.5f, 3.f * S);
		constexpr int32 Segments = 24;
		for (int32 i = 0; i < Segments; ++i)
		{
			const float A = i * UE_TWO_PI / Segments;
			Bar(CX + FMath::Cos(A) * R - T * 0.5f, CY + FMath::Sin(A) * R - T * 0.5f, T, T);
		}
		Bar(CX - T * 0.5f, CY - T * 0.5f, T, T);
		return;
	}
	if (Style == 0 || Style == 1)
	{
		Bar(CX - G - L, CY - T * 0.5f, L, T);
		Bar(CX + G, CY - T * 0.5f, L, T);
		Bar(CX - T * 0.5f, CY - G - L, T, L);
		Bar(CX - T * 0.5f, CY + G, T, L);
	}
	if (Style == 1 || Style == 2)
	{
		Bar(CX - T * 0.5f, CY - T * 0.5f, T, T);
	}
}

void ACSHUD::DrawNetStats()
{
	const UCSSettingsSubsystem* Settings = UCSSettingsSubsystem::Get(this);
	if (!Settings || !Settings->GetPreferences().bShowNetStats)
	{
		return;
	}
	// Ping once a second from this game's own measurement; jitter = the mean
	// change between those samples.
	const double Now = FPlatformTime::Seconds();
	if (Now - LastPingSample >= 1.0)
	{
		LastPingSample = Now;
		PingSamples.Add(UCSAuthority::GetRttMs(this));
		if (PingSamples.Num() > 10)
		{
			PingSamples.RemoveAt(0);
		}
	}
	float Jitter = 0.f;
	for (int32 i = 1; i < PingSamples.Num(); ++i)
	{
		Jitter += FMath::Abs(PingSamples[i] - PingSamples[i - 1]);
	}
	Jitter = PingSamples.Num() > 1 ? Jitter / (PingSamples.Num() - 1) : 0.f;
	const int32 Ping = PingSamples.Num() ? PingSamples.Last() : 0;
	const bool bOnline = UCSAuthority::IsSessionActive(this);
	const FString NetRole = !bOnline ? TEXT("OFFLINE") : (UCSAuthority::IsGameAuthority(this) ? TEXT("HOST") : TEXT("CLIENT"));
	const FLinearColor Color = !bOnline ? CSUI::TextDim : (Ping <= 60 ? CSUI::Money : (Ping <= 120 ? CSUI::Warning : CSUI::Danger));
	DrawLabel(FString::Printf(TEXT("%s  PING %d ms  JITTER %.0f ms"), *NetRole, Ping, Jitter), 24.f, 72.f, Color, 14.f, true);
}

void ACSHUD::DrawHitMarker()
{
	const double Age = GetSecondsSinceHitMarker();
	const double Life = bHitMarkerKill ? GKillMarkerSeconds : GHitMarkerSeconds;
	if (Age > Life)
	{
		return;
	}

	const float Alpha = 1.f - static_cast<float>(Age / Life);
	const FLinearColor Color = bHitMarkerKill ? WithAlpha(CSUI::Danger, Alpha)
		: (bHitMarkerHead ? FLinearColor(1.f, 0.85f, 0.3f, Alpha) : FLinearColor(1.f, 1.f, 1.f, Alpha));

	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;
	const float Inner = (bHitMarkerKill ? 10.f : 8.f) * S;
	const float Outer = (bHitMarkerKill ? 22.f : 17.f) * S;
	const float T = FMath::Max(1.f, 2.5f * S);
	for (int32 i = 0; i < 4; ++i)
	{
		const float Angle = FMath::DegreesToRadians(45.f + 90.f * i);
		const float DX = FMath::Cos(Angle);
		const float DY = FMath::Sin(Angle);
		DrawLine(CX + DX * Inner, CY + DY * Inner, CX + DX * Outer, CY + DY * Outer, Color, T);
	}
}

void ACSHUD::DrawDamageIndicators()
{
	const APawn* Pawn = GetOwningPawn();
	if (!Pawn || !PlayerOwner || !PlayerOwner->PlayerCameraManager || DamageIndicators.Num() == 0)
	{
		return;
	}

	const double Now = GetWorld()->GetRealTimeSeconds();
	const float CamYaw = PlayerOwner->PlayerCameraManager->GetCameraRotation().Yaw;
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;
	const float Radius = 150.f * S;
	const float T = FMath::Max(2.f, 6.f * S);

	float Flash = 0.f;
	for (const FDamageIndicator& D : DamageIndicators)
	{
		const float Age = static_cast<float>(Now - D.Time);
		const float Alpha = FMath::Clamp(1.f - Age / static_cast<float>(GDamageIndicatorSeconds), 0.f, 1.f);
		Flash = FMath::Max(Flash, FMath::Clamp(0.35f - Age, 0.f, 0.35f));

		FVector ToAttacker = D.From - Pawn->GetActorLocation();
		ToAttacker.Z = 0.f;
		if (ToAttacker.IsNearlyZero())
		{
			continue;
		}

		// 0 = straight ahead (top of the screen), clockwise positive.
		const float Relative = FMath::DegreesToRadians(FRotator::NormalizeAxis(ToAttacker.Rotation().Yaw - CamYaw));
		const float HalfArc = FMath::DegreesToRadians(20.f);
		const FLinearColor Color = WithAlpha(CSUI::Danger, 0.85f * Alpha);

		constexpr int32 Segments = 8;
		for (int32 i = 0; i < Segments; ++i)
		{
			const float A0 = Relative - HalfArc + (2.f * HalfArc) * i / Segments;
			const float A1 = Relative - HalfArc + (2.f * HalfArc) * (i + 1) / Segments;
			DrawLine(CX + FMath::Sin(A0) * Radius, CY - FMath::Cos(A0) * Radius,
				CX + FMath::Sin(A1) * Radius, CY - FMath::Cos(A1) * Radius, Color, T);
		}
	}

	if (Flash > 0.f)
	{
		// Brief red edge flash on any hit, so damage from behind is noticed too.
		const float E = 36.f * S;
		const FLinearColor Edge(0.8f, 0.f, 0.f, Flash);
		DrawRect(Edge, 0.f, 0.f, Canvas->ClipX, E);
		DrawRect(Edge, 0.f, Canvas->ClipY - E, Canvas->ClipX, E);
		DrawRect(Edge, 0.f, E, E, Canvas->ClipY - 2.f * E);
		DrawRect(Edge, Canvas->ClipX - E, E, E, Canvas->ClipY - 2.f * E);
	}
}

void ACSHUD::DrawMoney(const FCSPlayerCombatRecord& Record)
{
	const double Now = GetWorld()->GetRealTimeSeconds();
	if (LastMoney != INT32_MIN && Record.Money != LastMoney)
	{
		MoneyDelta = Record.Money - LastMoney;
		MoneyDeltaTime = Now;
	}
	LastMoney = Record.Money;

	const float H = Canvas->ClipY / S;
	const float X = 32.f;
	const float Y = H - 176.f;
	DrawLabel(CSUI::MoneyText(Record.Money).ToString(), X, Y, CSUI::Money, 30.f, true);

	// "+$300" floats up and fades next to the balance.
	const double Age = Now - MoneyDeltaTime;
	if (Age < 1.8 && MoneyDelta != 0)
	{
		const float Alpha = FMath::Clamp(static_cast<float>(1.8 - Age) / 0.6f, 0.f, 1.f);
		const FString Delta = (MoneyDelta > 0 ? TEXT("+") : TEXT("-")) + CSUI::MoneyText(FMath::Abs(MoneyDelta)).ToString();
		const float Rise = static_cast<float>(Age) * 18.f;
		DrawLabel(Delta, X + TextWidth(CSUI::MoneyText(Record.Money).ToString(), 30.f, true) + 14.f, Y + 6.f - Rise,
			WithAlpha(MoneyDelta > 0 ? CSUI::Money : CSUI::Danger, Alpha), 20.f, true);
	}
}

void ACSHUD::DrawVitals(const FCSPlayerCombatRecord& Record)
{
	const UCSCombatSettings* Settings = UCSCombatSettings::Get();
	const float H = Canvas->ClipY / S;
	const float X = 32.f;
	const float Y = H - 128.f;
	constexpr float PanelW = 330.f;
	constexpr float PanelH = 96.f;

	DrawBox(GPanel, X, Y, PanelW, PanelH);
	DrawBox(CSUI::Stroke, X, Y, PanelW, 1.5f);

	const float HealthPct = FMath::Clamp(Record.Health / FMath::Max(1.f, Settings->MaxHealth), 0.f, 1.f);
	const float ArmorPct = FMath::Clamp(Record.Armor / FMath::Max(1.f, Settings->MaxArmor), 0.f, 1.f);
	const FLinearColor HealthColor = HealthPct > 0.5f ? GHealth : (HealthPct > 0.25f ? CSUI::Warning : CSUI::Danger);

	// Health: a cross, the number, and a segmented bar.
	const float IX = X + 18.f;
	const float IY = Y + 16.f;
	DrawBox(HealthColor, IX + 8.f, IY, 8.f, 24.f);
	DrawBox(HealthColor, IX, IY + 8.f, 24.f, 8.f);
	DrawLabel(FString::FromInt(FMath::CeilToInt(Record.Health)), IX + 38.f, Y + 4.f, HealthColor, 40.f, true);

	constexpr int32 Segments = 10;
	const float BarX = X + 140.f;
	const float BarW = PanelW - 158.f;
	const float SegW = (BarW - (Segments - 1) * 3.f) / Segments;
	for (int32 i = 0; i < Segments; ++i)
	{
		const float Fill = FMath::Clamp(HealthPct * Segments - i, 0.f, 1.f);
		const float SX = BarX + i * (SegW + 3.f);
		DrawBox(GTrack, SX, Y + 24.f, SegW, 10.f);
		DrawBox(HealthColor, SX, Y + 24.f, SegW * Fill, 10.f);
	}

	// Armor: a small shield, the number and a thin bar.
	const float AY = Y + 60.f;
	DrawBox(GArmor, IX + 3.f, AY, 18.f, 14.f);
	DrawBox(GArmor, IX + 7.f, AY + 14.f, 10.f, 5.f);
	DrawLabel(FString::FromInt(FMath::CeilToInt(Record.Armor)), IX + 38.f, AY - 7.f, ArmorPct > 0.f ? GArmor : CSUI::TextDim, 26.f, true);
	DrawBox(GTrack, BarX, AY + 6.f, BarW, 6.f);
	DrawBox(GArmor, BarX, AY + 6.f, BarW * ArmorPct, 6.f);
}

void ACSHUD::DrawAmmo()
{
	const ACSCharacter* Pawn = Cast<ACSCharacter>(GetOwningPawn());
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	if (!Pawn || !Director)
	{
		return;
	}

	// From the authoritative loadout: whatever is in the equipped slot.
	const FCSLoadoutView Loadout = Director->GetLoadout(Pawn->GetOwningPlayerId());
	const UCSWeaponDefinition* Weapon = Loadout.Weapon;

	const float W = Canvas->ClipX / S;
	const float H = Canvas->ClipY / S;
	constexpr float PanelW = 330.f;
	constexpr float PanelH = 96.f;
	const float X = W - 32.f - PanelW;
	const float Y = H - 128.f;
	const float Right = X + PanelW - 18.f;

	DrawBox(GPanel, X, Y, PanelW, PanelH);
	DrawBox(CSUI::Stroke, X, Y, PanelW, 1.5f);

	const FString WeaponName = Weapon ? Weapon->DisplayName.ToString().ToUpper() : TEXT("NO WEAPON");
	DrawLabel(WeaponName, X + 18.f, Y + 12.f, CSUI::TextDim, 15.f, true);

	if (Loadout.bGrenade)
	{
		DrawLabel(FString::Printf(TEXT("x%d"), Loadout.RoundsInMag), Right, Y + 26.f, CSUI::Text, 50.f, true, 1.f);
		DrawLabel(TEXT("CLICK TO THROW"), X + 18.f, Y + 64.f, CSUI::Accent, 13.f, true);
		return;
	}
	if (Loadout.bKnife)
	{
		DrawLabel(TEXT("LMB SLASH  -  RMB STAB"), X + 18.f, Y + 64.f, CSUI::Accent, 13.f, true);
		return;
	}

	if (Loadout.bReloading)
	{
		DrawLabel(TEXT("RELOADING"), Right, Y + 38.f, CSUI::Warning, 30.f, true, 1.f);
		return;
	}
	if (!Weapon)
	{
		return;
	}

	const FString Reserve = FString::FromInt(Loadout.Reserve);
	const FLinearColor MagColor = Loadout.RoundsInMag == 0 ? CSUI::Danger
		: (Loadout.RoundsInMag <= FMath::Max(1, Weapon->MagazineSize / 4) ? CSUI::Warning : CSUI::Text);

	const float ReserveW = TextWidth(Reserve, 22.f, false);
	DrawLabel(Reserve, Right, Y + 50.f, CSUI::TextDim, 22.f, false, 1.f);
	DrawLabel(TEXT("/"), Right - ReserveW - 8.f, Y + 50.f, CSUI::TextDim, 22.f, false, 1.f);
	DrawLabel(FString::FromInt(Loadout.RoundsInMag), Right - ReserveW - 24.f, Y + 26.f, MagColor, 50.f, true, 1.f);

	// One tick per round in the magazine (compressed for big magazines).
	const int32 Mag = FMath::Max(1, Weapon->MagazineSize);
	const int32 Ticks = FMath::Min(Mag, 30);
	const float TickW = 4.f;
	const float TickGap = 2.f;
	const float TicksX = X + 18.f;
	for (int32 i = 0; i < Ticks; ++i)
	{
		const bool bFull = i < FMath::CeilToInt(static_cast<float>(Loadout.RoundsInMag) * Ticks / Mag);
		DrawBox(bFull ? WithAlpha(MagColor, 0.9f) : GTrack, TicksX + i * (TickW + TickGap), Y + 70.f, TickW, 12.f);
	}

	if (Loadout.RoundsInMag == 0)
	{
		DrawLabel(TEXT("PRESS R TO RELOAD"), W * 0.5f, H * 0.5f + 90.f, CSUI::Danger, 18.f, true, 0.5f);
	}
}

void ACSHUD::DrawKillFeed()
{
	if (KillFeed.Num() == 0)
	{
		return;
	}

	const float W = Canvas->ClipX / S;
	const int32 Me = GetLocalPlayerId();
	const double Now = GetWorld()->GetRealTimeSeconds();
	float Y = 22.f;

	for (int32 i = KillFeed.Num() - 1; i >= 0; --i)
	{
		const FKillFeedEntry& E = KillFeed[i];
		const float Alpha = FMath::Clamp(static_cast<float>(KillFeedSeconds - (Now - E.Time)), 0.f, 1.f);

		const FString Killer = E.KillerId == E.VictimId ? FString() : PlayerLabel(E.KillerId);
		const FString Weapon = E.Weapon.IsEmpty() ? TEXT("?") : E.Weapon.ToUpper();
		const FString Victim = PlayerLabel(E.VictimId);

		const float WK = TextWidth(Killer, 17.f, true);
		const float WW = TextWidth(Weapon, 13.f, true);
		const float WV = TextWidth(Victim, 17.f, true);
		const float HS = E.bHeadshot ? 30.f : 0.f;
		const float Total = WK + WW + WV + HS + 56.f;
		const float X = W - 32.f - Total;

		const bool bMine = E.KillerId == Me || E.VictimId == Me;
		DrawBox(WithAlpha(GPanel, Alpha), X, Y, Total, 32.f);
		if (bMine)
		{
			DrawFrame(WithAlpha(E.VictimId == Me ? CSUI::Danger : CSUI::Accent, 0.9f * Alpha), X, Y, Total, 32.f);
		}

		float CursorX = X + 12.f;
		DrawLabel(Killer, CursorX, Y + 5.f, WithAlpha(PlayerColor(E.KillerId), Alpha), 17.f, true);
		CursorX += WK + 12.f;
		DrawLabel(Weapon, CursorX, Y + 9.f, WithAlpha(CSUI::TextDim, Alpha), 13.f, true);
		CursorX += WW + 8.f;
		if (E.bHeadshot)
		{
			DrawBox(WithAlpha(CSUI::Danger, 0.85f * Alpha), CursorX, Y + 8.f, 24.f, 16.f);
			DrawLabel(TEXT("HS"), CursorX + 12.f, Y + 8.f, WithAlpha(CSUI::Text, Alpha), 12.f, true, 0.5f);
			CursorX += HS;
		}
		CursorX += 4.f;
		DrawLabel(Victim, CursorX, Y + 5.f, WithAlpha(PlayerColor(E.VictimId), Alpha), 17.f, true);

		Y += 38.f;
	}
}

void ACSHUD::DrawInteractionPrompt()
{
	const ACSCharacter* Pawn = Cast<ACSCharacter>(GetOwningPawn());
	const ACSWorldPickup* Pickup = Pawn ? Pawn->GetFocusedPickup() : nullptr;
	const ACSAmmoMachine* Machine = Pawn ? Pawn->GetFocusedMachine() : nullptr;
	if (!Pickup && !Machine)
	{
		return;
	}

	// Show the key the player actually bound, not a hard-coded "E".
	FKey Key = GetDefault<UCSInputConfig>()->Key_Interact;
	if (const UCSSettingsSubsystem* Settings = UCSSettingsSubsystem::Get(this))
	{
		Key = Settings->GetKeyFor(TEXT("Interact"), Key);
	}

	const float W = Canvas->ClipX / S;
	const float H = Canvas->ClipY / S;
	const FString KeyText = Key.GetDisplayName().ToString().ToUpper();
	const FString Action = Machine
		? FString::Printf(TEXT("Buy ammo  $%d"), UCSShopSettings::Get()->AmmoMachinePrice)
		: FString::Printf(TEXT("Pick up  %s"), *Pickup->GetPromptName().ToString());

	const float WK = FMath::Max(28.f, TextWidth(KeyText, 16.f, true) + 14.f);
	const float WA = TextWidth(Action, 18.f, false);
	const float Total = WK + WA + 32.f;
	const float X = W * 0.5f - Total * 0.5f;
	const float Y = H * 0.5f + 44.f;

	DrawBox(GPanelStrong, X, Y, Total, 38.f);
	DrawBox(CSUI::Text, X + 8.f, Y + 6.f, WK, 26.f);
	DrawLabel(KeyText, X + 8.f + WK * 0.5f, Y + 9.f, FLinearColor(0.03f, 0.03f, 0.05f, 1.f), 16.f, true, 0.5f);
	DrawLabel(Action, X + WK + 20.f, Y + 8.f, CSUI::Text, 18.f);
}

void ACSHUD::DrawShopStatus(const FCSPlayerCombatRecord& Record)
{
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	const ACSGameState* GS = GetCSGameState();
	if (!Director || !GS)
	{
		return;
	}
	const int32 Me = Record.PlayerId;
	const float Protection = Director->GetProtectionRemaining(Me);
	const float BuyLeft = Director->GetBuyTimeRemaining(Me);
	if (Protection <= 0.f && BuyLeft <= 0.f)
	{
		return;
	}

	FKey Key = GetDefault<UCSInputConfig>()->Key_BuyMenu;
	if (const UCSSettingsSubsystem* Settings = UCSSettingsSubsystem::Get(this))
	{
		Key = Settings->GetKeyFor(TEXT("BuyMenu"), Key);
	}

	const float W = Canvas->ClipX / S;
	const float H = Canvas->ClipY / S;
	constexpr float PanelW = 420.f;
	const float X = W * 0.5f - PanelW * 0.5f;
	const float Y = H - 200.f;

	const bool bProtection = Protection > 0.f;
	const float Total = bProtection ? FMath::Max(1.f, GS->GetRules().ProtectionSeconds) : FMath::Max(1.f, GS->GetRules().BuySeconds);
	const float Left = bProtection ? Protection : BuyLeft;
	const FLinearColor Color = bProtection ? GArmor : CSUI::Money;

	DrawBox(GPanelStrong, X, Y, PanelW, 50.f);
	DrawBox(GTrack, X, Y + 46.f, PanelW, 4.f);
	DrawBox(Color, X, Y + 46.f, PanelW * FMath::Clamp(Left / Total, 0.f, 1.f), 4.f);
	DrawLabel(bProtection ? TEXT("SPAWN PROTECTION") : TEXT("BUY TIME"), X + 16.f, Y + 6.f, Color, 16.f, true);
	DrawLabel(FString::Printf(TEXT("%.1f s"), Left), X + 16.f, Y + 25.f, CSUI::Text, 15.f);
	if (BuyLeft > 0.f)
	{
		const FString KeyText = Key.GetDisplayName().ToString().ToUpper();
		const float KW = FMath::Max(28.f, TextWidth(KeyText, 16.f, true) + 14.f);
		DrawBox(CSUI::Text, X + PanelW - 110.f - KW, Y + 12.f, KW, 26.f);
		DrawLabel(KeyText, X + PanelW - 110.f - KW * 0.5f, Y + 15.f, FLinearColor(0.03f, 0.03f, 0.05f, 1.f), 16.f, true, 0.5f);
		DrawLabel(TEXT("OPEN SHOP"), X + PanelW - 14.f, Y + 16.f, CSUI::Text, 15.f, true, 1.f);
	}
	if (bProtection)
	{
		DrawLabel(TEXT("Moving, jumping, crouching or firing ends it"), W * 0.5f, Y - 24.f, CSUI::TextDim, 14.f, false, 0.5f);
	}
}

void ACSHUD::DrawNotice()
{
	const double Age = GetWorld()->GetRealTimeSeconds() - NoticeTime;
	if (Age < 0.0 || Age > 2.0 || NoticeText.IsEmpty())
	{
		return;
	}
	const float Alpha = FMath::Clamp(static_cast<float>(2.0 - Age) / 0.4f, 0.f, 1.f);
	const float W = Canvas->ClipX / S;
	const float H = Canvas->ClipY / S;
	const FString Text = NoticeText.ToString();
	const float TW = TextWidth(Text, 20.f, true) + 40.f;
	DrawBox(WithAlpha(GPanelStrong, Alpha), W * 0.5f - TW * 0.5f, H * 0.5f + 130.f, TW, 40.f);
	DrawLabel(Text, W * 0.5f, H * 0.5f + 137.f, WithAlpha(CSUI::Warning, Alpha), 20.f, true, 0.5f);
}

void ACSHUD::DrawQuickSlots()
{
	const ACSCharacter* Pawn = Cast<ACSCharacter>(GetOwningPawn());
	if (!Pawn)
	{
		return;
	}

	const ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(this, Pawn->GetOwningPlayerId());
	const int32 Equipped = Inventory ? Inventory->GetEquippedSlot() : INDEX_NONE;
	const int32 NumSlots = CSLoadout::NumSlots;

	const float W = Canvas->ClipX / S;
	const float H = Canvas->ClipY / S;
	constexpr float BoxW = 104.f;
	constexpr float BoxH = 50.f;
	constexpr float Gap = 5.f;
	const int32 Total = NumSlots;
	const float StartX = W * 0.5f - (Total * (BoxW + Gap) - Gap) * 0.5f;
	const float Y = H - BoxH - 26.f;

	auto Slot = [&](int32 Column, int32 KeyNumber, const FString& Label, const FString& Sub, bool bSelected, bool bEmpty, const FLinearColor& Tint)
	{
		const float X = StartX + Column * (BoxW + Gap);
		DrawBox(bSelected ? WithAlpha(CSUI::Accent, 0.22f) : GPanel, X, Y, BoxW, BoxH);
		DrawBox(bSelected ? CSUI::Accent : WithAlpha(Tint, bEmpty ? 0.f : 0.7f), X, Y + BoxH - 3.f, BoxW, 3.f);
		DrawLabel(FString::FromInt(KeyNumber), X + 7.f, Y + 5.f, bSelected ? CSUI::Accent : CSUI::TextDim, 12.f, true);
		DrawLabel(Label, X + 22.f, Y + 5.f, bEmpty ? CSUI::TextDim : CSUI::Text, 15.f, bSelected);
		if (!Sub.IsEmpty())
		{
			DrawLabel(Sub, X + 22.f, Y + 26.f, CSUI::TextDim, 13.f);
		}
	};

	const UCSItemSettings* Settings = UCSItemSettings::Get();
	for (int32 i = 0; i < NumSlots; ++i)
	{
		FCSInventorySlot Data;
		if (Inventory)
		{
			Inventory->GetSlot(i, Data);
		}
		const UCSItemDefinition* Item = Data.IsEmpty() ? nullptr : Settings->GetItem(Data.ItemIndex);

		FString Label = Item ? Item->DisplayName.ToString() : TEXT("-");
		if (Label.Len() > 10)
		{
			Label = Label.Left(9) + TEXT(".");
		}

		FString Sub;
		if (Item)
		{
			const UCSWeaponDefinition* Weapon = Item->Weapon.LoadSynchronous();
			Sub = (Weapon && Weapon->IsFirearm()) ? FString::Printf(TEXT("%d / %d"), Data.AmmoInMag, Data.Reserve)
				: (Data.Count > 1 ? FString::Printf(TEXT("x%d"), Data.Count) : FString());
		}

		Slot(i, i + 1, Label, Sub, Equipped == i, Item == nullptr, Item ? Item->PlaceholderColor : FLinearColor::Black);
	}
}

void ACSHUD::DrawDeathOverlay(const FCSPlayerCombatRecord& Record)
{
	const float W = Canvas->ClipX / S;
	const float H = Canvas->ClipY / S;

	// Darkened edges, lighter centre.
	DrawRect(FLinearColor(0.08f, 0.f, 0.f, 0.30f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	DrawBox(FLinearColor(0.f, 0.f, 0.f, 0.35f), 0.f, 0.f, W, 120.f);
	DrawBox(FLinearColor(0.f, 0.f, 0.f, 0.35f), 0.f, H - 120.f, W, 120.f);

	const float Y = H * 0.5f - 110.f;
	DrawBox(GPanelStrong, W * 0.5f - 300.f, Y, 600.f, 190.f);
	DrawBox(CSUI::Danger, W * 0.5f - 300.f, Y, 600.f, 4.f);
	DrawLabel(TEXT("ELIMINATED"), W * 0.5f, Y + 16.f, CSUI::Danger, 46.f, true, 0.5f);

	FString By;
	if (LastKillerId != 0 && LastKillerId != Record.PlayerId)
	{
		By = FString::Printf(TEXT("by %s"), *PlayerLabel(LastKillerId));
		if (!LastKillerWeapon.IsEmpty())
		{
			By += FString::Printf(TEXT("  -  %s"), *LastKillerWeapon);
		}
		if (bLastDeathHeadshot)
		{
			By += TEXT("  -  headshot");
		}
	}
	DrawLabel(By, W * 0.5f, Y + 80.f, CSUI::Text, 20.f, false, 0.5f);

	const ACSGameState* GS = GetCSGameState();
	const bool bRounds = GS && GS->GetRules().bRounds;
	FString Line;
	if (bRounds)
	{
		const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
		const int32 Alive = Director ? Director->CountAlive(Record.GetTeam()) : 0;
		Line = Alive > 0 ? FString::Printf(TEXT("Back next round  -  %d teammate%s still fighting"), Alive, Alive == 1 ? TEXT("") : TEXT("s"))
			: TEXT("Back next round");
	}
	else
	{
		const double NetNow = UCSAuthority::GetNetworkTimeSeconds(this);
		Line = FString::Printf(TEXT("Respawning in %.1f s"), FMath::Max(0.0, Record.RespawnAtNetworkTime - NetNow));
	}
	DrawLabel(Line, W * 0.5f, Y + 118.f, CSUI::Accent, 22.f, true, 0.5f);
	DrawLabel(TEXT("Your weapons dropped where you fell. The pistol comes back with you."),
		W * 0.5f, Y + 154.f, CSUI::TextDim, 14.f, false, 0.5f);
}
