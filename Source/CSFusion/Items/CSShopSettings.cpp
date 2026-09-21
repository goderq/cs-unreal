// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Items/CSShopSettings.h"

#define LOCTEXT_NAMESPACE "CSShop"

UCSShopSettings::UCSShopSettings()
{
	// Defaults; a DefaultGame.ini list replaces them entirely.
	auto Add = [this](const TCHAR* Id, int32 Price, ECSShopCategory Cat, int32 Bundle, const TCHAR* Blurb)
	{
		FCSShopEntry& E = Entries.AddDefaulted_GetRef();
		E.ItemId = Id;
		E.Price = Price;
		E.Category = Cat;
		E.Bundle = Bundle;
		E.Blurb = Blurb;
	};
	Add(TEXT("ak47"), 2700, ECSShopCategory::Rifles, 2, TEXT("Hard hitting automatic rifle. Kicks up."));
	Add(TEXT("m4"), 3100, ECSShopCategory::Rifles, 2, TEXT("Accurate automatic rifle, controllable recoil."));
	Add(TEXT("smg"), 1250, ECSShopCategory::SMGs, 2, TEXT("Very fast fire rate, strong up close, cheap."));
	Add(TEXT("shotgun"), 1200, ECSShopCategory::Heavy, 2, TEXT("Pump action. Devastating inside a few metres."));
	Add(TEXT("sniper"), 4750, ECSShopCategory::Snipers, 2, TEXT("Bolt action with a scope. Huge damage per shot."));
	Add(TEXT("armor"), 650, ECSShopCategory::Gear, 1, TEXT("Body armor: absorbs a share of incoming damage."));
	Add(TEXT("medkit"), 400, ECSShopCategory::Gear, 1, TEXT("Restores health. Use it from its slot."));
	Add(TEXT("grenade"), 300, ECSShopCategory::Gear, 1, TEXT("Frag grenade. Select it, press fire to throw - it goes off 2 s later."));
	Add(TEXT("ammo_rifle"), 100, ECSShopCategory::Ammo, 1, TEXT("Rifle rounds for the AK-47 and M4."));
	Add(TEXT("ammo_smg"), 80, ECSShopCategory::Ammo, 1, TEXT("Rounds for the SMG."));
	Add(TEXT("ammo_shells"), 100, ECSShopCategory::Ammo, 1, TEXT("Shells for the shotgun."));
	Add(TEXT("ammo_sniper"), 150, ECSShopCategory::Ammo, 1, TEXT("Rounds for the sniper rifle."));
}

FText UCSShopSettings::CategoryName(ECSShopCategory Category)
{
	switch (Category)
	{
	case ECSShopCategory::Rifles:	return LOCTEXT("Rifles", "RIFLES");
	case ECSShopCategory::SMGs:		return LOCTEXT("SMGs", "SMG");
	case ECSShopCategory::Heavy:	return LOCTEXT("Heavy", "HEAVY");
	case ECSShopCategory::Snipers:	return LOCTEXT("Snipers", "SNIPERS");
	case ECSShopCategory::Gear:		return LOCTEXT("Gear", "GEAR");
	default:						return LOCTEXT("Ammo", "AMMO");
	}
}

#undef LOCTEXT_NAMESPACE
