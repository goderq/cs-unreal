// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v1.1 buy menu (B). A category rail on the left, the price list in the
// middle, details of the hovered item on the right.
//
// Purely a view plus buy requests: every purchase goes through
// ACSCharacter::RequestBuy to the authority, which re-checks the shop window,
// the money and the inventory (ACSMatchDirector::TryBuy). What the list shows
// as affordable / owned is only a hint read from replicated state.

#pragma once

#include "CoreMinimal.h"
#include "Items/CSShopSettings.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class SBox;
class ACSCharacter;
class UCSItemDefinition;
struct FSlateBrush;

class CSFUSION_API SCSShopPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCSShopPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, WorldContext)
		/** Besides Esc, this key closes the menu (the player's shop binding). */
		SLATE_ARGUMENT(FKey, CloseKey)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** Call when (re)shown: rebuilds the list on the next tick. */
	void Refresh();

	/** Same path as a click. Returns false if the client-side hint already says no. */
	bool Buy(int32 ShopIndex);

	/** What the list would say about an entry right now. */
	ECSBuyResult Predict(int32 ShopIndex) const;

private:
	ACSCharacter* GetLocalCharacter() const;
	int32 GetMoney() const;
	float GetTimeLeft() const;

	/** Category filter; INDEX_NONE = everything. */
	void SetCategory(int32 Category);
	void RebuildList();
	void RebuildDetails();

	/** Hash of money + inventory, so affordability refreshes when they change. */
	uint32 ComputeSignature() const;

	TSharedRef<SWidget> MakeCategoryButton(int32 Category, const FText& Label);
	TSharedRef<SWidget> MakeRow(int32 ShopIndex, int32 Hotkey);
	TSharedRef<SWidget> MakeIcon(const UCSItemDefinition* Item, float Size, TArray<TSharedPtr<FSlateBrush>>& Sink);

	TWeakObjectPtr<UObject> WorldContext;
	FSimpleDelegate OnClose;
	FKey CloseKey;

	TSharedPtr<SVerticalBox> ListBox;
	TSharedPtr<SBox> DetailsBox;

	int32 Category = INDEX_NONE;
	int32 Hovered = INDEX_NONE;
	int32 ShownDetails = -2;

	/** Shop indices in the list, in hotkey order (1..9). */
	TArray<int32> Visible;

	uint32 LastSignature = 0;

	/** Feedback line under the list ("Bought AK-47", "Not enough money"). */
	FText Feedback;
	FLinearColor FeedbackColor = FLinearColor::White;
	double FeedbackUntil = 0.0;

	/** Icon brushes must outlive the images that point at them. */
	TArray<TSharedPtr<FSlateBrush>> ListBrushes;
	TArray<TSharedPtr<FSlateBrush>> DetailBrushes;
};
