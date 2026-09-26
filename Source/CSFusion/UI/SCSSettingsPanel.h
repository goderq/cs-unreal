// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Settings screen, shared by the main menu and the in-match ESC menu.
// v2.0 phase 6: six tabs (video, controls, crosshair with a live sample,
// audio, accessibility, key bindings) that ease in when switched.
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

	/** Tab 0 video .. 5 key bindings. */
	void ShowTab(int32 Index);

private:
	UCSSettingsSubsystem* GetSettings() const;
	const UCSInputConfig* GetInputConfig() const;

	TSharedRef<SWidget> MakeSlider(float Min, float Max, TFunction<float()> Get, TFunction<void(float)> Set, TFunction<FText(float)> Format);
	void RebuildKeyRows();
	void SyncSelectors();
	TSharedRef<SWidget> MakeGraphicsRows();
	FReply OnAutoDetect();
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
	TSharedPtr<SCSSelector> PresetSelector;
	TSharedPtr<SCSSelector> GroupSelectors[static_cast<int32>(ECSGraphicsGroup::Count)];
	TSharedPtr<SCSSelector> UpscalerSelector;
	TSharedPtr<SCSSelector> RenderScaleSelector;
	TSharedPtr<SCSSelector> RayTracingSelector;
	/** Upscaler value (ECSUpscaler) behind each option; DLSS only where it works. */
	TArray<int32> UpscalerChoices;
	TSharedPtr<SCSSelector> VSyncSelector;
	TSharedPtr<SCSSelector> InvertSelector;
	TSharedPtr<SCSSelector> ShowFpsSelector;
	// Phase 6 (C11).
	TSharedPtr<SCSSelector> NetStatsSelector;
	TSharedPtr<SCSSelector> AimToggleSelector;
	TSharedPtr<SCSSelector> CrosshairStyleSelector;
	TSharedPtr<SCSSelector> CrosshairColorSelector;
	TSharedPtr<SCSSelector> CrosshairOutlineSelector;
	TSharedPtr<SCSSelector> CrosshairDynamicSelector;
	TSharedPtr<SCSSelector> ColorVisionSelector;
	TSharedPtr<SCSSelector> MotionBlurSelector;
	TSharedPtr<SCSSelector> ReduceFlashSelector;
	/** Tabs: video, controls, crosshair, audio, accessibility, keys. */
	TSharedPtr<class SCSAnimatedSwitcher> PageSwitcher;
	TSharedPtr<SVerticalBox> KeyRows;

	FName CapturingBinding;
	FText StatusText;
};
