// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "UI/SCSSettingsPanel.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Input/CSInputConfig.h"
#include "UI/CSUIStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CSSettings"

namespace
{
	const EWindowMode::Type GWindowModes[] = { EWindowMode::Fullscreen, EWindowMode::WindowedFullscreen, EWindowMode::Windowed };

	int32 WindowModeToIndex(EWindowMode::Type Mode)
	{
		for (int32 i = 0; i < UE_ARRAY_COUNT(GWindowModes); ++i)
		{
			if (GWindowModes[i] == Mode)
			{
				return i;
			}
		}
		return 1;
	}
}

void SCSSettingsPanel::Construct(const FArguments& InArgs)
{
	WorldContext = InArgs._WorldContext;
	OnClose = InArgs._OnClose;

	Resolutions = UCSSettingsSubsystem::GetSupportedResolutions();

	TArray<FText> ResolutionOptions;
	for (const FIntPoint& R : Resolutions)
	{
		ResolutionOptions.Add(FText::FromString(FString::Printf(TEXT("%d x %d"), R.X, R.Y)));
	}

	TArray<FText> FrameOptions;
	for (const float Limit : UCSSettingsSubsystem::GetFrameRateChoices())
	{
		FrameOptions.Add(Limit <= 0.f ? LOCTEXT("Unlimited", "Unlimited") : FText::AsNumber(FMath::RoundToInt(Limit)));
	}

	const TArray<FText> OffOn = { LOCTEXT("Off", "Off"), LOCTEXT("On", "On") };

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			CSUI::MakeHeader(LOCTEXT("Title", "SETTINGS"), LOCTEXT("Subtitle", "Saved on this PC only. Nothing here affects gameplay state."))
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot().Padding(FMargin(0.f, 0.f, 14.f, 0.f))
			[
				SNew(SVerticalBox)

				// --- Video --------------------------------------------------
				+ SVerticalBox::Slot().AutoHeight()[ CSUI::MakeSectionLabel(LOCTEXT("Video", "VIDEO")) ]
				+ SVerticalBox::Slot().AutoHeight()
				[
					CSUI::MakeRow(LOCTEXT("Resolution", "Resolution"),
						SAssignNew(ResolutionSelector, SCSSelector).Options(ResolutionOptions)
						.OnSelectionChanged_Lambda([this](int32 i) { if (Resolutions.IsValidIndex(i)) { WorkingGfx.Resolution = Resolutions[i]; } }))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					CSUI::MakeRow(LOCTEXT("DisplayMode", "Display mode"),
						SAssignNew(WindowModeSelector, SCSSelector)
						.Options({ LOCTEXT("Fullscreen", "Fullscreen"), LOCTEXT("Borderless", "Borderless window"), LOCTEXT("Windowed", "Windowed") })
						.OnSelectionChanged_Lambda([this](int32 i) { WorkingGfx.WindowMode = GWindowModes[FMath::Clamp(i, 0, 2)]; }))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					CSUI::MakeRow(LOCTEXT("FrameLimit", "FPS limit"),
						SAssignNew(FrameLimitSelector, SCSSelector).Options(FrameOptions)
						.OnSelectionChanged_Lambda([this](int32 i) { WorkingGfx.FrameRateLimit = UCSSettingsSubsystem::GetFrameRateChoices()[i]; }))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					CSUI::MakeRow(LOCTEXT("VSync", "VSync"),
						SAssignNew(VSyncSelector, SCSSelector).Options(OffOn)
						.OnSelectionChanged_Lambda([this](int32 i) { WorkingGfx.bVSync = i == 1; }))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					CSUI::MakeRow(LOCTEXT("Quality", "Graphics quality"),
						SAssignNew(QualitySelector, SCSSelector)
						.Options({ LOCTEXT("Low", "Low"), LOCTEXT("Medium", "Medium"), LOCTEXT("High", "High"), LOCTEXT("Epic", "Epic") })
						.OnSelectionChanged_Lambda([this](int32 i) { WorkingGfx.Quality = i; }))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					CSUI::MakeRow(LOCTEXT("ShowFps", "Show FPS"),
						SAssignNew(ShowFpsSelector, SCSSelector).Options(OffOn)
						.OnSelectionChanged_Lambda([this](int32 i) { WorkingPrefs.bShowFps = i == 1; }))
				]

				// --- Controls -----------------------------------------------
				+ SVerticalBox::Slot().AutoHeight()[ CSUI::MakeSectionLabel(LOCTEXT("Controls", "CONTROLS")) ]
				+ SVerticalBox::Slot().AutoHeight()
				[
					CSUI::MakeRow(LOCTEXT("Sensitivity", "Mouse sensitivity"),
						MakeSlider(0.05f, 5.f,
							[this]() { return WorkingPrefs.MouseSensitivity; },
							[this](float V) { WorkingPrefs.MouseSensitivity = V; },
							[](float V) { return FText::AsNumber(V, &FNumberFormattingOptions().SetMinimumFractionalDigits(2).SetMaximumFractionalDigits(2)); }))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					CSUI::MakeRow(LOCTEXT("InvertY", "Invert mouse Y"),
						SAssignNew(InvertSelector, SCSSelector).Options(OffOn)
						.OnSelectionChanged_Lambda([this](int32 i) { WorkingPrefs.bInvertY = i == 1; }))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					CSUI::MakeRow(LOCTEXT("FOV", "Field of view"),
						MakeSlider(70.f, 120.f,
							[this]() { return WorkingPrefs.FieldOfView; },
							[this](float V) { WorkingPrefs.FieldOfView = FMath::RoundToFloat(V); },
							[](float V) { return FText::Format(LOCTEXT("Deg", "{0} deg"), FText::AsNumber(FMath::RoundToInt(V))); }))
				]

				// --- Audio --------------------------------------------------
				+ SVerticalBox::Slot().AutoHeight()[ CSUI::MakeSectionLabel(LOCTEXT("Audio", "AUDIO")) ]
				+ SVerticalBox::Slot().AutoHeight()
				[
					CSUI::MakeRow(LOCTEXT("Master", "Master volume"),
						MakeSlider(0.f, 1.f, [this]() { return WorkingPrefs.MasterVolume; }, [this](float V) { WorkingPrefs.MasterVolume = V; },
							[](float V) { return FText::AsPercent(V); }))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					CSUI::MakeRow(LOCTEXT("Music", "Music volume"),
						MakeSlider(0.f, 1.f, [this]() { return WorkingPrefs.MusicVolume; }, [this](float V) { WorkingPrefs.MusicVolume = V; },
							[](float V) { return FText::AsPercent(V); }))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					CSUI::MakeRow(LOCTEXT("Effects", "Effects volume"),
						MakeSlider(0.f, 1.f, [this]() { return WorkingPrefs.EffectsVolume; }, [this](float V) { WorkingPrefs.EffectsVolume = V; },
							[](float V) { return FText::AsPercent(V); }))
				]

				// --- Key bindings -------------------------------------------
				+ SVerticalBox::Slot().AutoHeight()[ CSUI::MakeSectionLabel(LOCTEXT("Keys", "KEY BINDINGS")) ]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(KeyRows, SVerticalBox)
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() { return StatusText; })
			.Font(CSUI::Font(14))
			.ColorAndOpacity(CSUI::Warning)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)
			[
				CSUI::MakeButton(LOCTEXT("Apply", "APPLY"), FOnClicked::CreateSP(this, &SCSSettingsPanel::OnApply), CSUI::EButtonKind::Primary)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)
			[
				CSUI::MakeButton(LOCTEXT("Reset", "RESET TO DEFAULTS"), FOnClicked::CreateSP(this, &SCSSettingsPanel::OnReset))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				CSUI::MakeButton(LOCTEXT("Back", "BACK"), FOnClicked::CreateSP(this, &SCSSettingsPanel::OnBack))
			]
		]
	];

	Refresh();
}

UCSSettingsSubsystem* SCSSettingsPanel::GetSettings() const
{
	return UCSSettingsSubsystem::Get(WorldContext.Get());
}

const UCSInputConfig* SCSSettingsPanel::GetInputConfig() const
{
	// The asset the character uses; the class defaults carry the same keys if
	// it cannot be loaded for any reason.
	static TWeakObjectPtr<const UCSInputConfig> Cached;
	if (!Cached.IsValid())
	{
		Cached = LoadObject<UCSInputConfig>(nullptr, TEXT("/Game/Input/DA_CSInputConfig.DA_CSInputConfig"));
	}
	return Cached.IsValid() ? Cached.Get() : GetDefault<UCSInputConfig>();
}

TSharedRef<SWidget> SCSSettingsPanel::MakeSlider(float Min, float Max, TFunction<float()> Get,
	TFunction<void(float)> Set, TFunction<FText(float)> Format)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
		[
			SNew(SSlider)
			.IsFocusable(false)
			.MinValue(Min)
			.MaxValue(Max)
			.Value_Lambda([Get]() { return Get(); })
			.OnValueChanged_Lambda([Set](float V) { Set(V); })
			.SliderBarColor(CSUI::Hover)
			.SliderHandleColor(CSUI::Accent)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(72.f)
			[
				SNew(STextBlock)
				.Text_Lambda([Get, Format]() { return Format(Get()); })
				.Font(CSUI::Font(14))
				.ColorAndOpacity(CSUI::TextDim)
				.Justification(ETextJustify::Right)
			]
		];
}

void SCSSettingsPanel::Refresh()
{
	if (const UCSSettingsSubsystem* Settings = GetSettings())
	{
		WorkingPrefs = Settings->GetPreferences();
		WorkingGfx = Settings->GetGraphics();
	}
	CapturingBinding = NAME_None;
	StatusText = FText::GetEmpty();
	SyncSelectors();
	RebuildKeyRows();
}

void SCSSettingsPanel::SyncSelectors()
{
	int32 ResIndex = Resolutions.IndexOfByKey(WorkingGfx.Resolution);
	if (ResIndex == INDEX_NONE)
	{
		// Current mode is not in the list (e.g. an odd window size): add it.
		Resolutions.Add(WorkingGfx.Resolution);
		TArray<FText> Options;
		for (const FIntPoint& R : Resolutions)
		{
			Options.Add(FText::FromString(FString::Printf(TEXT("%d x %d"), R.X, R.Y)));
		}
		ResIndex = Resolutions.Num() - 1;
		ResolutionSelector->SetOptions(Options, ResIndex);
	}
	ResolutionSelector->SetSelectedIndex(ResIndex);
	WindowModeSelector->SetSelectedIndex(WindowModeToIndex(WorkingGfx.WindowMode));

	const TArray<float>& Limits = UCSSettingsSubsystem::GetFrameRateChoices();
	int32 LimitIndex = Limits.IndexOfByPredicate([this](float L) { return FMath::IsNearlyEqual(L, WorkingGfx.FrameRateLimit, 0.5f); });
	FrameLimitSelector->SetSelectedIndex(LimitIndex == INDEX_NONE ? Limits.Num() - 1 : LimitIndex);

	QualitySelector->SetSelectedIndex(WorkingGfx.Quality);
	VSyncSelector->SetSelectedIndex(WorkingGfx.bVSync ? 1 : 0);
	InvertSelector->SetSelectedIndex(WorkingPrefs.bInvertY ? 1 : 0);
	ShowFpsSelector->SetSelectedIndex(WorkingPrefs.bShowFps ? 1 : 0);
}

void SCSSettingsPanel::RebuildKeyRows()
{
	if (!KeyRows.IsValid())
	{
		return;
	}
	KeyRows->ClearChildren();

	for (const FCSRebindableBinding& Binding : GetInputConfig()->GetRebindableBindings())
	{
		const FName Id = Binding.Id;
		const FKey Default = Binding.DefaultKey;

		auto KeyLabel = [this, Id, Default]()
		{
			if (CapturingBinding == Id)
			{
				return LOCTEXT("PressKey", "Press a key...  (Esc cancels)");
			}
			const FKey* Override = WorkingPrefs.KeyOverrides.Find(Id);
			return (Override ? *Override : Default).GetDisplayName();
		};

		KeyRows->AddSlot().AutoHeight()
		[
			CSUI::MakeRow(Binding.DisplayName,
				SNew(SButton).IsFocusable(false)
				.ButtonStyle(&CSUI::ButtonStyle(CSUI::EButtonKind::Normal))
				.ContentPadding(FMargin(14.f, 6.f))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this, Id]()
				{
					CapturingBinding = Id;
					StatusText = FText::GetEmpty();
					return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::SetDirectly);
				})
				[
					SNew(STextBlock).Text_Lambda(KeyLabel).Font(CSUI::Font(15, true)).ColorAndOpacity(CSUI::Text)
				])
		];
	}
}

FReply SCSSettingsPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (CapturingBinding.IsNone())
	{
		return FReply::Unhandled();
	}

	const FKey Key = InKeyEvent.GetKey();
	const FName Id = CapturingBinding;
	CapturingBinding = NAME_None;

	if (Key == EKeys::Escape)
	{
		return FReply::Handled();
	}

	// Keys reserved for the fixed bindings (menu, slots, mouse look/fire).
	static const FKey Reserved[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::BackSpace };
	for (const FKey& R : Reserved)
	{
		if (Key == R)
		{
			StatusText = FText::Format(LOCTEXT("Reserved", "{0} is reserved (weapon slots / scoreboard)."), Key.GetDisplayName());
			return FReply::Handled();
		}
	}

	const TArray<FCSRebindableBinding> Bindings = GetInputConfig()->GetRebindableBindings();
	auto Current = [this](const FCSRebindableBinding& B)
	{
		const FKey* Override = WorkingPrefs.KeyOverrides.Find(B.Id);
		return Override ? *Override : B.DefaultKey;
	};

	const FCSRebindableBinding* Target = Bindings.FindByPredicate([Id](const FCSRebindableBinding& B) { return B.Id == Id; });
	if (!Target)
	{
		return FReply::Handled();
	}
	const FKey OldKey = Current(*Target);

	auto Assign = [this](const FCSRebindableBinding& B, const FKey& NewKey)
	{
		if (NewKey == B.DefaultKey)
		{
			WorkingPrefs.KeyOverrides.Remove(B.Id);
		}
		else
		{
			WorkingPrefs.KeyOverrides.Add(B.Id, NewKey);
		}
	};

	// A key can drive only one action: swap with whoever had it.
	for (const FCSRebindableBinding& Other : Bindings)
	{
		if (Other.Id != Id && Current(Other) == Key)
		{
			Assign(Other, OldKey);
			StatusText = FText::Format(LOCTEXT("Swapped", "{0} was used by \"{1}\" - swapped to {2}."),
				Key.GetDisplayName(), Other.DisplayName, OldKey.GetDisplayName());
		}
	}
	Assign(*Target, Key);
	return FReply::Handled();
}

FReply SCSSettingsPanel::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Clicking anywhere else cancels a pending capture.
	CapturingBinding = NAME_None;
	return FReply::Unhandled();
}

FReply SCSSettingsPanel::OnApply()
{
	if (UCSSettingsSubsystem* Settings = GetSettings())
	{
		Settings->ApplyGraphics(WorkingGfx);
		Settings->SetPreferences(WorkingPrefs, /*bSave*/ true);
		StatusText = LOCTEXT("Applied", "Settings applied and saved.");
	}
	return FReply::Handled();
}

FReply SCSSettingsPanel::OnReset()
{
	WorkingPrefs = UCSSettingsSubsystem::DefaultPreferences();
	WorkingGfx = UCSSettingsSubsystem::DefaultGraphics();
	SyncSelectors();
	StatusText = LOCTEXT("ResetHint", "Defaults restored - press APPLY to keep them.");
	return FReply::Handled();
}

FReply SCSSettingsPanel::OnBack()
{
	CapturingBinding = NAME_None;
	OnClose.ExecuteIfBound();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
