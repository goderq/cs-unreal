// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "UI/CSHUD.h"

#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
#include "Core/CSAuthority.h"
#include "Core/CSCombatSettings.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameModes/CSGameState.h"
#include "Inventory/CSPlayerInventory.h"
#include "Items/CSItemDefinition.h"
#include "Items/CSItemSettings.h"
#include "Pickups/CSWorldPickup.h"
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
		DrawInteractionPrompt();
	}

	DrawStatusPanel();
	DrawQuickSlots();
}

void ACSHUD::DrawInteractionPrompt()
{
	const ACSCharacter* Pawn = Cast<ACSCharacter>(GetOwningPawn());
	const ACSWorldPickup* Pickup = Pawn ? Pawn->GetFocusedPickup() : nullptr;
	if (!Pickup)
	{
		return;
	}

	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;
	const FString Name = Pickup->GetPromptName().ToString();

	DrawShadowedText(Name, CX - Name.Len() * 5.5f, CY + 40.f, FLinearColor(1.f, 0.9f, 0.5f), 1.3f);
	DrawShadowedText(TEXT("Press E to pick up"), CX - 80.f, CY + 66.f, FLinearColor::White, 1.f);
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

	const float BoxW = 118.f;
	const float BoxH = 46.f;
	const float Gap = 6.f;
	const int32 Total = NumSlots + 1; // +1 for the starter pistol
	const float StartX = Canvas->ClipX * 0.5f - (Total * (BoxW + Gap) - Gap) * 0.5f;
	const float Y = Canvas->ClipY - BoxH - 18.f;

	auto DrawBox = [&](int32 Column, int32 KeyNumber, const FString& Label, const FString& Sub, bool bSelected)
	{
		const float X = StartX + Column * (BoxW + Gap);
		DrawRect(bSelected ? FLinearColor(0.2f, 0.6f, 0.3f, 0.75f) : FLinearColor(0.f, 0.f, 0.f, 0.5f), X, Y, BoxW, BoxH);
		DrawShadowedText(FString::FromInt(KeyNumber), X + 5.f, Y + 3.f, FLinearColor(0.7f, 0.7f, 0.7f), 0.8f);
		DrawShadowedText(Label, X + 18.f, Y + 4.f, FLinearColor::White, 0.9f);
		if (!Sub.IsEmpty())
		{
			DrawShadowedText(Sub, X + 18.f, Y + 24.f, FLinearColor(0.8f, 0.8f, 0.8f), 0.8f);
		}
	};

	// Slot 1 is always the starter pistol - it lives outside the inventory.
	DrawBox(0, 1, TEXT("Pistol"), TEXT("starter"), Equipped == INDEX_NONE);

	const UCSItemSettings* Settings = UCSItemSettings::Get();
	for (int32 i = 0; i < NumSlots; ++i)
	{
		const FCSInventorySlot& Slot = Inventory->GetSlots()[i];
		const UCSItemDefinition* Item = Slot.IsEmpty() ? nullptr : Settings->GetItem(Slot.ItemIndex);

		FString Label = Item ? Item->DisplayName.ToString() : TEXT("-");
		if (Label.Len() > 12)
		{
			Label = Label.Left(11) + TEXT(".");
		}

		FString Sub;
		if (Item)
		{
			Sub = Item->IsWeapon() ? FString::Printf(TEXT("%d rds"), Slot.AmmoInMag)
				: (Slot.Count > 1 ? FString::Printf(TEXT("x%d"), Slot.Count) : FString());
		}

		DrawBox(i + 1, i + 2, Label, Sub, Equipped == i);
	}
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
	const float Bottom = Canvas->ClipY - 60.f; // HP/ammo sit above the quick-slot bar

	const FLinearColor HealthColor = Record.Health > 50.f
		? FLinearColor(0.85f, 1.f, 0.85f)
		: (Record.Health > 25.f ? FLinearColor(1.f, 0.85f, 0.3f) : FLinearColor(1.f, 0.3f, 0.3f));

	DrawShadowedText(FString::Printf(TEXT("HP  %d"), FMath::RoundToInt(Record.Health)), Left, Bottom - 110.f, HealthColor, 1.6f);
	DrawShadowedText(FString::Printf(TEXT("ARM %d"), FMath::RoundToInt(Record.Armor)), Left + 170.f, Bottom - 110.f, FLinearColor(0.6f, 0.8f, 1.f), 1.6f);

	// Ammo, bottom right, from the authoritative loadout: the equipped
	// inventory weapon or the starter pistol (unlimited reserve by design).
	const FCSLoadoutView Loadout = Director->GetLoadout(Pawn->GetOwningPlayerId());
	const UCSWeaponDefinition* Weapon = Loadout.Weapon;
	const bool bReloading = Loadout.bReloading;

	FString AmmoText;
	if (bReloading)
	{
		AmmoText = TEXT("RELOADING");
	}
	else if (Weapon)
	{
		AmmoText = FString::Printf(TEXT("%d / %s"), Loadout.RoundsInMag,
			Loadout.Reserve < 0 ? TEXT("INF") : *FString::FromInt(Loadout.Reserve));
	}
	else
	{
		AmmoText = TEXT("NO WEAPON");
	}

	const FLinearColor AmmoColor = (Loadout.RoundsInMag == 0 && !bReloading)
		? FLinearColor(1.f, 0.3f, 0.3f)
		: FLinearColor::White;

	DrawShadowedText(AmmoText, Canvas->ClipX - 260.f, Bottom - 110.f, AmmoColor, 1.6f);
	if (Weapon)
	{
		DrawShadowedText(Weapon->DisplayName.ToString(), Canvas->ClipX - 260.f, Bottom - 75.f, FLinearColor(0.8f, 0.8f, 0.8f));
	}

	DrawShadowedText(FString::Printf(TEXT("K %d   D %d"), Record.Kills, Record.Deaths), Left, Bottom - 75.f, FLinearColor(0.8f, 0.8f, 0.8f));
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
