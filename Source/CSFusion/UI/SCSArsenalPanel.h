// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v2.0 phase 6 (AUDIT K2, decision a): the ARSENAL and CATALOG pages of the
// main menu. No currency or items outside a match - money is earned and
// spent inside it - so these are reference pages, read from the same data
// the match uses (item registry, weapon definitions, shop, mode rules):
//
//   Arsenal  the five loadout slots, then every weapon with its full numbers:
//            damage, shots and time to kill, fire rate, magazine, reload,
//            accuracy, recoil, range, aim time, price
//   Catalog  everything the in-match shop sells, by category, with prices,
//            the ammo machine price and what kills pay in each mode

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class SVerticalBox;
class UCSItemDefinition;
struct FSlateBrush;

class CSFUSION_API SCSArsenalPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCSArsenalPanel) : _bCatalog(false) {}
		/** false = Arsenal, true = Catalog. */
		SLATE_ARGUMENT(bool, bCatalog)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Arsenal: show a weapon's card (item registry index). */
	void SelectWeapon(int32 ItemIndex);

private:
	TSharedRef<SWidget> MakeArsenal();
	TSharedRef<SWidget> MakeCatalog();
	TSharedRef<SWidget> MakeIcon(const UCSItemDefinition* Item, float Size);
	void RebuildDetails();

	/** Item registry indices of the weapons, in shop order. */
	TArray<int32> Weapons;
	int32 Selected = INDEX_NONE;
	TSharedPtr<SBox> Details;
	TArray<TSharedPtr<FSlateBrush>> Brushes;
};
