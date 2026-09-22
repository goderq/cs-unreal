// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "UI/SCSLoginScreen.h"

#include "Account/CSAccountSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/CSUIStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CSLogin"

void SCSLoginScreen::Construct(const FArguments& InArgs)
{
	WorldContext = InArgs._WorldContext;
	OnSignedIn = InArgs._OnSignedIn;

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SImage).Image(CSUI::WhiteBrush()).ColorAndOpacity(CSUI::Backdrop)
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Fill)
		[
			SNew(SBox).WidthOverride(4.f)
			[
				SNew(SImage).Image(CSUI::WhiteBrush()).ColorAndOpacity(CSUI::Accent)
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(720.f)
			[
				SNew(SBorder)
				.BorderImage(CSUI::WhiteBrush())
				.BorderBackgroundColor(CSUI::Panel)
				.Padding(FMargin(48.f, 42.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(STextBlock).Text(LOCTEXT("TitleA", "CS ")).Font(CSUI::Font(44, true)).ColorAndOpacity(CSUI::Text)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(STextBlock).Text(LOCTEXT("TitleB", "FUSION")).Font(CSUI::Font(44, true)).ColorAndOpacity(CSUI::Accent)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 6.f, 0.f, 30.f)
					[
						SNew(STextBlock).Text(LOCTEXT("Subtitle", "Sign in with your Epic account to play"))
						.Font(CSUI::Font(15)).ColorAndOpacity(CSUI::TextDim)
					]

					// Status line: what is happening right now.
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock).Font(CSUI::Font(20, true))
						.Text(this, &SCSLoginScreen::GetStatusText)
						.ColorAndOpacity_Lambda([this]()
						{
							const UCSAccountSubsystem* Account = GetAccount();
							const ECSAccountState State = Account ? Account->GetState() : ECSAccountState::SignedOut;
							return FSlateColor(State == ECSAccountState::Ready ? CSUI::Money
								: (State == ECSAccountState::Failed || State == ECSAccountState::NotConfigured ? CSUI::Danger : CSUI::Text));
						})
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 10.f, 0.f, 26.f)
					[
						SNew(SBox).WidthOverride(560.f)
						[
							SNew(STextBlock).Font(CSUI::Font(14)).ColorAndOpacity(CSUI::TextDim)
							.Text(this, &SCSLoginScreen::GetDetailText)
							.Justification(ETextJustify::Center)
							.AutoWrapText(true)
						]
					]

					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(SBox).WidthOverride(420.f).HeightOverride(56.f)
						[
							CSUI::MakeButton(TAttribute<FText>(this, &SCSLoginScreen::GetButtonText),
								FOnClicked::CreateSP(this, &SCSLoginScreen::OnSignInClicked),
								CSUI::EButtonKind::Primary,
								TAttribute<bool>(this, &SCSLoginScreen::IsButtonEnabled), 20)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 10.f, 0.f, 0.f)
					[
						SNew(SBox).WidthOverride(420.f).HeightOverride(44.f)
						[
							CSUI::MakeButton(LOCTEXT("Quit", "QUIT"), FOnClicked::CreateSP(this, &SCSLoginScreen::OnQuitClicked),
								CSUI::EButtonKind::Normal, true, 16)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 28.f, 0.f, 0.f)
					[
						SNew(SBox).WidthOverride(560.f)
						[
							SNew(STextBlock).Font(CSUI::Font(12)).ColorAndOpacity(CSUI::TextDim)
							.Text(LOCTEXT("Privacy", "Epic handles the sign-in; the game never sees your password. Stored for you: your Epic account id, a nickname and your match stats."))
							.Justification(ETextJustify::Center)
							.AutoWrapText(true)
						]
					]
				]
			]
		]
	];

	// Returning players go straight through: the Epic SDK reuses its session.
	if (UCSAccountSubsystem* Account = GetAccount())
	{
		if (Account->GetState() == ECSAccountState::SignedOut)
		{
			Account->SignIn();
		}
	}
}

SCSLoginScreen::~SCSLoginScreen() = default;

UCSAccountSubsystem* SCSLoginScreen::GetAccount() const
{
	return WorldContext.IsValid() ? UCSAccountSubsystem::Get(WorldContext.Get()) : nullptr;
}

FText SCSLoginScreen::GetStatusText() const
{
	const UCSAccountSubsystem* Account = GetAccount();
	switch (Account ? Account->GetState() : ECSAccountState::SignedOut)
	{
	case ECSAccountState::SigningIn:		return LOCTEXT("StatusSigningIn", "SIGNING IN...");
	case ECSAccountState::Ready:			return FText::Format(LOCTEXT("StatusReady", "WELCOME, {0}"), FText::FromString(Account->GetNickname().ToUpper()));
	case ECSAccountState::Failed:			return LOCTEXT("StatusFailed", "SIGN-IN FAILED");
	case ECSAccountState::NotConfigured:	return LOCTEXT("StatusNoConfig", "ACCOUNTS ARE NOT SET UP");
	default:								return LOCTEXT("StatusIdle", "NOT SIGNED IN");
	}
}

FText SCSLoginScreen::GetDetailText() const
{
	const UCSAccountSubsystem* Account = GetAccount();
	const ECSAccountState State = Account ? Account->GetState() : ECSAccountState::SignedOut;
	if (State == ECSAccountState::NotConfigured)
	{
		return LOCTEXT("DetailNoConfig", "This build has no Config/Backend.ini, so it cannot reach Epic or the account server. Copy Config/Backend.ini.example, fill it in and restart - the steps are in docs/ACCOUNTS.md.");
	}
	if (State == ECSAccountState::Failed)
	{
		return FText::Format(LOCTEXT("DetailFailed", "{0}"), FText::FromString(Account->GetLastError()));
	}
	if (State == ECSAccountState::SigningIn)
	{
		return LOCTEXT("DetailSigningIn", "Finish the login in the Epic window. If nothing opened, check that the Epic overlay is allowed, or use the browser window that appeared.");
	}
	if (State == ECSAccountState::Ready)
	{
		const FCSAccountStats& Stats = Account->GetStats();
		return FText::Format(LOCTEXT("DetailReady", "{0} matches, {1} kills, {2} deaths. Loading the menu..."),
			FText::AsNumber(Stats.Matches), FText::AsNumber(Stats.Kills), FText::AsNumber(Stats.Deaths));
	}
	return LOCTEXT("DetailIdle", "An Epic account is free and takes a minute to create.");
}

FText SCSLoginScreen::GetButtonText() const
{
	const UCSAccountSubsystem* Account = GetAccount();
	switch (Account ? Account->GetState() : ECSAccountState::SignedOut)
	{
	case ECSAccountState::SigningIn:	return LOCTEXT("BtnWait", "WAITING FOR EPIC...");
	case ECSAccountState::Failed:		return LOCTEXT("BtnRetry", "TRY AGAIN");
	case ECSAccountState::Ready:		return LOCTEXT("BtnReady", "CONTINUE");
	default:							return LOCTEXT("BtnSignIn", "SIGN IN WITH EPIC");
	}
}

bool SCSLoginScreen::IsButtonEnabled() const
{
	const UCSAccountSubsystem* Account = GetAccount();
	const ECSAccountState State = Account ? Account->GetState() : ECSAccountState::SignedOut;
	return State != ECSAccountState::SigningIn && State != ECSAccountState::NotConfigured;
}

FReply SCSLoginScreen::OnSignInClicked()
{
	if (UCSAccountSubsystem* Account = GetAccount())
	{
		if (Account->IsReady())
		{
			OnSignedIn.ExecuteIfBound();
		}
		else
		{
			Account->SignIn();
		}
	}
	return FReply::Handled();
}

FReply SCSLoginScreen::OnQuitClicked()
{
	UWorld* World = WorldContext.IsValid() ? WorldContext->GetWorld() : nullptr;
	UKismetSystemLibrary::QuitGame(World, World ? World->GetFirstPlayerController() : nullptr, EQuitPreference::Quit, false);
	return FReply::Handled();
}

void SCSLoginScreen::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	SpinnerPhase += InDeltaTime;

	// Hand over to the menu a moment after the account is ready, so the
	// welcome line is actually readable.
	const UCSAccountSubsystem* Account = GetAccount();
	if (!bNotified && Account && Account->IsReady())
	{
		if (ReadyAt <= 0.0)
		{
			ReadyAt = InCurrentTime;
		}
		else if (InCurrentTime - ReadyAt > 0.9)
		{
			bNotified = true;
			OnSignedIn.ExecuteIfBound();
		}
	}
}

FReply SCSLoginScreen::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Enter || InKeyEvent.GetKey() == EKeys::SpaceBar)
	{
		return OnSignInClicked();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
