// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "UI/CSHUD.h"

#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
#include "Core/CSAuthority.h"
#include "Core/CSCombatSettings.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameModes/CSGameState.h"
#include "Weapons/CSWeaponComponent.h"
#include "Weapons/CSWeaponDefinition.h"

void ACSHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	DrawMatchInfo();

	const ACSCharacter* Pawn = Cast<ACSCharacter>(GetOwningPawn());
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	if (!Pawn || !Director)
	{
		DrawShadowedText(TEXT("Waiting for match state..."), 40.f, Canvas->ClipY - 80.f, FLinearColor::White);
		return;
	}

	FCSPlayerCombatRecord Record;
	if (!Director->GetRecord(Pawn->GetOwningPlayerId(), Record))
	{
		DrawShadowedText(TEXT("Joining match..."), 40.f, Canvas->ClipY - 80.f, FLinearColor::White);
		return;
	}

	if (!Record.bAlive)
	{
		const double Now = UCSAuthority::GetNetworkTimeSeconds(this);
		DrawDeathOverlay(static_cast<float>(FMath::Max(0.0, Record.RespawnAtNetworkTime - Now)));
	}
	else
	{
		DrawCrosshair();
	}

	DrawStatusPanel();
}

void ACSHUD::DrawShadowedText(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale)
{
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	DrawText(Text, FLinearColor(0.f, 0.f, 0.f, 0.8f), X + 2.f, Y + 2.f, Font, Scale);
	DrawText(Text, Color, X, Y, Font, Scale);
}

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

	const float L = CrosshairLength;
	DrawLine(CX - Gap - L, CY, CX - Gap, CY, CrosshairColor, 2.f);
	DrawLine(CX + Gap, CY, CX + Gap + L, CY, CrosshairColor, 2.f);
	DrawLine(CX, CY - Gap - L, CX, CY - Gap, CrosshairColor, 2.f);
	DrawLine(CX, CY + Gap, CX, CY + Gap + L, CrosshairColor, 2.f);
}

void ACSHUD::DrawStatusPanel()
{
	const ACSCharacter* Pawn = Cast<ACSCharacter>(GetOwningPawn());
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	FCSPlayerCombatRecord Record;
	if (!Pawn || !Director || !Director->GetRecord(Pawn->GetOwningPlayerId(), Record))
	{
		return;
	}

	const float Left = 40.f;
	const float Bottom = Canvas->ClipY - 60.f;

	const FLinearColor HealthColor = Record.Health > 50.f
		? FLinearColor(0.85f, 1.f, 0.85f)
		: (Record.Health > 25.f ? FLinearColor(1.f, 0.85f, 0.3f) : FLinearColor(1.f, 0.3f, 0.3f));

	DrawShadowedText(FString::Printf(TEXT("HP  %d"), FMath::RoundToInt(Record.Health)), Left, Bottom - 30.f, HealthColor, 1.6f);
	DrawShadowedText(FString::Printf(TEXT("ARM %d"), FMath::RoundToInt(Record.Armor)), Left + 170.f, Bottom - 30.f, FLinearColor(0.6f, 0.8f, 1.f), 1.6f);

	// Ammo, bottom right. The starter pistol has unlimited reserve by design.
	const UCSWeaponComponent* WeaponComp = Pawn->GetWeaponComponent();
	const UCSWeaponDefinition* Weapon = WeaponComp ? WeaponComp->GetActiveWeapon() : nullptr;

	const double Now = UCSAuthority::GetNetworkTimeSeconds(this);
	const bool bReloading = Record.ReloadCompleteNetworkTime > 0.0 && Now < Record.ReloadCompleteNetworkTime;

	FString AmmoText;
	if (bReloading)
	{
		AmmoText = TEXT("RELOADING");
	}
	else if (Weapon)
	{
		AmmoText = FString::Printf(TEXT("%d / %s"), Record.StarterRoundsInMag,
			Weapon->HasUnlimitedReserve() ? TEXT("INF") : *FString::FromInt(Weapon->ReserveAmmo));
	}
	else
	{
		AmmoText = TEXT("NO WEAPON");
	}

	const FLinearColor AmmoColor = (Record.StarterRoundsInMag == 0 && !bReloading)
		? FLinearColor(1.f, 0.3f, 0.3f)
		: FLinearColor::White;

	DrawShadowedText(AmmoText, Canvas->ClipX - 260.f, Bottom - 30.f, AmmoColor, 1.6f);
	if (Weapon)
	{
		DrawShadowedText(Weapon->DisplayName.ToString(), Canvas->ClipX - 260.f, Bottom + 5.f, FLinearColor(0.8f, 0.8f, 0.8f));
	}

	DrawShadowedText(FString::Printf(TEXT("K %d   D %d"), Record.Kills, Record.Deaths), Left, Bottom + 5.f, FLinearColor(0.8f, 0.8f, 0.8f));
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

	const int32 Remaining = FMath::CeilToInt(GS->GetPhaseTimeRemaining());
	const FString Text = FString::Printf(TEXT("%s   %d:%02d   players %d"),
		*Phase, Remaining / 60, Remaining % 60, UCSAuthority::GetRoomPlayerCount(this));

	DrawShadowedText(Text, Canvas->ClipX * 0.5f - 150.f, 24.f, FLinearColor::White, 1.2f);
}

void ACSHUD::DrawDeathOverlay(float SecondsToRespawn)
{
	DrawRect(FLinearColor(0.4f, 0.f, 0.f, 0.35f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	DrawShadowedText(TEXT("YOU DIED"), Canvas->ClipX * 0.5f - 90.f, Canvas->ClipY * 0.5f - 40.f, FLinearColor(1.f, 0.3f, 0.3f), 2.4f);
	DrawShadowedText(FString::Printf(TEXT("Respawning in %.1f"), SecondsToRespawn),
		Canvas->ClipX * 0.5f - 95.f, Canvas->ClipY * 0.5f + 20.f, FLinearColor::White, 1.3f);
}
