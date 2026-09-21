// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Shared look for every menu: one palette, one font family, one button style.
//
// Why Slate and not UMG Widget Blueprints: the whole UI is code, so it diffs,
// reviews and merges like the rest of the project, and it cannot silently
// break the way a binary .uasset widget does when a C++ property is renamed.
// Slate widgets are added straight to the game viewport, which applies the
// project's DPI curve - the layout is authored for 1080p and scales to 1440p
// and 4K by itself.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Input/Reply.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;

namespace CSUI
{
	// --- Palette -------------------------------------------------------------
	inline const FLinearColor Backdrop    = FLinearColor(0.008f, 0.011f, 0.018f, 0.93f);
	inline const FLinearColor Panel       = FLinearColor(0.020f, 0.026f, 0.038f, 0.95f);
	inline const FLinearColor PanelRaised = FLinearColor(0.038f, 0.048f, 0.068f, 1.00f);
	inline const FLinearColor Hover       = FLinearColor(0.070f, 0.090f, 0.125f, 1.00f);
	inline const FLinearColor Accent      = FLinearColor(1.000f, 0.580f, 0.120f, 1.00f);
	inline const FLinearColor AccentDim   = FLinearColor(0.420f, 0.220f, 0.040f, 1.00f);
	inline const FLinearColor Danger      = FLinearColor(0.950f, 0.270f, 0.230f, 1.00f);
	inline const FLinearColor Text        = FLinearColor(0.940f, 0.950f, 0.965f, 1.00f);
	inline const FLinearColor TextDim     = FLinearColor(0.520f, 0.570f, 0.650f, 1.00f);
	inline const FLinearColor Warning     = FLinearColor(1.000f, 0.820f, 0.300f, 1.00f);
	// v1.1
	inline const FLinearColor TeamAlpha   = FLinearColor(0.250f, 0.620f, 1.000f, 1.00f);
	inline const FLinearColor TeamBravo   = FLinearColor(1.000f, 0.360f, 0.260f, 1.00f);
	inline const FLinearColor Money       = FLinearColor(0.420f, 0.920f, 0.450f, 1.00f);
	inline const FLinearColor Stroke      = FLinearColor(1.000f, 1.000f, 1.000f, 0.07f);

	/** Team colour; free-for-all (None) is the accent. */
	FLinearColor TeamColor(uint8 Team);

	/** "$2,500". */
	FText MoneyText(int32 Amount);

	/** "1:05" for seconds. */
	FText ClockText(float Seconds);

	/** Thin horizontal bar, Fraction 0..1, for item stats. */
	TSharedRef<SWidget> MakeStatBar(const TAttribute<float>& Fraction, const FLinearColor& Color, float Height = 5.f);

	/** Roboto (the engine font, always cooked). */
	FSlateFontInfo Font(int32 Size, bool bBold = false);

	/** A plain white brush to tint. */
	const FSlateBrush* WhiteBrush();

	enum class EButtonKind : uint8 { Normal, Primary, Danger, Nav };

	const FButtonStyle& ButtonStyle(EButtonKind Kind);

	/** Full-width button with a left-aligned label. */
	TSharedRef<SWidget> MakeButton(const TAttribute<FText>& Label, FOnClicked OnClicked,
		EButtonKind Kind = EButtonKind::Normal, const TAttribute<bool>& bEnabled = true, int32 FontSize = 18);

	TSharedRef<SWidget> MakeHeader(const FText& Title, const FText& Subtitle = FText::GetEmpty());

	TSharedRef<SWidget> MakeSectionLabel(const FText& Label);

	/** Label on the left, arbitrary control on the right. */
	TSharedRef<SWidget> MakeRow(const FText& Label, TSharedRef<SWidget> Control);

	/** Colored square with a short tag, standing in for item art until Stage 6. */
	TSharedRef<SWidget> MakeItemIcon(const FLinearColor& Color, const FText& Tag, float Size);

	/** Two/three-letter tag derived from an item name ("AK-47" -> "AK"). */
	FText ShortTag(const FText& Name);
}

DECLARE_DELEGATE_OneParam(FCSOnSelectionChanged, int32);

/**
 * "<  value  >" selector. Used instead of a combo box for resolution, window
 * mode, quality and so on: one click per step, no popup to lose focus to,
 * and it reads well on a TV-distance 1440p screen.
 */
class CSFUSION_API SCSSelector : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCSSelector) : _SelectedIndex(0) {}
		SLATE_ARGUMENT(TArray<FText>, Options)
		SLATE_ARGUMENT(int32, SelectedIndex)
		SLATE_EVENT(FCSOnSelectionChanged, OnSelectionChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	int32 GetSelectedIndex() const { return Selected; }
	void SetSelectedIndex(int32 Index);
	void SetOptions(const TArray<FText>& InOptions, int32 Index);

private:
	FReply Step(int32 Delta);
	FText GetLabel() const;

	TArray<FText> Options;
	int32 Selected = 0;
	FCSOnSelectionChanged OnSelectionChanged;
};
