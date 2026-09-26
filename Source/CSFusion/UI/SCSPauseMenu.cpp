// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "UI/SCSPauseMenu.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Multiplayer/CSSessionSubsystem.h"
#include "UI/CSUIStyle.h"
#include "UI/SCSSettingsPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CSPause"

namespace
{
	enum EPausePage : int32 { Main = 0, ConfirmLeave = 1, SettingsPage = 2 };
}

void SCSPauseMenu::Construct(const FArguments& InArgs)
{
	WorldContext = InArgs._WorldContext;
	OnResume = InArgs._OnResume;
	OnLeaveMatch = InArgs._OnLeaveMatch;

	auto Page = [](TSharedRef<SWidget> Content, float Width) -> TSharedRef<SWidget>
	{
		return SNew(SBox).WidthOverride(Width).VAlign(VAlign_Center)
			[
				SNew(SBorder)
				.BorderImage(CSUI::WhiteBrush())
				.BorderBackgroundColor(CSUI::Panel)
				.Padding(FMargin(36.f, 32.f))
				[
					Content
				]
			];
	};

	// Phase 6: the pages ease in (menu opened, settings, leave confirmation).
	const TSharedRef<SWidget> MainPage = Page(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				CSUI::MakeHeader(LOCTEXT("Paused", "MENU"), LOCTEXT("NoPause", "The match keeps running - you can still be hit."))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 18.f)
			[
				SNew(STextBlock).Text(this, &SCSPauseMenu::GetSessionLine).Font(CSUI::Font(14)).ColorAndOpacity(CSUI::TextDim)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
			[
				CSUI::MakeButton(LOCTEXT("Resume", "RESUME"),
					FOnClicked::CreateLambda([this]() { OnResume.ExecuteIfBound(); return FReply::Handled(); }), CSUI::EButtonKind::Primary)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
			[
				CSUI::MakeButton(LOCTEXT("Settings", "SETTINGS"),
					FOnClicked::CreateLambda([this]() { ShowSettings(); return FReply::Handled(); }))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
			[
				CSUI::MakeButton(LOCTEXT("Leave", "LEAVE MATCH"),
					FOnClicked::CreateLambda([this]() { Switcher->SetActivePage(ConfirmLeave); return FReply::Handled(); }),
					CSUI::EButtonKind::Danger)
			],
		560.f);
	const TSharedRef<SWidget> LeavePage = Page(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				CSUI::MakeHeader(LOCTEXT("LeaveTitle", "LEAVE MATCH?"),
					LOCTEXT("LeaveBody", "Everything in your inventory drops where you stand, and other players can take it."))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
			[
				CSUI::MakeButton(LOCTEXT("LeaveConfirm", "LEAVE"),
					FOnClicked::CreateLambda([this]() { OnLeaveMatch.ExecuteIfBound(); return FReply::Handled(); }),
					CSUI::EButtonKind::Danger)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
			[
				CSUI::MakeButton(LOCTEXT("Stay", "STAY"),
					FOnClicked::CreateLambda([this]() { ResetToMain(); return FReply::Handled(); }))
			],
		560.f);
	const TSharedRef<SWidget> SettingsPageWidget = SNew(SBox).WidthOverride(900.f).HeightOverride(820.f)
		[
			SNew(SBorder)
			.BorderImage(CSUI::WhiteBrush())
			.BorderBackgroundColor(CSUI::Panel)
			.Padding(FMargin(36.f, 32.f))
			[
				SAssignNew(Settings, SCSSettingsPanel)
				.WorldContext(WorldContext)
				.OnClose_Lambda([this]() { ResetToMain(); })
			]
		];

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SImage).Image(CSUI::WhiteBrush()).ColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.55f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(FMargin(16.f, 40.f))
		[
			SAssignNew(Switcher, SCSAnimatedSwitcher)
		]
	];
	Switcher->AddPage(MainPage);
	Switcher->AddPage(LeavePage);
	Switcher->AddPage(SettingsPageWidget);
}

FText SCSPauseMenu::GetSessionLine() const
{
	const UWorld* World = WorldContext.IsValid() ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	const UCSSessionSubsystem* Session = GI ? GI->GetSubsystem<UCSSessionSubsystem>() : nullptr;
	if (!Session || !Session->IsInRoom())
	{
		return LOCTEXT("Offline", "Offline practice");
	}
	return FText::Format(LOCTEXT("SessionLine", "Room {0}   {1} player(s)   {2} ms{3}"),
		FText::FromString(Session->GetRoomName()), FText::AsNumber(Session->GetPlayerCount()),
		FText::AsNumber(Session->GetRttMs()),
		Session->IsMasterClient() ? LOCTEXT("Host", "   host") : FText::GetEmpty());
}

void SCSPauseMenu::ResetToMain()
{
	if (Switcher->GetActivePage() == Main)
	{
		Switcher->PlayIntro();   // the menu was just opened
	}
	Switcher->SetActivePage(Main);
}

void SCSPauseMenu::ShowSettings()
{
	Settings->Refresh();
	Switcher->SetActivePage(SettingsPage);
}

FReply SCSPauseMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		if (Switcher->GetActivePage() != Main)
		{
			ResetToMain();
		}
		else
		{
			OnResume.ExecuteIfBound();
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
