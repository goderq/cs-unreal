// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Inventory screen: list, icon, count, description, equip/use, drop, inspect.
//
// Purely a view plus request buttons. It reads the Master-Client-owned
// ACSPlayerInventory that every peer replicates, and every button goes
// through the same authority RPCs as the keyboard (RequestSlot,
// RequestDropSlot). The list rebuilds only when the replicated slots change,
// so an item dropped here disappears from the list the moment the authority
// has actually removed it - not before.

#pragma once

#include "CoreMinimal.h"
#include "Inventory/CSPlayerInventory.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class SBox;
class ACSCharacter;
class UCSItemDefinition;
class UCSWeaponDefinition;

class CSFUSION_API SCSInventoryPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCSInventoryPanel) : _ShowCloseButton(true) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, WorldContext)
		SLATE_ARGUMENT(bool, ShowCloseButton)
		/** Besides Esc, this key closes the screen (the player's inventory binding). */
		SLATE_ARGUMENT(FKey, CloseKey)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** Selection: INDEX_NONE = starter pistol, 0..N-1 = inventory slot. */
	void Select(int32 Slot);
	int32 GetSelected() const { return Selected; }

	/** The same actions as the buttons, for the self-test. */
	void EquipSelected();
	void DropSelected();

private:
	ACSCharacter* GetLocalCharacter() const;
	const ACSPlayerInventory* GetInventory() const;

	/** Hash of what the list shows; the list is rebuilt when it changes. */
	uint32 ComputeSignature() const;

	void RebuildList();
	void RebuildDetails();

	TSharedRef<SWidget> MakeRow(int32 Slot, const UCSItemDefinition* Item, const FCSInventorySlot& Data, bool bEquipped);
	TSharedRef<SWidget> MakeStats(const UCSItemDefinition* Item, const UCSWeaponDefinition* Weapon, const FCSInventorySlot& Data) const;

	bool CanAct() const;

	TWeakObjectPtr<UObject> WorldContext;
	FSimpleDelegate OnClose;
	FKey CloseKey;
	bool bClosable = true;

	TSharedPtr<SVerticalBox> ListBox;
	TSharedPtr<SBox> DetailsBox;

	int32 Selected = INDEX_NONE;
	bool bInspect = false;
	uint32 LastSignature = 0;
};
