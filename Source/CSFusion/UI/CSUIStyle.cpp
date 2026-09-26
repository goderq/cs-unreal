// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "UI/CSUIStyle.h"

#include "Audio/CSAudioSettings.h"
#include "Brushes/SlateColorBrush.h"
#include "Sound/SoundBase.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace CSUI
{
	FSlateFontInfo Font(int32 Size, bool bBold)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	const FSlateBrush* WhiteBrush()
	{
		static const FSlateColorBrush Brush(FLinearColor::White);
		return &Brush;
	}

	const FButtonStyle& ButtonStyle(EButtonKind Kind)
	{
		auto Make = [](const FLinearColor& Normal, const FLinearColor& Hovered, const FLinearColor& Pressed)
		{
			FButtonStyle Style;
			Style.SetNormal(FSlateColorBrush(Normal));
			Style.SetHovered(FSlateColorBrush(Hovered));
			Style.SetPressed(FSlateColorBrush(Pressed));
			Style.SetDisabled(FSlateColorBrush(FLinearColor(0.03f, 0.03f, 0.035f, 0.8f)));
			Style.SetNormalPadding(FMargin(0.f));
			Style.SetPressedPadding(FMargin(0.f));
			// Generated UI sounds (see CSAudioSettings); null-safe if missing.
			const UCSAudioSettings* Audio = UCSAudioSettings::Get();
			// The style is static and outlives every world: root the sounds so the
			// garbage collector cannot free them under Slate (that crashed the
			// ESC menu after a map change).
			if (USoundBase* Click = Audio->UIClick.LoadSynchronous())
			{
				FSlateSound Sound;
				Click->AddToRoot();
				Sound.SetResourceObject(Click);
				Style.SetPressedSound(Sound);
			}
			if (USoundBase* HoverSound = Audio->UIHover.LoadSynchronous())
			{
				FSlateSound Sound;
				HoverSound->AddToRoot();
				Sound.SetResourceObject(HoverSound);
				Style.SetHoveredSound(Sound);
			}
			return Style;
		};

		static const FButtonStyle NormalStyle  = Make(PanelRaised, Hover, AccentDim);
		static const FButtonStyle PrimaryStyle = Make(AccentDim, Accent * 0.8f, Accent);
		static const FButtonStyle DangerStyle  = Make(FLinearColor(0.30f, 0.07f, 0.07f, 1.f), FLinearColor(0.55f, 0.12f, 0.11f, 1.f), Danger);
		static const FButtonStyle NavStyle     = Make(FLinearColor::Transparent, Hover, AccentDim);

		switch (Kind)
		{
		case EButtonKind::Primary:	return PrimaryStyle;
		case EButtonKind::Danger:	return DangerStyle;
		case EButtonKind::Nav:		return NavStyle;
		default:					return NormalStyle;
		}
	}

	TSharedRef<SWidget> MakeButton(const TAttribute<FText>& Label, FOnClicked OnClicked,
		EButtonKind Kind, const TAttribute<bool>& bEnabled, int32 FontSize)
	{
		return SNew(SButton).IsFocusable(false)
			.ButtonStyle(&ButtonStyle(Kind))
			.IsEnabled(bEnabled)
			.OnClicked(OnClicked)
			.ContentPadding(FMargin(20.f, 11.f))
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(Font(FontSize, Kind != EButtonKind::Normal))
				.ColorAndOpacity(Text)
			];
	}

	TSharedRef<SWidget> MakeHeader(const FText& Title, const FText& Subtitle)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(Title).Font(Font(30, true)).ColorAndOpacity(Text)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text(Subtitle).Font(Font(14)).ColorAndOpacity(TextDim)
				.Visibility(Subtitle.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 18.f)
			[
				SNew(SBox).HeightOverride(2.f).WidthOverride(64.f).HAlign(HAlign_Left)
				[
					SNew(SImage).Image(WhiteBrush()).ColorAndOpacity(Accent)
				]
			];
	}

	TSharedRef<SWidget> MakeSectionLabel(const FText& Label)
	{
		return SNew(SBox).Padding(FMargin(0.f, 18.f, 0.f, 8.f))
			[
				SNew(STextBlock).Text(Label).Font(Font(13, true)).ColorAndOpacity(Accent)
			];
	}

	TSharedRef<SWidget> MakeRow(const FText& Label, TSharedRef<SWidget> Control)
	{
		return SNew(SBox).Padding(FMargin(0.f, 3.f))
			[
				SNew(SBorder)
				.BorderImage(WhiteBrush())
				.BorderBackgroundColor(PanelRaised)
				.Padding(FMargin(16.f, 6.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(0.45f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(Label).Font(Font(15)).ColorAndOpacity(Text)
					]
					+ SHorizontalBox::Slot().FillWidth(0.55f).VAlign(VAlign_Center)
					[
						Control
					]
				]
			];
	}

	TSharedRef<SWidget> MakeItemIcon(const FLinearColor& Color, const FText& Tag, float Size)
	{
		return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SImage).Image(WhiteBrush()).ColorAndOpacity(Color * 0.55f + FLinearColor(0.f, 0.f, 0.f, 0.45f))
				]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(Tag).Font(Font(FMath::RoundToInt(Size * 0.3f), true)).ColorAndOpacity(Text)
				]
			];
	}

	FText ShortTag(const FText& Name)
	{
		FString S = Name.ToString().ToUpper();
		FString Tag;
		for (const TCHAR C : S)
		{
			if (FChar::IsAlnum(C))
			{
				Tag.AppendChar(C);
				if (Tag.Len() == 3)
				{
					break;
				}
			}
			else if (Tag.Len() >= 2)
			{
				break;
			}
		}
		return FText::FromString(Tag);
	}

	FLinearColor TeamColor(uint8 Team)
	{
		return Team == 1 ? TeamAlpha : (Team == 2 ? TeamBravo : Accent);
	}

	FText MoneyText(int32 Amount)
	{
		FNumberFormattingOptions Options;
		Options.UseGrouping = true;
		return FText::FromString(TEXT("$") + FText::AsNumber(Amount, &Options).ToString());
	}

	FText ClockText(float Seconds)
	{
		const int32 Total = FMath::Max(0, FMath::CeilToInt(Seconds));
		return FText::FromString(FString::Printf(TEXT("%d:%02d"), Total / 60, Total % 60));
	}

	TSharedRef<SWidget> MakeStatBar(const TAttribute<float>& Fraction, const FLinearColor& Color, float Height)
	{
		// Track + fill; the fill width follows the attribute every frame.
		return SNew(SBox).HeightOverride(Height).WidthOverride(220.f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SImage).Image(WhiteBrush()).ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.08f))
				]
				+ SOverlay::Slot().HAlign(HAlign_Left)
				[
					SNew(SBox)
					.WidthOverride_Lambda([Fraction]() { return 220.f * FMath::Clamp(Fraction.Get(), 0.f, 1.f); })
					[
						SNew(SImage).Image(WhiteBrush()).ColorAndOpacity(Color)
					]
				]
			];
	}
}

// ---------------------------------------------------------------------------
// SCSSelector
// ---------------------------------------------------------------------------

void SCSSelector::Construct(const FArguments& InArgs)
{
	Options = InArgs._Options;
	Selected = Options.Num() > 0 ? FMath::Clamp(InArgs._SelectedIndex, 0, Options.Num() - 1) : 0;
	OnSelectionChanged = InArgs._OnSelectionChanged;

	auto Arrow = [this](const TCHAR* Glyph, int32 Delta) -> TSharedRef<SWidget>
	{
		return SNew(SButton).IsFocusable(false)
			.ButtonStyle(&CSUI::ButtonStyle(CSUI::EButtonKind::Nav))
			.ContentPadding(FMargin(12.f, 4.f))
			.OnClicked(this, &SCSSelector::Step, Delta)
			[
				SNew(STextBlock).Text(FText::FromString(Glyph)).Font(CSUI::Font(16, true)).ColorAndOpacity(CSUI::Accent)
			];
	};

	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[ Arrow(TEXT("<"), -1) ]
		+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(this, &SCSSelector::GetLabel).Font(CSUI::Font(15)).ColorAndOpacity(CSUI::Text)
		]
		+ SHorizontalBox::Slot().AutoWidth()[ Arrow(TEXT(">"), +1) ]
	];
}

FReply SCSSelector::Step(int32 Delta)
{
	if (Options.Num() > 0)
	{
		Selected = (Selected + Delta + Options.Num()) % Options.Num();
		OnSelectionChanged.ExecuteIfBound(Selected);
	}
	return FReply::Handled();
}

FText SCSSelector::GetLabel() const
{
	return Options.IsValidIndex(Selected) ? Options[Selected] : FText::GetEmpty();
}

void SCSSelector::SetSelectedIndex(int32 Index)
{
	if (Options.Num() > 0)
	{
		Selected = FMath::Clamp(Index, 0, Options.Num() - 1);
	}
}

void SCSSelector::SetOptions(const TArray<FText>& InOptions, int32 Index)
{
	Options = InOptions;
	SetSelectedIndex(Index);
}

// ---------------------------------------------------------------------------
// Animated page switcher (phase 6)
// ---------------------------------------------------------------------------

void SCSAnimatedSwitcher::Construct(const FArguments& InArgs)
{
	Intro = FCurveSequence(0.f, 0.2f, ECurveEaseFunction::CubicOut);
	ChildSlot
	[
		SAssignNew(Switcher, SWidgetSwitcher)
	];
}

int32 SCSAnimatedSwitcher::AddPage(TSharedRef<SWidget> Page)
{
	Switcher->AddSlot()[Page];
	return Switcher->GetNumWidgets() - 1;
}

void SCSAnimatedSwitcher::SetActivePage(int32 Index)
{
	if (Index == Switcher->GetActiveWidgetIndex())
	{
		return;
	}
	Switcher->SetActiveWidgetIndex(Index);
	PlayIntro();
}

int32 SCSAnimatedSwitcher::GetActivePage() const
{
	return Switcher->GetActiveWidgetIndex();
}

void SCSAnimatedSwitcher::PlayIntro()
{
	Intro.Play(AsShared());
}

void SCSAnimatedSwitcher::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const float T = Intro.IsPlaying() ? Intro.GetLerp() : 1.f;
	SetRenderOpacity(T);
	SetRenderTransform(FSlateRenderTransform(FVector2f(0.f, (1.f - T) * 14.f)));
}

// ---------------------------------------------------------------------------
// Crosshair preview (phase 6)
// ---------------------------------------------------------------------------

int32 SCSCrosshairPreview::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	auto Box = [&](float X, float Y, float W, float H, const FLinearColor& Color, int32 Layer)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, Layer,
			AllottedGeometry.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y))),
			CSUI::WhiteBrush(), ESlateDrawEffect::None, Color);
	};
	// A light and a dark half, to judge the colour and the outline on both.
	Box(0.f, 0.f, Size.X * 0.5f, Size.Y, FLinearColor(0.62f, 0.58f, 0.5f), LayerId);
	Box(Size.X * 0.5f, 0.f, Size.X * 0.5f, Size.Y, FLinearColor(0.08f, 0.09f, 0.1f), LayerId);

	int32 Style = 0, Color = 0;
	float Len = 8.f, Gap = 5.f, Thick = 2.f;
	bool bOutline = true;
	if (Source)
	{
		Source(Style, Color, Len, Gap, Thick, bOutline);
	}
	const FLinearColor Col = CSUI::CrosshairColors[FMath::Clamp(Color, 0, 5)];
	const FLinearColor Outline(0.f, 0.f, 0.f, bOutline ? 0.6f : 0.f);
	const float CX = Size.X * 0.5f, CY = Size.Y * 0.5f;
	auto Bar = [&](float X, float Y, float W, float H)
	{
		Box(X - 1.f, Y - 1.f, W + 2.f, H + 2.f, Outline, LayerId + 1);
		Box(X, Y, W, H, Col, LayerId + 2);
	};
	if (Style == 3)
	{
		const float R = FMath::Max(Gap + Len * 0.5f, 3.f);
		for (int32 i = 0; i < 24; ++i)
		{
			const float A = i * UE_TWO_PI / 24.f;
			Bar(CX + FMath::Cos(A) * R - Thick * 0.5f, CY + FMath::Sin(A) * R - Thick * 0.5f, Thick, Thick);
		}
		Bar(CX - Thick * 0.5f, CY - Thick * 0.5f, Thick, Thick);
		return LayerId + 2;
	}
	if (Style == 0 || Style == 1)
	{
		Bar(CX - Gap - Len, CY - Thick * 0.5f, Len, Thick);
		Bar(CX + Gap, CY - Thick * 0.5f, Len, Thick);
		Bar(CX - Thick * 0.5f, CY - Gap - Len, Thick, Len);
		Bar(CX - Thick * 0.5f, CY + Gap, Thick, Len);
	}
	if (Style == 1 || Style == 2)
	{
		Bar(CX - Thick * 0.5f, CY - Thick * 0.5f, Thick, Thick);
	}
	return LayerId + 2;
}
