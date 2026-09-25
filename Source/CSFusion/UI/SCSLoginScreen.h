// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// The first screen of the game: sign in with Epic before the main menu.
//
// It only drives UCSAccountSubsystem and shows what that says. A returning
// player never sees a button: the saved Epic session signs them in silently
// and the menu follows. The [SIGN IN WITH EPIC] button (the Epic overlay or
// browser) appears only when that did not work; [PLAY OFFLINE] keeps practice
// available without a connection.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

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

private:
	class UCSAccountSubsystem* GetAccount() const;
	FText GetStatusText() const;
	FText GetDetailText() const;
	FText GetButtonText() const;
	bool IsButtonEnabled() const;
	bool IsSignInVisible() const;
	bool IsOfflineVisible() const;
	FReply OnSignInClicked();
	FReply OnOfflineClicked();
	FReply OnQuitClicked();

	TWeakObjectPtr<UObject> WorldContext;
	FSimpleDelegate OnSignedIn;
	bool bNotified = false;
	float SpinnerPhase = 0.f;
	double ReadyAt = 0.0;
};
