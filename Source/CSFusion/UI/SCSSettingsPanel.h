// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Settings screen, shared by the main menu and the in-match ESC menu.
//
// Edits a working copy; APPLY writes it through UCSSettingsSubsystem (which
// saves it), BACK discards it. Key rebinding: click a key, press the new
// one; a key already used by another action is swapped onto it.

#pragma once

#include "CoreMinimal.h"
#include "Settings/CSSettingsSubsystem.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class SCSSelector;
class UCSInputConfig;

class CSFUSION_API SCSSettingsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCSSettingsPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, WorldContext)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Re-reads current settings into the working copy (call when shown). */
	void Refresh();

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	bool IsCapturingKey() const { return !CapturingBinding.IsNone(); }

private:
	UCSSettingsSubsystem* GetSettings() const;
	const UCSInputConfig* GetInputConfig() const;

	TSharedRef<SWidget> MakeSlider(float Min, float Max, TFunction<float()> Get, TFunction<void(float)> Set, TFunction<FText(float)> Format);
	void RebuildKeyRows();
	void SyncSelectors();
	FReply OnApply();
	FReply OnReset();
	FReply OnBack();

	TWeakObjectPtr<UObject> WorldContext;
	FSimpleDelegate OnClose;

	FCSPlayerPreferences WorkingPrefs;
	FCSGraphicsSettings WorkingGfx;
	TArray<FIntPoint> Resolutions;

	TSharedPtr<SCSSelector> ResolutionSelector;
	TSharedPtr<SCSSelector> WindowModeSelector;
	TSharedPtr<SCSSelector> FrameLimitSelector;
	TSharedPtr<SCSSelector> QualitySelector;
	TSharedPtr<SCSSelector> VSyncSelector;
	TSharedPtr<SCSSelector> InvertSelector;
	TSharedPtr<SVerticalBox> KeyRows;

	FName CapturingBinding;
	FText StatusText;
};
