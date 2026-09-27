// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "UI/SCSLoginScreen.h"

#include "Account/CSAccountSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/CSUIStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CSLogin"

namespace
{
	// Switcher slots, in the order Construct adds them.
	enum ESlotIndex : int32 { SlotMain, SlotSignIn, SlotRegister, SlotRecoverRequest, SlotRecoverCode, SlotChoice, SlotLink };
}

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
				.Padding(FMargin(48.f, 38.f))
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
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 18.f, 0.f, 0.f)
					[
						SNew(SWidgetSwitcher)
						.WidgetIndex(this, &SCSLoginScreen::GetSwitcherIndex)
						+ SWidgetSwitcher::Slot()[ BuildMainPage() ]
						+ SWidgetSwitcher::Slot()[ BuildSignInPage() ]
						+ SWidgetSwitcher::Slot()[ BuildRegisterPage() ]
						+ SWidgetSwitcher::Slot()[ BuildRecoverRequestPage() ]
						+ SWidgetSwitcher::Slot()[ BuildRecoverCodePage() ]
						+ SWidgetSwitcher::Slot()[ BuildChoicePage() ]
						+ SWidgetSwitcher::Slot()[ BuildLinkPage() ]
					]
				]
			]
		]
	];

	// The silent attempt with the remembered session starts with the game
	// (UCSAccountSubsystem::Initialize), not here: after "sign out" this screen
	// must not sign the old account straight back in. The Epic window opens
	// only when the player presses the button - never on its own.
}

SCSLoginScreen::~SCSLoginScreen() = default;

UCSAccountSubsystem* SCSLoginScreen::GetAccount() const
{
	return WorldContext.IsValid() ? UCSAccountSubsystem::Get(WorldContext.Get()) : nullptr;
}

// ---------------------------------------------------------------------------
// Building blocks
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SCSLoginScreen::MakeWideButton(const TAttribute<FText>& Label, FOnClicked OnClicked, bool bPrimary, TAttribute<bool> Enabled, float Height)
{
	return SNew(SBox).WidthOverride(440.f).HeightOverride(Height).HAlign(HAlign_Fill)
	[
		CSUI::MakeButton(Label, OnClicked, bPrimary ? CSUI::EButtonKind::Primary : CSUI::EButtonKind::Normal, Enabled, bPrimary ? 19 : 15)
	];
}

TSharedRef<SWidget> SCSLoginScreen::MakeField(const FText& Label, TSharedPtr<SEditableTextBox>& OutBox, const FText& Hint, bool bPassword, bool bSubmitOnEnter)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
		[
			SNew(STextBlock).Text(Label).Font(CSUI::Font(12, true)).ColorAndOpacity(CSUI::TextDim)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).WidthOverride(440.f).HeightOverride(40.f)
			[
				SAssignNew(OutBox, SEditableTextBox)
				.Font(CSUI::Font(16))
				.HintText(Hint)
				.IsPassword(bPassword)
				.IsEnabled(this, &SCSLoginScreen::IsFormEnabled)
				.OnTextCommitted_Lambda([this, bSubmitOnEnter](const FText&, ETextCommit::Type Commit)
				{
					if (bSubmitOnEnter && Commit == ETextCommit::OnEnter)
					{
						OnSubmit();
					}
				})
			]
		];
}

TSharedRef<SWidget> SCSLoginScreen::MakeFormMessage()
{
	return SNew(SBox).WidthOverride(440.f)
	[
		SNew(STextBlock).Font(CSUI::Font(14)).AutoWrapText(true).Justification(ETextJustify::Center)
		.Text_Lambda([this]()
		{
			const UCSAccountSubsystem* Account = GetAccount();
			if (Account && Account->IsBusy())
			{
				return LOCTEXT("Busy", "Please wait...");
			}
			return FText::FromString(FormMessage);
		})
		.ColorAndOpacity_Lambda([this]() { return FSlateColor(bMessageIsError ? CSUI::Danger : CSUI::Money); })
	];
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SCSLoginScreen::BuildMainPage()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 22.f)
		[
			SNew(STextBlock).Text(LOCTEXT("Subtitle", "Sign in to play")).Font(CSUI::Font(15)).ColorAndOpacity(CSUI::TextDim)
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
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 10.f, 0.f, 24.f)
		[
			SNew(SBox).WidthOverride(560.f)
			[
				SNew(STextBlock).Font(CSUI::Font(14)).ColorAndOpacity(CSUI::TextDim)
				.Text(this, &SCSLoginScreen::GetDetailText)
				.Justification(ETextJustify::Center)
				.AutoWrapText(true)
			]
		]
		// The two ways in (hidden while the remembered session is being tried).
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(SVerticalBox)
			.Visibility_Lambda([this]()
			{
				const UCSAccountSubsystem* Account = GetAccount();
				const ECSAccountState State = Account ? Account->GetState() : ECSAccountState::SignedOut;
				return State == ECSAccountState::CheckingSession || State == ECSAccountState::NotConfigured || State == ECSAccountState::Ready
					? EVisibility::Collapsed : EVisibility::Visible;
			})
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeWideButton(LOCTEXT("BtnEmail", "LOGIN WITH EMAIL"), FOnClicked::CreateSP(this, &SCSLoginScreen::OnEmailClicked),
					true, TAttribute<bool>(this, &SCSLoginScreen::IsMainEnabled), 56.f)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
			[
				MakeWideButton(TAttribute<FText>::CreateLambda([this]()
				{
					const UCSAccountSubsystem* Account = GetAccount();
					return Account && Account->GetState() == ECSAccountState::SigningIn
						? LOCTEXT("BtnWait", "WAITING...") : LOCTEXT("BtnEpic", "LOGIN WITH EPIC GAMES");
				}), FOnClicked::CreateSP(this, &SCSLoginScreen::OnEpicClicked), true, TAttribute<bool>(this, &SCSLoginScreen::IsMainEnabled), 56.f)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 16.f, 0.f, 0.f)
		[
			SNew(SBox).Visibility_Lambda([this]() { return IsOfflineVisible() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				MakeWideButton(LOCTEXT("Offline", "PLAY OFFLINE (PRACTICE, NO STATS)"),
					FOnClicked::CreateSP(this, &SCSLoginScreen::OnOfflineClicked), false, true, 44.f)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 10.f, 0.f, 0.f)
		[
			MakeWideButton(LOCTEXT("Quit", "QUIT"), FOnClicked::CreateSP(this, &SCSLoginScreen::OnQuitClicked), false, true, 44.f)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 24.f, 0.f, 0.f)
		[
			SNew(SBox).WidthOverride(560.f)
			[
				SNew(STextBlock).Font(CSUI::Font(12)).ColorAndOpacity(CSUI::TextDim)
				.Text(LOCTEXT("Privacy", "Email and Epic Games open the same profile once linked. Passwords go only to the account service and are never stored by the game. Stored for you: your nickname, match stats, and the email or Epic account id you sign in with."))
				.Justification(ETextJustify::Center)
				.AutoWrapText(true)
			]
		];
}

TSharedRef<SWidget> SCSLoginScreen::BuildSignInPage()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 18.f)
		[
			SNew(STextBlock).Text(LOCTEXT("SignInTitle", "LOGIN WITH EMAIL")).Font(CSUI::Font(20, true)).ColorAndOpacity(CSUI::Text)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			MakeField(LOCTEXT("Email", "EMAIL"), SignInEmail, LOCTEXT("EmailHint", "you@example.com"), false, false)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 10.f, 0.f, 0.f)
		[
			MakeField(LOCTEXT("Password", "PASSWORD"), SignInPassword, FText::GetEmpty(), true, true)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 12.f, 0.f, 12.f)
		[
			MakeFormMessage()
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			MakeWideButton(LOCTEXT("BtnSignIn", "SIGN IN"), FOnClicked::CreateSP(this, &SCSLoginScreen::OnSubmit), true,
				TAttribute<bool>(this, &SCSLoginScreen::IsFormEnabled), 52.f)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 10.f, 0.f, 0.f)
		[
			MakeWideButton(LOCTEXT("BtnToRegister", "NO ACCOUNT? REGISTER"), FOnClicked::CreateLambda([this]()
			{
				RegEmail->SetText(SignInEmail->GetText());
				ShowPage(EPage::Register);
				return FReply::Handled();
			}), false, TAttribute<bool>(this, &SCSLoginScreen::IsFormEnabled), 42.f)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f, 0.f, 0.f)
		[
			MakeWideButton(LOCTEXT("BtnForgot", "FORGOT PASSWORD?"), FOnClicked::CreateLambda([this]()
			{
				RecoverEmail->SetText(SignInEmail->GetText());
				ShowPage(EPage::RecoverRequest);
				return FReply::Handled();
			}), false, TAttribute<bool>(this, &SCSLoginScreen::IsFormEnabled), 42.f)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f, 0.f, 0.f)
		[
			MakeWideButton(LOCTEXT("BtnBack", "BACK"), FOnClicked::CreateLambda([this]() { ShowPage(EPage::Main); return FReply::Handled(); }),
				false, TAttribute<bool>(this, &SCSLoginScreen::IsFormEnabled), 42.f)
		];
}

TSharedRef<SWidget> SCSLoginScreen::BuildRegisterPage()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 6.f)
		[
			SNew(STextBlock).Text(LOCTEXT("RegTitle", "CREATE AN ACCOUNT")).Font(CSUI::Font(20, true)).ColorAndOpacity(CSUI::Text)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 14.f)
		[
			SNew(SBox).WidthOverride(560.f)
			[
				SNew(STextBlock).Font(CSUI::Font(12)).ColorAndOpacity(CSUI::TextDim).AutoWrapText(true).Justification(ETextJustify::Center)
				.Text(LOCTEXT("RegNote", "The nickname is chosen once; later only the administration can change it. Already play with Epic Games? Sign in with Epic and add the email on the PROFILE page instead - then both open the same profile."))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			MakeField(LOCTEXT("Nickname", "NICKNAME"), RegNickname, LOCTEXT("NickHint", "3-20 letters, digits, _ or -"), false, false)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f, 0.f, 0.f)
		[
			MakeField(LOCTEXT("Email", "EMAIL"), RegEmail, LOCTEXT("EmailHint", "you@example.com"), false, false)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f, 0.f, 0.f)
		[
			MakeField(LOCTEXT("PasswordNew", "PASSWORD (AT LEAST 8 CHARACTERS)"), RegPassword, FText::GetEmpty(), true, false)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f, 0.f, 0.f)
		[
			MakeField(LOCTEXT("PasswordRepeat", "REPEAT THE PASSWORD"), RegPassword2, FText::GetEmpty(), true, true)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 12.f, 0.f, 12.f)
		[
			MakeFormMessage()
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			MakeWideButton(LOCTEXT("BtnRegister", "REGISTER AND PLAY"), FOnClicked::CreateSP(this, &SCSLoginScreen::OnSubmit), true,
				TAttribute<bool>(this, &SCSLoginScreen::IsFormEnabled), 52.f)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 10.f, 0.f, 0.f)
		[
			MakeWideButton(LOCTEXT("BtnHaveAccount", "I HAVE AN ACCOUNT - SIGN IN"), FOnClicked::CreateLambda([this]()
			{
				SignInEmail->SetText(RegEmail->GetText());
				ShowPage(EPage::EmailSignIn);
				return FReply::Handled();
			}), false, TAttribute<bool>(this, &SCSLoginScreen::IsFormEnabled), 42.f)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f, 0.f, 0.f)
		[
			MakeWideButton(LOCTEXT("BtnBack", "BACK"), FOnClicked::CreateLambda([this]() { ShowPage(EPage::Main); return FReply::Handled(); }),
				false, TAttribute<bool>(this, &SCSLoginScreen::IsFormEnabled), 42.f)
		];
}

TSharedRef<SWidget> SCSLoginScreen::BuildRecoverRequestPage()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 6.f)
		[
			SNew(STextBlock).Text(LOCTEXT("RecTitle", "RESET THE PASSWORD")).Font(CSUI::Font(20, true)).ColorAndOpacity(CSUI::Text)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 14.f)
		[
			SNew(SBox).WidthOverride(560.f)
			[
				SNew(STextBlock).Font(CSUI::Font(13)).ColorAndOpacity(CSUI::TextDim).AutoWrapText(true).Justification(ETextJustify::Center)
				.Text(LOCTEXT("RecNote", "We send a letter to your email. Type the 6-digit code from it here - or paste the whole reset link - with a new password."))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			MakeField(LOCTEXT("Email", "EMAIL"), RecoverEmail, LOCTEXT("EmailHint", "you@example.com"), false, true)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 12.f, 0.f, 12.f)
		[
			MakeFormMessage()
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			MakeWideButton(LOCTEXT("BtnSendCode", "SEND THE CODE"), FOnClicked::CreateSP(this, &SCSLoginScreen::OnSubmit), true,
				TAttribute<bool>(this, &SCSLoginScreen::IsFormEnabled), 52.f)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 10.f, 0.f, 0.f)
		[
			MakeWideButton(LOCTEXT("BtnHaveCode", "I ALREADY HAVE A CODE"), FOnClicked::CreateLambda([this]()
			{
				ShowPage(EPage::RecoverCode);
				return FReply::Handled();
			}), false, TAttribute<bool>(this, &SCSLoginScreen::IsFormEnabled), 42.f)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f, 0.f, 0.f)
		[
			MakeWideButton(LOCTEXT("BtnBack", "BACK"), FOnClicked::CreateLambda([this]() { ShowPage(EPage::EmailSignIn); return FReply::Handled(); }),
				false, TAttribute<bool>(this, &SCSLoginScreen::IsFormEnabled), 42.f)
		];
}

TSharedRef<SWidget> SCSLoginScreen::BuildRecoverCodePage()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 6.f)
		[
			SNew(STextBlock).Text(LOCTEXT("CodeTitle", "ENTER THE CODE")).Font(CSUI::Font(20, true)).ColorAndOpacity(CSUI::Text)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 14.f)
		[
			SNew(SBox).WidthOverride(560.f)
			[
				SNew(STextBlock).Font(CSUI::Font(13)).ColorAndOpacity(CSUI::TextDim).AutoWrapText(true).Justification(ETextJustify::Center)
				.Text_Lambda([this]()
				{
					return FText::Format(LOCTEXT("CodeNote", "The letter was sent to {0}. The code (or link) works once and expires in an hour. Check the spam folder too."),
						FText::FromString(Field(RecoverEmail)));
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			MakeField(LOCTEXT("Code", "CODE OR LINK FROM THE EMAIL"), RecoverCode, LOCTEXT("CodeHint", "123456  or  https://...token=..."), false, false)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f, 0.f, 0.f)
		[
			MakeField(LOCTEXT("PasswordNew", "NEW PASSWORD (AT LEAST 8 CHARACTERS)"), RecoverPassword, FText::GetEmpty(), true, true)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 12.f, 0.f, 12.f)
		[
			MakeFormMessage()
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			MakeWideButton(LOCTEXT("BtnReset", "SET THE PASSWORD AND PLAY"), FOnClicked::CreateSP(this, &SCSLoginScreen::OnSubmit), true,
				TAttribute<bool>(this, &SCSLoginScreen::IsFormEnabled), 52.f)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 10.f, 0.f, 0.f)
		[
			MakeWideButton(LOCTEXT("BtnBack", "BACK"), FOnClicked::CreateLambda([this]() { ShowPage(EPage::RecoverRequest); return FReply::Handled(); }),
				false, TAttribute<bool>(this, &SCSLoginScreen::IsFormEnabled), 42.f)
		];
}

TSharedRef<SWidget> SCSLoginScreen::BuildChoicePage()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(STextBlock).Text(LOCTEXT("ChoiceTitle", "NEW EPIC ACCOUNT")).Font(CSUI::Font(20, true)).ColorAndOpacity(CSUI::Text)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 20.f)
		[
			SNew(SBox).WidthOverride(560.f)
			[
				SNew(STextBlock).Font(CSUI::Font(14)).ColorAndOpacity(CSUI::TextDim).AutoWrapText(true).Justification(ETextJustify::Center)
				.Text_Lambda([this]()
				{
					const UCSAccountSubsystem* Account = GetAccount();
					return FText::Format(LOCTEXT("ChoiceNote", "The Epic account {0} has no CS-Fusion profile yet. Already registered with email? Link Epic to that profile - your nickname and stats stay. Otherwise create a new profile."),
						FText::FromString(Account && !Account->GetEpicDisplayName().IsEmpty() ? Account->GetEpicDisplayName() : TEXT("")));
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			MakeWideButton(LOCTEXT("BtnLinkExisting", "I HAVE AN EMAIL ACCOUNT - LINK EPIC"), FOnClicked::CreateLambda([this]()
			{
				if (const UCSAccountSubsystem* Account = GetAccount())
				{
					LinkEmail->SetText(FText::FromString(Account->GetRememberedEmail()));
				}
				ShowPage(EPage::LinkEmailForEpic);
				return FReply::Handled();
			}), true, TAttribute<bool>(this, &SCSLoginScreen::IsFormEnabled), 52.f)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 10.f, 0.f, 0.f)
		[
			MakeWideButton(LOCTEXT("BtnCreateNew", "CREATE A NEW PROFILE"), FOnClicked::CreateLambda([this]()
			{
				if (UCSAccountSubsystem* Account = GetAccount())
				{
					Account->CreateProfileForEpic();
				}
				return FReply::Handled();
			}), true, TAttribute<bool>(this, &SCSLoginScreen::IsFormEnabled), 52.f)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 10.f, 0.f, 0.f)
		[
			MakeWideButton(LOCTEXT("BtnCancel", "CANCEL"), FOnClicked::CreateLambda([this]()
			{
				if (UCSAccountSubsystem* Account = GetAccount())
				{
					Account->CancelProfileChoice();
				}
				ShowPage(EPage::Main);
				return FReply::Handled();
			}), false, TAttribute<bool>(this, &SCSLoginScreen::IsFormEnabled), 42.f)
		];
}

TSharedRef<SWidget> SCSLoginScreen::BuildLinkPage()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 6.f)
		[
			SNew(STextBlock).Text(LOCTEXT("LinkTitle", "LINK EPIC TO YOUR PROFILE")).Font(CSUI::Font(20, true)).ColorAndOpacity(CSUI::Text)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 14.f)
		[
			SNew(SBox).WidthOverride(560.f)
			[
				SNew(STextBlock).Font(CSUI::Font(13)).ColorAndOpacity(CSUI::TextDim).AutoWrapText(true).Justification(ETextJustify::Center)
				.Text(LOCTEXT("LinkNote", "Sign in with the email of your profile. From then on Epic Games opens it too."))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			MakeField(LOCTEXT("Email", "EMAIL"), LinkEmail, LOCTEXT("EmailHint", "you@example.com"), false, false)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 10.f, 0.f, 0.f)
		[
			MakeField(LOCTEXT("Password", "PASSWORD"), LinkPassword, FText::GetEmpty(), true, true)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 12.f, 0.f, 12.f)
		[
			MakeFormMessage()
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			MakeWideButton(LOCTEXT("BtnLink", "SIGN IN AND LINK"), FOnClicked::CreateSP(this, &SCSLoginScreen::OnSubmit), true,
				TAttribute<bool>(this, &SCSLoginScreen::IsFormEnabled), 52.f)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 10.f, 0.f, 0.f)
		[
			MakeWideButton(LOCTEXT("BtnBack", "BACK"), FOnClicked::CreateLambda([this]() { ShowPage(EPage::Main); return FReply::Handled(); }),
				false, TAttribute<bool>(this, &SCSLoginScreen::IsFormEnabled), 42.f)
		];
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

int32 SCSLoginScreen::GetSwitcherIndex() const
{
	const UCSAccountSubsystem* Account = GetAccount();
	const ECSAccountState State = Account ? Account->GetState() : ECSAccountState::SignedOut;
	if (State == ECSAccountState::ChoosingProfile)
	{
		return Page == EPage::LinkEmailForEpic ? SlotLink : SlotChoice;
	}
	// A sign-in that finished, or one that is checking the saved session, shows the status.
	if (State == ECSAccountState::Ready || State == ECSAccountState::CheckingSession || State == ECSAccountState::NotConfigured)
	{
		return SlotMain;
	}
	switch (Page)
	{
	case EPage::EmailSignIn:		return SlotSignIn;
	case EPage::Register:			return SlotRegister;
	case EPage::RecoverRequest:		return SlotRecoverRequest;
	case EPage::RecoverCode:		return SlotRecoverCode;
	default:						return SlotMain;
	}
}

void SCSLoginScreen::ShowPage(EPage NewPage)
{
	Page = NewPage;
	FormMessage.Reset();
	bMessageIsError = false;
	TSharedPtr<SEditableTextBox> Focus;
	switch (NewPage)
	{
	case EPage::EmailSignIn:		Focus = Field(SignInEmail).IsEmpty() ? SignInEmail : SignInPassword; break;
	case EPage::Register:			Focus = RegNickname; break;
	case EPage::RecoverRequest:		Focus = RecoverEmail; break;
	case EPage::RecoverCode:		Focus = RecoverCode; break;
	case EPage::LinkEmailForEpic:	Focus = Field(LinkEmail).IsEmpty() ? LinkEmail : LinkPassword; break;
	default: break;
	}
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(Focus.IsValid() ? StaticCastSharedPtr<SWidget>(Focus) : SharedThis(this), EFocusCause::SetDirectly);
	}
}

FString SCSLoginScreen::Field(const TSharedPtr<SEditableTextBox>& Box) const
{
	return Box.IsValid() ? Box->GetText().ToString() : FString();
}

void SCSLoginScreen::SetMessage(const FString& Message, bool bError)
{
	FormMessage = Message;
	bMessageIsError = bError;
}

FText SCSLoginScreen::GetStatusText() const
{
	const UCSAccountSubsystem* Account = GetAccount();
	switch (Account ? Account->GetState() : ECSAccountState::SignedOut)
	{
	case ECSAccountState::CheckingSession:	return LOCTEXT("StatusChecking", "SIGNING YOU IN...");
	case ECSAccountState::SigningIn:		return LOCTEXT("StatusSigningIn", "SIGNING IN...");
	case ECSAccountState::Ready:			return FText::Format(LOCTEXT("StatusReady", "WELCOME, {0}"), FText::FromString(Account->GetNickname().ToUpper()));
	case ECSAccountState::Failed:			return LOCTEXT("StatusFailed", "SIGN-IN FAILED");
	case ECSAccountState::NotConfigured:	return LOCTEXT("StatusNoConfig", "ACCOUNTS ARE NOT SET UP");
	case ECSAccountState::Offline:			return LOCTEXT("StatusOffline", "PLAYING OFFLINE");
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
		return FText::FromString(Account->GetLastError());
	}
	if (State == ECSAccountState::CheckingSession)
	{
		return LOCTEXT("DetailChecking", "Using your saved session. No password is needed.");
	}
	if (State == ECSAccountState::SigningIn)
	{
		return LOCTEXT("DetailSigningIn", "Finish the login in the Epic window. If nothing opened, check that the Epic overlay is allowed, or use the browser window that appeared.");
	}
	if (State == ECSAccountState::SignedOut && !Account->GetLastError().IsEmpty())
	{
		return FText::FromString(Account->GetLastError());
	}
	if (State == ECSAccountState::Ready)
	{
		const FCSAccountStats& Stats = Account->GetStats();
		return FText::Format(LOCTEXT("DetailReady", "{0} matches, {1} kills, {2} deaths. Loading the menu..."),
			FText::AsNumber(Stats.Matches), FText::AsNumber(Stats.Kills), FText::AsNumber(Stats.Deaths));
	}
	return LOCTEXT("DetailIdle", "Choose how to sign in. New here? LOGIN WITH EMAIL has a registration form; an Epic account works too.");
}

bool SCSLoginScreen::IsMainEnabled() const
{
	const UCSAccountSubsystem* Account = GetAccount();
	return Account && !Account->IsBusy() && Account->GetState() != ECSAccountState::NotConfigured;
}

bool SCSLoginScreen::IsFormEnabled() const
{
	const UCSAccountSubsystem* Account = GetAccount();
	return Account && !Account->IsBusy();
}

bool SCSLoginScreen::IsOfflineVisible() const
{
	const UCSAccountSubsystem* Account = GetAccount();
	const ECSAccountState State = Account ? Account->GetState() : ECSAccountState::SignedOut;
	return State == ECSAccountState::SignedOut || State == ECSAccountState::Failed || State == ECSAccountState::NotConfigured;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

FReply SCSLoginScreen::OnEmailClicked()
{
	if (const UCSAccountSubsystem* Account = GetAccount(); Account && IsMainEnabled())
	{
		if (Field(SignInEmail).IsEmpty())
		{
			SignInEmail->SetText(FText::FromString(Account->GetRememberedEmail()));
		}
		ShowPage(EPage::EmailSignIn);
	}
	return FReply::Handled();
}

FReply SCSLoginScreen::OnEpicClicked()
{
	if (UCSAccountSubsystem* Account = GetAccount(); Account && IsMainEnabled())
	{
		Account->SignIn();
	}
	return FReply::Handled();
}

void SCSLoginScreen::OnResult(bool bOk, const FString& Message)
{
	SetMessage(Message, !bOk);
}

FReply SCSLoginScreen::OnSubmit()
{
	UCSAccountSubsystem* Account = GetAccount();
	if (!Account || Account->IsBusy())
	{
		return FReply::Handled();
	}
	FormMessage.Reset();
	const TWeakPtr<SCSLoginScreen> Weak = SharedThis(this);
	auto Done = [Weak](bool bOk, const FString& Message)
	{
		if (const TSharedPtr<SCSLoginScreen> Self = Weak.Pin())
		{
			Self->OnResult(bOk, Message);
		}
	};

	const ECSAccountState State = Account->GetState();
	const EPage Current = State == ECSAccountState::ChoosingProfile ? EPage::LinkEmailForEpic : Page;
	switch (Current)
	{
	case EPage::EmailSignIn:
		Account->SignInWithEmail(Field(SignInEmail), Field(SignInPassword), Done);
		break;
	case EPage::Register:
		if (Field(RegPassword) != Field(RegPassword2))
		{
			SetMessage(TEXT("The passwords do not match."), true);
			break;
		}
		Account->RegisterWithEmail(Field(RegEmail), Field(RegPassword), Field(RegNickname), Done);
		break;
	case EPage::RecoverRequest:
		Account->RequestPasswordReset(Field(RecoverEmail), [Weak](bool bOk, const FString& Message)
		{
			if (const TSharedPtr<SCSLoginScreen> Self = Weak.Pin())
			{
				if (bOk)
				{
					Self->ShowPage(EPage::RecoverCode);
				}
				Self->SetMessage(Message, !bOk);
			}
		});
		break;
	case EPage::RecoverCode:
		Account->ResetPassword(Field(RecoverEmail), Field(RecoverCode), Field(RecoverPassword), Done);
		break;
	case EPage::LinkEmailForEpic:
		if (State == ECSAccountState::ChoosingProfile)
		{
			Account->LinkEpicToEmailProfile(Field(LinkEmail), Field(LinkPassword), Done);
		}
		break;
	default:
		break;
	}
	return FReply::Handled();
}

FReply SCSLoginScreen::OnOfflineClicked()
{
	if (UCSAccountSubsystem* Account = GetAccount())
	{
		Account->PlayOffline();
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
	if (!bNotified && Account && Account->IsOffline())
	{
		bNotified = true;
		OnSignedIn.ExecuteIfBound();
		return;
	}
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
	if (InKeyEvent.GetKey() == EKeys::Escape && Page != EPage::Main)
	{
		ShowPage(Page == EPage::RecoverCode ? EPage::RecoverRequest
			: (Page == EPage::RecoverRequest || Page == EPage::Register ? EPage::EmailSignIn : EPage::Main));
		return FReply::Handled();
	}
	if (InKeyEvent.GetKey() == EKeys::Enter)
	{
		const UCSAccountSubsystem* Account = GetAccount();
		if (Account && Account->IsReady())
		{
			OnSignedIn.ExecuteIfBound();
			return FReply::Handled();
		}
		if (Page != EPage::Main)
		{
			return OnSubmit();
		}
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
