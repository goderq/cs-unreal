// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// The first screen of the game: sign in before the main menu.
//
// It only drives UCSAccountSubsystem and shows what that says. A returning
// player never sees a button: the remembered session (email) or the saved Epic
// session signs them in silently and the menu follows. Otherwise (v2.4) two
// ways in: [LOGIN WITH EMAIL] (sign in, register, recover the password with a
// code from the email) and [LOGIN WITH EPIC GAMES] (the Epic overlay or
// browser). A first Epic sign-in without a profile asks whether to create one
// or to link Epic to the player's email profile. [PLAY OFFLINE] keeps
// practice available without a connection.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class STextBlock;

class CSFUSION_API SCSLoginScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCSLoginScreen) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, WorldContext)
		/** Called once the account is ready and the main menu should take over. */
		SLATE_EVENT(FSimpleDelegate, OnSignedIn)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SCSLoginScreen() override;

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** The form pages (the Epic profile choice follows the account state). */
	enum class EPage : uint8 { Main, EmailSignIn, Register, RecoverRequest, RecoverCode, LinkEmailForEpic };

	/** Self-tests: open a page, as if the player clicked there. */
	void ShowPage(EPage NewPage);

private:
	class UCSAccountSubsystem* GetAccount() const;
	int32 GetSwitcherIndex() const;
	FText GetStatusText() const;
	FText GetDetailText() const;
	bool IsMainEnabled() const;
	bool IsFormEnabled() const;
	bool IsOfflineVisible() const;

	TSharedRef<SWidget> BuildMainPage();
	TSharedRef<SWidget> BuildSignInPage();
	TSharedRef<SWidget> BuildRegisterPage();
	TSharedRef<SWidget> BuildRecoverRequestPage();
	TSharedRef<SWidget> BuildRecoverCodePage();
	TSharedRef<SWidget> BuildChoicePage();
	TSharedRef<SWidget> BuildLinkPage();
	TSharedRef<SWidget> MakeField(const FText& Label, TSharedPtr<SEditableTextBox>& OutBox, const FText& Hint, bool bPassword, bool bSubmitOnEnter);
	TSharedRef<SWidget> MakeFormMessage();
	TSharedRef<SWidget> MakeWideButton(const TAttribute<FText>& Label, FOnClicked OnClicked, bool bPrimary, TAttribute<bool> Enabled, float Height = 48.f);

	FReply OnEmailClicked();
	FReply OnEpicClicked();
	FReply OnOfflineClicked();
	FReply OnQuitClicked();
	FReply OnSubmit();
	void OnResult(bool bOk, const FString& Message);
	void SetMessage(const FString& Message, bool bError);
	FString Field(const TSharedPtr<SEditableTextBox>& Box) const;

	TWeakObjectPtr<UObject> WorldContext;
	FSimpleDelegate OnSignedIn;
	bool bNotified = false;
	float SpinnerPhase = 0.f;
	double ReadyAt = 0.0;

	EPage Page = EPage::Main;
	FString FormMessage;
	bool bMessageIsError = false;

	TSharedPtr<SEditableTextBox> SignInEmail;
	TSharedPtr<SEditableTextBox> SignInPassword;
	TSharedPtr<SEditableTextBox> RegNickname;
	TSharedPtr<SEditableTextBox> RegEmail;
	TSharedPtr<SEditableTextBox> RegPassword;
	TSharedPtr<SEditableTextBox> RegPassword2;
	TSharedPtr<SEditableTextBox> RecoverEmail;
	TSharedPtr<SEditableTextBox> RecoverCode;
	TSharedPtr<SEditableTextBox> RecoverPassword;
	TSharedPtr<SEditableTextBox> LinkEmail;
	TSharedPtr<SEditableTextBox> LinkPassword;
};
