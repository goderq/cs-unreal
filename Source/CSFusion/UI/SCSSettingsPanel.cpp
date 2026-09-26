// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "UI/SCSSettingsPanel.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Graphics/CSGraphics.h"
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
	auto Percent = [](float V) { return FText::AsPercent(V); };
	auto Pixels = [](float V) { return FText::Format(LOCTEXT("Px", "{0} px"), FText::AsNumber(FMath::RoundToInt(V))); };

	// --- Pages (phase 6: tabs instead of one long list) ------------------------
	TSharedRef<SVerticalBox> Video = SNew(SVerticalBox);
	Video->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("Resolution", "Resolution"),
		SAssignNew(ResolutionSelector, SCSSelector).Options(ResolutionOptions)
		.OnSelectionChanged_Lambda([this](int32 i) { if (Resolutions.IsValidIndex(i)) { WorkingGfx.Resolution = Resolutions[i]; } })) ];
	Video->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("DisplayMode", "Display mode"),
		SAssignNew(WindowModeSelector, SCSSelector)
		.Options({ LOCTEXT("Fullscreen", "Fullscreen"), LOCTEXT("Borderless", "Borderless window"), LOCTEXT("Windowed", "Windowed") })
		.OnSelectionChanged_Lambda([this](int32 i) { WorkingGfx.WindowMode = GWindowModes[FMath::Clamp(i, 0, 2)]; })) ];
	Video->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("FrameLimit", "FPS limit"),
		SAssignNew(FrameLimitSelector, SCSSelector).Options(FrameOptions)
		.OnSelectionChanged_Lambda([this](int32 i) { WorkingGfx.FrameRateLimit = UCSSettingsSubsystem::GetFrameRateChoices()[i]; })) ];
	Video->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("VSync", "VSync"),
		SAssignNew(VSyncSelector, SCSSelector).Options(OffOn)
		.OnSelectionChanged_Lambda([this](int32 i) { WorkingGfx.bVSync = i == 1; })) ];
	Video->AddSlot().AutoHeight()[ MakeGraphicsRows() ];
	Video->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("ShowFps", "Show FPS"),
		SAssignNew(ShowFpsSelector, SCSSelector).Options(OffOn)
		.OnSelectionChanged_Lambda([this](int32 i) { WorkingPrefs.bShowFps = i == 1; })) ];
	Video->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("NetStats", "Show ping and jitter"),
		SAssignNew(NetStatsSelector, SCSSelector).Options(OffOn)
		.OnSelectionChanged_Lambda([this](int32 i) { WorkingPrefs.bShowNetStats = i == 1; })) ];

	TSharedRef<SVerticalBox> Controls = SNew(SVerticalBox);
	Controls->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("Sensitivity", "Mouse sensitivity"),
		MakeSlider(0.05f, 5.f, [this]() { return WorkingPrefs.MouseSensitivity; }, [this](float V) { WorkingPrefs.MouseSensitivity = V; },
			[](float V) { return FText::AsNumber(V, &FNumberFormattingOptions().SetMinimumFractionalDigits(2).SetMaximumFractionalDigits(2)); })) ];
	Controls->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("AimSensitivity", "Aiming sensitivity"),
		MakeSlider(0.2f, 2.f, [this]() { return WorkingPrefs.AimSensitivity; }, [this](float V) { WorkingPrefs.AimSensitivity = V; }, Percent)) ];
	Controls->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("AimMode", "Aim"),
		SAssignNew(AimToggleSelector, SCSSelector).Options({ LOCTEXT("Hold", "Hold"), LOCTEXT("Toggle", "Toggle") })
		.OnSelectionChanged_Lambda([this](int32 i) { WorkingPrefs.bToggleAim = i == 1; })) ];
	Controls->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("InvertY", "Invert mouse Y"),
		SAssignNew(InvertSelector, SCSSelector).Options(OffOn)
		.OnSelectionChanged_Lambda([this](int32 i) { WorkingPrefs.bInvertY = i == 1; })) ];
	Controls->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("FOV", "Field of view"),
		MakeSlider(70.f, 120.f, [this]() { return WorkingPrefs.FieldOfView; }, [this](float V) { WorkingPrefs.FieldOfView = FMath::RoundToFloat(V); },
			[](float V) { return FText::Format(LOCTEXT("Deg", "{0} deg"), FText::AsNumber(FMath::RoundToInt(V))); })) ];

	TArray<FText> ColorNames;
	for (const TCHAR* Name : CSUI::CrosshairColorNames)
	{
		ColorNames.Add(FText::FromString(Name));
	}
	TSharedRef<SVerticalBox> Crosshair = SNew(SVerticalBox);
	Crosshair->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 0.f, 0.f, 10.f)
	[
		SNew(SBox).WidthOverride(240.f).HeightOverride(140.f)
		[
			SNew(SCSCrosshairPreview)
			.Source([this](int32& Style, int32& Color, float& Size, float& Gap, float& Thick, bool& bOutline)
			{
				Style = WorkingPrefs.CrosshairStyle; Color = WorkingPrefs.CrosshairColor; Size = WorkingPrefs.CrosshairSize;
				Gap = WorkingPrefs.CrosshairGap; Thick = WorkingPrefs.CrosshairThickness; bOutline = WorkingPrefs.bCrosshairOutline;
			})
		]
	];
	Crosshair->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("CrossStyle", "Style"),
		SAssignNew(CrosshairStyleSelector, SCSSelector)
		.Options({ LOCTEXT("Cross", "Cross"), LOCTEXT("CrossDot", "Cross and dot"), LOCTEXT("Dot", "Dot"), LOCTEXT("Circle", "Circle") })
		.OnSelectionChanged_Lambda([this](int32 i) { WorkingPrefs.CrosshairStyle = i; })) ];
	Crosshair->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("CrossColor", "Colour"),
		SAssignNew(CrosshairColorSelector, SCSSelector).Options(ColorNames)
		.OnSelectionChanged_Lambda([this](int32 i) { WorkingPrefs.CrosshairColor = i; })) ];
	Crosshair->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("CrossSize", "Size"),
		MakeSlider(2.f, 20.f, [this]() { return WorkingPrefs.CrosshairSize; }, [this](float V) { WorkingPrefs.CrosshairSize = FMath::RoundToFloat(V); }, Pixels)) ];
	Crosshair->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("CrossGap", "Gap"),
		MakeSlider(0.f, 16.f, [this]() { return WorkingPrefs.CrosshairGap; }, [this](float V) { WorkingPrefs.CrosshairGap = FMath::RoundToFloat(V); }, Pixels)) ];
	Crosshair->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("CrossThick", "Thickness"),
		MakeSlider(1.f, 5.f, [this]() { return WorkingPrefs.CrosshairThickness; }, [this](float V) { WorkingPrefs.CrosshairThickness = FMath::RoundToFloat(V); }, Pixels)) ];
	Crosshair->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("CrossOutline", "Outline"),
		SAssignNew(CrosshairOutlineSelector, SCSSelector).Options(OffOn)
		.OnSelectionChanged_Lambda([this](int32 i) { WorkingPrefs.bCrosshairOutline = i == 1; })) ];
	Crosshair->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("CrossDynamic", "Opens with weapon spread"),
		SAssignNew(CrosshairDynamicSelector, SCSSelector).Options(OffOn)
		.OnSelectionChanged_Lambda([this](int32 i) { WorkingPrefs.bCrosshairDynamic = i == 1; })) ];

	TSharedRef<SVerticalBox> Audio = SNew(SVerticalBox);
	Audio->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("Master", "Master volume"),
		MakeSlider(0.f, 1.f, [this]() { return WorkingPrefs.MasterVolume; }, [this](float V) { WorkingPrefs.MasterVolume = V; }, Percent)) ];
	Audio->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("Music", "Music volume"),
		MakeSlider(0.f, 1.f, [this]() { return WorkingPrefs.MusicVolume; }, [this](float V) { WorkingPrefs.MusicVolume = V; }, Percent)) ];
	Audio->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("Effects", "Effects volume"),
		MakeSlider(0.f, 1.f, [this]() { return WorkingPrefs.EffectsVolume; }, [this](float V) { WorkingPrefs.EffectsVolume = V; }, Percent)) ];

	TSharedRef<SVerticalBox> Access = SNew(SVerticalBox);
	Access->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("ColorVision", "Colour vision correction"),
		SAssignNew(ColorVisionSelector, SCSSelector)
		.Options({ LOCTEXT("CVOff", "Off"), LOCTEXT("Protan", "Protanopia (red)"), LOCTEXT("Deutan", "Deuteranopia (green)"), LOCTEXT("Tritan", "Tritanopia (blue)") })
		.OnSelectionChanged_Lambda([this](int32 i) { WorkingPrefs.ColorVision = i; })) ];
	Access->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("UIScale", "Interface size"),
		MakeSlider(0.8f, 1.3f, [this]() { return WorkingPrefs.UIScale; }, [this](float V) { WorkingPrefs.UIScale = FMath::RoundToFloat(V * 20.f) / 20.f; }, Percent)) ];
	Access->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("CameraMotion", "Camera and weapon motion"),
		MakeSlider(0.f, 1.f, [this]() { return WorkingPrefs.CameraMotion; }, [this](float V) { WorkingPrefs.CameraMotion = V; }, Percent)) ];
	Access->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("MotionBlur", "Motion blur"),
		SAssignNew(MotionBlurSelector, SCSSelector).Options(OffOn)
		.OnSelectionChanged_Lambda([this](int32 i) { WorkingPrefs.bMotionBlur = i == 1; })) ];
	Access->AddSlot().AutoHeight()[ CSUI::MakeRow(LOCTEXT("ReduceFlash", "Softer flashbang white-out"),
		SAssignNew(ReduceFlashSelector, SCSSelector).Options(OffOn)
		.OnSelectionChanged_Lambda([this](int32 i) { WorkingPrefs.bReduceFlash = i == 1; })) ];

	TSharedRef<SVerticalBox> Keys = SNew(SVerticalBox);
	Keys->AddSlot().AutoHeight()[ SAssignNew(KeyRows, SVerticalBox) ];

	const FText TabNames[] = { LOCTEXT("TabVideo", "VIDEO"), LOCTEXT("TabControls", "CONTROLS"), LOCTEXT("TabCrosshair", "CROSSHAIR"),
		LOCTEXT("TabAudio", "AUDIO"), LOCTEXT("TabAccess", "ACCESSIBILITY"), LOCTEXT("TabKeys", "KEY BINDINGS") };
	TSharedRef<SWidget> Pages[] = { Video, Controls, Crosshair, Audio, Access, Keys };

	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	SAssignNew(PageSwitcher, SCSAnimatedSwitcher);
	for (int32 t = 0; t < UE_ARRAY_COUNT(TabNames); ++t)
	{
		PageSwitcher->AddPage(SNew(SScrollBox) + SScrollBox::Slot().Padding(FMargin(0.f, 0.f, 14.f, 0.f))[ Pages[t] ]);
		Tabs->AddSlot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton).IsFocusable(false)
				.ButtonStyle(&CSUI::ButtonStyle(CSUI::EButtonKind::Nav))
				.ContentPadding(FMargin(12.f, 8.f))
				.OnClicked_Lambda([this, t]() { PageSwitcher->SetActivePage(t); return FReply::Handled(); })
				[
					SNew(STextBlock).Text(TabNames[t]).Font(CSUI::Font(14, true))
					.ColorAndOpacity_Lambda([this, t]() { return FSlateColor(PageSwitcher->GetActivePage() == t ? CSUI::Accent : CSUI::TextDim); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).HeightOverride(3.f)
				[
					SNew(SBorder).BorderImage(CSUI::WhiteBrush())
					.BorderBackgroundColor_Lambda([this, t]() { return FSlateColor(PageSwitcher->GetActivePage() == t ? CSUI::Accent : FLinearColor::Transparent); })
				]
			]
		];
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			CSUI::MakeHeader(LOCTEXT("Title", "SETTINGS"), LOCTEXT("Subtitle", "Saved on this PC only. Nothing here affects gameplay state."))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
		[
			Tabs
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			PageSwitcher.ToSharedRef()
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

void SCSSettingsPanel::ShowTab(int32 Index)
{
	if (PageSwitcher.IsValid())
	{
		PageSwitcher->SetActivePage(FMath::Clamp(Index, 0, 5));
	}
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

	PresetSelector->SetSelectedIndex(WorkingGfx.Preset);
	for (int32 g = 0; g < static_cast<int32>(ECSGraphicsGroup::Count); ++g)
	{
		GroupSelectors[g]->SetSelectedIndex(WorkingGfx.Groups[g]);
	}
	const int32 UpscalerIndex = UpscalerChoices.IndexOfByKey(static_cast<int32>(CSGraphics::Effective(static_cast<ECSUpscaler>(WorkingGfx.Upscaler))));
	UpscalerSelector->SetSelectedIndex(UpscalerIndex == INDEX_NONE ? 0 : UpscalerIndex);
	RenderScaleSelector->SetSelectedIndex(WorkingGfx.RenderScale);
	RayTracingSelector->SetSelectedIndex(WorkingGfx.bRayTracing && CSGraphics::GetCaps().bRayTracing ? 1 : 0);
	VSyncSelector->SetSelectedIndex(WorkingGfx.bVSync ? 1 : 0);
	InvertSelector->SetSelectedIndex(WorkingPrefs.bInvertY ? 1 : 0);
	ShowFpsSelector->SetSelectedIndex(WorkingPrefs.bShowFps ? 1 : 0);
	NetStatsSelector->SetSelectedIndex(WorkingPrefs.bShowNetStats ? 1 : 0);
	AimToggleSelector->SetSelectedIndex(WorkingPrefs.bToggleAim ? 1 : 0);
	CrosshairStyleSelector->SetSelectedIndex(WorkingPrefs.CrosshairStyle);
	CrosshairColorSelector->SetSelectedIndex(WorkingPrefs.CrosshairColor);
	CrosshairOutlineSelector->SetSelectedIndex(WorkingPrefs.bCrosshairOutline ? 1 : 0);
	CrosshairDynamicSelector->SetSelectedIndex(WorkingPrefs.bCrosshairDynamic ? 1 : 0);
	ColorVisionSelector->SetSelectedIndex(WorkingPrefs.ColorVision);
	MotionBlurSelector->SetSelectedIndex(WorkingPrefs.bMotionBlur ? 1 : 0);
	ReduceFlashSelector->SetSelectedIndex(WorkingPrefs.bReduceFlash ? 1 : 0);
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

TSharedRef<SWidget> SCSSettingsPanel::MakeGraphicsRows()
{
	const FCSGraphicsCaps& Caps = CSGraphics::GetCaps();
	const TArray<FText> Levels = { LOCTEXT("Low", "Low"), LOCTEXT("Medium", "Medium"), LOCTEXT("High", "High"),
		LOCTEXT("Epic", "Epic"), LOCTEXT("Cinematic", "Cinematic") };
	TArray<FText> Presets = Levels;
	Presets.Add(LOCTEXT("Custom", "Custom"));

	// DLSS appears only where it runs (AUDIT K5); TSR and TAA always.
	UpscalerChoices.Reset();
	TArray<FText> Upscalers;
	if (Caps.bDLSS)
	{
		UpscalerChoices.Add(static_cast<int32>(ECSUpscaler::DLSS));
		Upscalers.Add(LOCTEXT("DLSS", "NVIDIA DLSS"));
	}
	UpscalerChoices.Add(static_cast<int32>(ECSUpscaler::TSR));
	Upscalers.Add(LOCTEXT("TSR", "TSR (Unreal)"));
	UpscalerChoices.Add(static_cast<int32>(ECSUpscaler::TAA));
	Upscalers.Add(LOCTEXT("TAA", "TAA"));

	TArray<FText> Scales;
	const FText ScaleNames[] = { LOCTEXT("Native", "Native"), LOCTEXT("ScaleQuality", "Quality"), LOCTEXT("Balanced", "Balanced"),
		LOCTEXT("Performance", "Performance"), LOCTEXT("UltraPerformance", "Ultra Performance") };
	for (int32 i = 0; i < 5; ++i)
	{
		Scales.Add(FText::Format(LOCTEXT("ScaleFmt", "{0} ({1}%)"), ScaleNames[i],
			FText::AsNumber(FMath::RoundToInt(CSGraphics::ScreenPercentage(static_cast<ECSRenderScale>(i))))));
	}

	const FText GroupNames[] = { LOCTEXT("ViewDistance", "View distance"), LOCTEXT("AntiAliasing", "Anti-aliasing"),
		LOCTEXT("Shadows", "Shadows"), LOCTEXT("GI", "Global illumination"), LOCTEXT("Reflections", "Reflections"),
		LOCTEXT("PostProcess", "Post-processing"), LOCTEXT("Textures", "Textures"), LOCTEXT("Effects", "Effects"),
		LOCTEXT("Shading", "Shading") };

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight()
	[
		CSUI::MakeRow(LOCTEXT("Preset", "Graphics preset"),
			SAssignNew(PresetSelector, SCSSelector).Options(Presets)
			.OnSelectionChanged_Lambda([this](int32 i)
			{
				UCSSettingsSubsystem::SetPreset(WorkingGfx, i);
				SyncSelectors();
			}))
	];
	for (int32 g = 0; g < static_cast<int32>(ECSGraphicsGroup::Count); ++g)
	{
		Box->AddSlot().AutoHeight()
		[
			CSUI::MakeRow(GroupNames[g],
				SAssignNew(GroupSelectors[g], SCSSelector).Options(Levels)
				.OnSelectionChanged_Lambda([this, g](int32 i)
				{
					WorkingGfx.Groups[g] = i;
					WorkingGfx.Preset = FCSGraphicsSettings::CustomPreset;
					PresetSelector->SetSelectedIndex(WorkingGfx.Preset);
				}))
		];
	}
	Box->AddSlot().AutoHeight()
	[
		CSUI::MakeRow(LOCTEXT("Upscaler", "Anti-aliasing / upscaler"),
			SAssignNew(UpscalerSelector, SCSSelector).Options(Upscalers)
			.OnSelectionChanged_Lambda([this](int32 i) { if (UpscalerChoices.IsValidIndex(i)) { WorkingGfx.Upscaler = UpscalerChoices[i]; } }))
	];
	Box->AddSlot().AutoHeight()
	[
		CSUI::MakeRow(LOCTEXT("RenderScale", "Render resolution"),
			SAssignNew(RenderScaleSelector, SCSSelector).Options(Scales)
			.OnSelectionChanged_Lambda([this](int32 i) { WorkingGfx.RenderScale = i; }))
	];
	Box->AddSlot().AutoHeight()
	[
		CSUI::MakeRow(LOCTEXT("RayTracing", "Hardware ray tracing"),
			SAssignNew(RayTracingSelector, SCSSelector)
			.Options(Caps.bRayTracing ? TArray<FText>{ LOCTEXT("Off", "Off"), LOCTEXT("On", "On") }
				: TArray<FText>{ LOCTEXT("RTUnsupported", "Not supported on this PC") })
			.OnSelectionChanged_Lambda([this](int32 i) { WorkingGfx.bRayTracing = i == 1; }))
	];
	Box->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 8.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("GpuInfo", "{0}. DLSS: {1}."),
				FText::FromString(FString::Printf(TEXT("%s, %lld MB"), *Caps.Adapter, Caps.VideoMemoryMB)), FText::FromString(Caps.DLSSStatus)))
			.Font(CSUI::Font(12))
			.ColorAndOpacity(CSUI::TextDim)
			.AutoWrapText(true)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(12.f, 0.f, 0.f, 0.f)
		[
			CSUI::MakeButton(LOCTEXT("AutoDetect", "AUTO-DETECT"), FOnClicked::CreateSP(this, &SCSSettingsPanel::OnAutoDetect), CSUI::EButtonKind::Normal, true, 14)
		]
	];
	return Box;
}

FReply SCSSettingsPanel::OnAutoDetect()
{
	if (UCSSettingsSubsystem* Settings = GetSettings())
	{
		Settings->AutoDetectGraphics();
		WorkingGfx = Settings->GetGraphics();
		SyncSelectors();
		StatusText = LOCTEXT("Detected", "Graphics picked for this PC and applied.");
	}
	return FReply::Handled();
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
