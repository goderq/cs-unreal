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
	Add(TEXT("pistol"), 200, ECSShopCategory::Pistols, 1, TEXT("Semi-automatic sidearm, 13 rounds. Everyone spawns with one."));
	Add(TEXT("armor"), 650, ECSShopCategory::Gear, 1, TEXT("Body armor: absorbs half of incoming damage."));
	Add(TEXT("grenade"), 300, ECSShopCategory::Grenades, 1, TEXT("HE grenade. Pull the pin, throw - it goes off 1.6 s later."));
	Add(TEXT("flashbang"), 200, ECSShopCategory::Grenades, 1, TEXT("Blinds everyone who looks at it. Carry up to two."));
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
	case ECSShopCategory::Pistols:	return LOCTEXT("Pistols", "PISTOLS");
	case ECSShopCategory::Grenades:	return LOCTEXT("Grenades", "GRENADES");
	default:						return LOCTEXT("Ammo", "AMMO");
	}
}

#undef LOCTEXT_NAMESPACE
