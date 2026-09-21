// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "UI/CSHUD.h"

#include "Camera/PlayerCameraManager.h"
#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
#include "Core/CSAuthority.h"
#include "Core/CSCombatSettings.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/CSGameState.h"
#include "Input/CSInputConfig.h"
#include "Inventory/CSPlayerInventory.h"
#include "Items/CSItemDefinition.h"
#include "Items/CSItemSettings.h"
#include "Pickups/CSWorldPickup.h"
#include "Settings/CSSettingsSubsystem.h"
#include "Weapons/CSWeaponComponent.h"
#include "Weapons/CSWeaponDefinition.h"

namespace
{
	const FLinearColor GPanel(0.f, 0.f, 0.f, 0.45f);
	const FLinearColor GPanelStrong(0.f, 0.f, 0.f, 0.6f);
	const FLinearColor GText(0.95f, 0.96f, 0.97f, 1.f);
	const FLinearColor GTextDim(0.62f, 0.65f, 0.70f, 1.f);
	const FLinearColor GAccent(0.2f, 0.85f, 0.48f, 1.f);
	const FLinearColor GHealth(0.92f, 0.94f, 0.95f, 1.f);
	const FLinearColor GArmor(0.40f, 0.68f, 1.f, 1.f);
	const FLinearColor GDanger(1.f, 0.30f, 0.28f, 1.f);

	constexpr double GHitMarkerSeconds = 0.22;
	constexpr double GKillMarkerSeconds = 0.45;
	constexpr double GDamageIndicatorSeconds = 1.3;

	// The engine's Canvas fonts are small (tuned for debug text); these bring
	// the HUD to a readable size at the 1080p reference resolution.
	constexpr float GMediumFontFactor = 1.5f;
	constexpr float GLargeFontFactor = 1.9f;
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

double ACSHUD::GetSecondsSinceHitMarker() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetRealTimeSeconds() - HitMarkerTime : 1000.0;
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

void ACSHUD::DrawBox(const FLinearColor& Color, float X, float Y, float W, float H)
{
	DrawRect(Color, X * S, Y * S, W * S, H * S);
}

float ACSHUD::TextWidth(const FString& Text, float Scale, bool bLarge) const
{
	Scale *= bLarge ? GLargeFontFactor : GMediumFontFactor;
	UFont* Font = GEngine ? (bLarge ? GEngine->GetLargeFont() : GEngine->GetMediumFont()) : nullptr;
	if (!Font)
	{
		return 0.f;
	}
	float W = 0.f;
	float H = 0.f;
	const_cast<ACSHUD*>(this)->GetTextSize(Text, W, H, Font, Scale * S);
	return W / S;
}

void ACSHUD::DrawLabel(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale, bool bLarge, float AlignX)
{
	const float FontScale = Scale * (bLarge ? GLargeFontFactor : GMediumFontFactor);
	UFont* Font = GEngine ? (bLarge ? GEngine->GetLargeFont() : GEngine->GetMediumFont()) : nullptr;
	if (AlignX != 0.f)
	{
		X -= TextWidth(Text, Scale, bLarge) * AlignX;
	}
	DrawText(Text, FLinearColor(0.f, 0.f, 0.f, 0.7f * Color.A), (X + 1.5f) * S, (Y + 1.5f) * S, Font, FontScale * S);
	DrawText(Text, Color, X * S, Y * S, Font, FontScale * S);
}

FString ACSHUD::PlayerLabel(int32 PlayerId) const
{
	return PlayerId == GetLocalPlayerId() ? TEXT("You") : FString::Printf(TEXT("Player %d"), PlayerId);
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
	BindToDirector();

	const double Now = GetWorld()->GetRealTimeSeconds();
	KillFeed.RemoveAll([this, Now](const FKillFeedEntry& E) { return Now - E.Time > KillFeedSeconds; });
	DamageIndicators.RemoveAll([Now](const FDamageIndicator& D) { return Now - D.Time > GDamageIndicatorSeconds; });

	DrawMatchInfo();
	DrawKillFeed();

	const ACSCharacter* Pawn = Cast<ACSCharacter>(GetOwningPawn());
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	const float W = Canvas->ClipX / S;
	FCSPlayerCombatRecord Record;
	if (!Pawn || !Director || !Director->GetRecord(Pawn->GetOwningPlayerId(), Record))
	{
		const FString Waiting = (!Pawn || !Director) ? TEXT("Waiting for match state...") : TEXT("Joining match...");
		DrawLabel(Waiting, W * 0.5f, 600.f, GText, 1.2f, false, 0.5f);
		return;
	}

	if (!Record.bAlive)
	{
		const double NetNow = UCSAuthority::GetNetworkTimeSeconds(this);
		DrawDeathOverlay(static_cast<float>(FMath::Max(0.0, Record.RespawnAtNetworkTime - NetNow)));
	}
	else
	{
		DrawCrosshair();
		DrawHitMarker();
		DrawInteractionPrompt();
	}

	DrawDamageIndicators();
	DrawVitals();
	DrawAmmo();
	DrawQuickSlots();
}

// ---------------------------------------------------------------------------
// Elements
// ---------------------------------------------------------------------------

void ACSHUD::DrawCrosshair()
{
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;

	// Open the crosshair with the current spread so bloom is visible.
	float Gap = CrosshairGap;
	if (const ACSCharacter* Pawn = Cast<ACSCharacter>(GetOwningPawn()))
	{
		if (const UCSWeaponComponent* Weapon = Pawn->GetWeaponComponent())
		{
			Gap += Weapon->GetCurrentSpreadDegrees() * 6.f;
		}
	}

	const float G = Gap * S;
	const float L = CrosshairLength * S;
	const float T = FMath::Max(1.f, 2.f * S);
	DrawLine(CX - G - L, CY, CX - G, CY, CrosshairColor, T);
	DrawLine(CX + G, CY, CX + G + L, CY, CrosshairColor, T);
	DrawLine(CX, CY - G - L, CX, CY - G, CrosshairColor, T);
	DrawLine(CX, CY + G, CX, CY + G + L, CrosshairColor, T);
	DrawRect(CrosshairColor, CX - T * 0.5f, CY - T * 0.5f, T, T);
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
	const FLinearColor Color = bHitMarkerKill ? FLinearColor(GDanger.R, GDanger.G, GDanger.B, Alpha)
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
		const FLinearColor Color(GDanger.R, GDanger.G, GDanger.B, 0.85f * Alpha);

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

void ACSHUD::DrawVitals()
{
	const ACSCharacter* Pawn = Cast<ACSCharacter>(GetOwningPawn());
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	FCSPlayerCombatRecord Record;
	if (!Pawn || !Director || !Director->GetRecord(Pawn->GetOwningPlayerId(), Record))
	{
		return;
	}

	const UCSCombatSettings* Settings = UCSCombatSettings::Get();
	const float H = Canvas->ClipY / S;
	const float X = 40.f;
	const float Y = H - 150.f;
	const float PanelW = 320.f;

	DrawBox(GPanel, X, Y, PanelW, 104.f);

	const float HealthPct = FMath::Clamp(Record.Health / FMath::Max(1.f, Settings->MaxHealth), 0.f, 1.f);
	const float ArmorPct = FMath::Clamp(Record.Armor / FMath::Max(1.f, Settings->MaxArmor), 0.f, 1.f);
	const FLinearColor HealthColor = HealthPct > 0.5f ? GHealth : (HealthPct > 0.25f ? FLinearColor(1.f, 0.8f, 0.3f) : GDanger);

	DrawLabel(TEXT("HP"), X + 16.f, Y + 14.f, GTextDim, 0.9f);
	DrawLabel(FString::FromInt(FMath::CeilToInt(Record.Health)), X + 60.f, Y + 2.f, HealthColor, 1.5f, true);
	DrawBox(FLinearColor(1.f, 1.f, 1.f, 0.12f), X + 150.f, Y + 22.f, 154.f, 8.f);
	DrawBox(HealthColor, X + 150.f, Y + 22.f, 154.f * HealthPct, 8.f);

	DrawLabel(TEXT("ARM"), X + 16.f, Y + 62.f, GTextDim, 0.9f);
	DrawLabel(FString::FromInt(FMath::CeilToInt(Record.Armor)), X + 60.f, Y + 52.f, GArmor, 1.2f, true);
	DrawBox(FLinearColor(1.f, 1.f, 1.f, 0.12f), X + 150.f, Y + 70.f, 154.f, 8.f);
	DrawBox(GArmor, X + 150.f, Y + 70.f, 154.f * ArmorPct, 8.f);

	DrawLabel(FString::Printf(TEXT("K %d   D %d"), Record.Kills, Record.Deaths), X, Y - 30.f, GTextDim, 1.f);
}

void ACSHUD::DrawAmmo()
{
	const ACSCharacter* Pawn = Cast<ACSCharacter>(GetOwningPawn());
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	if (!Pawn || !Director)
	{
		return;
	}

	// From the authoritative loadout: the equipped inventory weapon or the
	// starter pistol (unlimited reserve by design).
	const FCSLoadoutView Loadout = Director->GetLoadout(Pawn->GetOwningPlayerId());
	const UCSWeaponDefinition* Weapon = Loadout.Weapon;

	const float W = Canvas->ClipX / S;
	const float H = Canvas->ClipY / S;
	const float Right = W - 40.f;
	const float Y = H - 150.f;

	DrawBox(GPanel, Right - 320.f, Y, 320.f, 104.f);

	const FString WeaponName = Weapon ? Weapon->DisplayName.ToString().ToUpper() : TEXT("NO WEAPON");
	DrawLabel(WeaponName, Right - 16.f, Y + 12.f, GTextDim, 0.95f, false, 1.f);

	if (Loadout.bReloading)
	{
		DrawLabel(TEXT("RELOADING"), Right - 16.f, Y + 44.f, FLinearColor(1.f, 0.8f, 0.3f), 1.2f, true, 1.f);
		return;
	}
	if (!Weapon)
	{
		return;
	}

	const FString Reserve = Loadout.Reserve < 0 ? TEXT("INF") : FString::FromInt(Loadout.Reserve);
	const FLinearColor MagColor = Loadout.RoundsInMag == 0 ? GDanger
		: (Loadout.RoundsInMag <= FMath::Max(1, Weapon->MagazineSize / 4) ? FLinearColor(1.f, 0.8f, 0.3f) : GText);

	const float ReserveW = TextWidth(Reserve, 1.1f, false);
	DrawLabel(Reserve, Right - 16.f, Y + 58.f, GTextDim, 1.1f, false, 1.f);
	DrawLabel(TEXT("/"), Right - 26.f - ReserveW, Y + 58.f, GTextDim, 1.1f, false, 1.f);
	DrawLabel(FString::FromInt(Loadout.RoundsInMag), Right - 44.f - ReserveW, Y + 36.f, MagColor, 1.7f, true, 1.f);

	if (Loadout.RoundsInMag == 0)
	{
		DrawLabel(TEXT("PRESS R TO RELOAD"), W * 0.5f, H * 0.5f + 90.f, GDanger, 1.f, false, 0.5f);
	}
}

void ACSHUD::DrawMatchInfo()
{
	const ACSGameState* GS = GetWorld() ? GetWorld()->GetGameState<ACSGameState>() : nullptr;
	if (!GS)
	{
		return;
	}

	FString Phase;
	switch (GS->GetMatchPhase())
	{
	case ECSMatchPhase::WaitingForPlayers:	Phase = TEXT("WAITING FOR PLAYERS"); break;
	case ECSMatchPhase::Warmup:				Phase = TEXT("WARMUP"); break;
	case ECSMatchPhase::InProgress:			Phase = TEXT("ROUND"); break;
	case ECSMatchPhase::PostMatch:			Phase = TEXT("ROUND OVER"); break;
	}

	const float W = Canvas->ClipX / S;
	const int32 Remaining = FMath::Max(0, FMath::CeilToInt(GS->GetPhaseTimeRemaining()));
	const FString Timer = FString::Printf(TEXT("%d:%02d"), Remaining / 60, Remaining % 60);

	DrawBox(GPanel, W * 0.5f - 110.f, 18.f, 220.f, 70.f);
	DrawLabel(Timer, W * 0.5f, 18.f, GText, 1.3f, true, 0.5f);
	DrawLabel(FString::Printf(TEXT("%s  -  %d PLAYERS"), *Phase, UCSAuthority::GetRoomPlayerCount(this)),
		W * 0.5f, 60.f, GTextDim, 0.8f, false, 0.5f);
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
	float Y = 24.f;

	for (int32 i = KillFeed.Num() - 1; i >= 0; --i)
	{
		const FKillFeedEntry& E = KillFeed[i];
		const float Alpha = FMath::Clamp(static_cast<float>(KillFeedSeconds - (Now - E.Time)), 0.f, 1.f);

		const FString Killer = E.KillerId == E.VictimId ? FString() : PlayerLabel(E.KillerId);
		const FString Middle = FString::Printf(TEXT("  [%s%s]  "), E.Weapon.IsEmpty() ? TEXT("?") : *E.Weapon, E.bHeadshot ? TEXT(" HS") : TEXT(""));
		const FString Victim = PlayerLabel(E.VictimId);

		const float WK = TextWidth(Killer, 0.95f, false);
		const float WM = TextWidth(Middle, 0.85f, false);
		const float WV = TextWidth(Victim, 0.95f, false);
		const float Total = WK + WM + WV + 24.f;
		const float X = W - 40.f - Total;

		const bool bMine = E.KillerId == Me || E.VictimId == Me;
		DrawBox(bMine ? FLinearColor(0.6f, 0.08f, 0.06f, 0.55f * Alpha) : FLinearColor(0.f, 0.f, 0.f, 0.45f * Alpha), X, Y, Total, 32.f);

		float CursorX = X + 12.f;
		DrawLabel(Killer, CursorX, Y + 5.f, E.KillerId == Me ? FLinearColor(GAccent.R, GAccent.G, GAccent.B, Alpha) : FLinearColor(1.f, 1.f, 1.f, Alpha), 0.95f);
		CursorX += WK;
		DrawLabel(Middle, CursorX, Y + 6.f, FLinearColor(GTextDim.R, GTextDim.G, GTextDim.B, Alpha), 0.85f);
		CursorX += WM;
		DrawLabel(Victim, CursorX, Y + 5.f, E.VictimId == Me ? FLinearColor(GDanger.R, GDanger.G, GDanger.B, Alpha) : FLinearColor(1.f, 1.f, 1.f, Alpha), 0.95f);

		Y += 38.f;
	}
}

void ACSHUD::DrawInteractionPrompt()
{
	const ACSCharacter* Pawn = Cast<ACSCharacter>(GetOwningPawn());
	const ACSWorldPickup* Pickup = Pawn ? Pawn->GetFocusedPickup() : nullptr;
	if (!Pickup)
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
	const FString Name = Pickup->GetPromptName().ToString();
	const FString KeyText = FString::Printf(TEXT(" %s "), *Key.GetDisplayName().ToString().ToUpper());
	const FString Action = FString::Printf(TEXT("  Pick up  %s"), *Name);

	const float WK = TextWidth(KeyText, 1.f, false);
	const float WA = TextWidth(Action, 1.f, false);
	const float X = W * 0.5f - (WK + WA) * 0.5f;
	const float Y = H * 0.5f + 44.f;

	DrawBox(GPanelStrong, X - 10.f, Y - 6.f, WK + WA + 20.f, 36.f);
	DrawBox(FLinearColor(1.f, 1.f, 1.f, 0.9f), X, Y - 1.f, WK, 26.f);
	DrawLabel(KeyText, X, Y, FLinearColor(0.05f, 0.05f, 0.05f, 1.f), 1.f);
	DrawLabel(Action, X + WK, Y, GText, 1.f);
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
	const int32 NumSlots = Inventory ? Inventory->GetSlots().Num() : 0;

	const float W = Canvas->ClipX / S;
	const float H = Canvas->ClipY / S;
	const float BoxW = 112.f;
	const float BoxH = 50.f;
	const float Gap = 6.f;
	const int32 Total = NumSlots + 1; // +1 for the starter pistol
	const float StartX = W * 0.5f - (Total * (BoxW + Gap) - Gap) * 0.5f;
	const float Y = H - BoxH - 22.f;

	auto Slot = [&](int32 Column, int32 KeyNumber, const FString& Label, const FString& Sub, bool bSelected, bool bEmpty, const FLinearColor& Tint)
	{
		const float X = StartX + Column * (BoxW + Gap);
		DrawBox(bSelected ? FLinearColor(0.08f, 0.35f, 0.18f, 0.8f) : GPanel, X, Y, BoxW, BoxH);
		DrawBox(bSelected ? GAccent : FLinearColor(Tint.R, Tint.G, Tint.B, bEmpty ? 0.f : 0.8f), X, Y + BoxH - 3.f, BoxW, 3.f);
		DrawLabel(FString::FromInt(KeyNumber), X + 6.f, Y + 4.f, GTextDim, 0.75f);
		DrawLabel(Label, X + 20.f, Y + 5.f, bEmpty ? GTextDim : GText, 0.85f);
		if (!Sub.IsEmpty())
		{
			DrawLabel(Sub, X + 20.f, Y + 26.f, GTextDim, 0.75f);
		}
	};

	// Slot 1 is always the starter pistol - it lives outside the inventory.
	Slot(0, 1, TEXT("Pistol"), TEXT("starter"), Equipped == INDEX_NONE, false, FLinearColor(0.5f, 0.5f, 0.55f));

	const UCSItemSettings* Settings = UCSItemSettings::Get();
	for (int32 i = 0; i < NumSlots; ++i)
	{
		const FCSInventorySlot& Data = Inventory->GetSlots()[i];
		const UCSItemDefinition* Item = Data.IsEmpty() ? nullptr : Settings->GetItem(Data.ItemIndex);

		FString Label = Item ? Item->DisplayName.ToString() : TEXT("-");
		if (Label.Len() > 11)
		{
			Label = Label.Left(10) + TEXT(".");
		}

		FString Sub;
		if (Item)
		{
			Sub = Item->IsWeapon() ? FString::Printf(TEXT("%d rds"), Data.AmmoInMag)
				: (Data.Count > 1 ? FString::Printf(TEXT("x%d"), Data.Count) : FString());
		}

		Slot(i + 1, i + 2, Label, Sub, Equipped == i, Item == nullptr, Item ? Item->PlaceholderColor : FLinearColor::Black);
	}
}

void ACSHUD::DrawDeathOverlay(float SecondsToRespawn)
{
	DrawRect(FLinearColor(0.35f, 0.f, 0.f, 0.35f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	const float W = Canvas->ClipX / S;
	const float H = Canvas->ClipY / S;
	DrawLabel(TEXT("YOU DIED"), W * 0.5f, H * 0.5f - 60.f, GDanger, 1.6f, true, 0.5f);
	DrawLabel(FString::Printf(TEXT("Respawning in %.1f s"), SecondsToRespawn), W * 0.5f, H * 0.5f + 10.f, GText, 1.2f, false, 0.5f);
	DrawLabel(TEXT("Your inventory dropped where you fell. Your pistol comes back with you."),
		W * 0.5f, H * 0.5f + 44.f, GTextDim, 0.9f, false, 0.5f);
}
