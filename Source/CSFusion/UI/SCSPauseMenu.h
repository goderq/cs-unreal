// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// In-match ESC menu: Resume, Settings, Leave Match.
//
// The match does not pause - it is multiplayer. The menu only takes the
// local player's input; the pawn stands still and can still be shot, which
// the header says plainly.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SWidgetSwitcher;
class SCSSettingsPanel;

class CSFUSION_API SCSPauseMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCSPauseMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, WorldContext)
		SLATE_EVENT(FSimpleDelegate, OnResume)
		SLATE_EVENT(FSimpleDelegate, OnLeaveMatch)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** Back to the first page (call when the menu is opened). */
	void ResetToMain();

	void ShowSettings();

private:
	FText GetSessionLine() const;

	TWeakObjectPtr<UObject> WorldContext;
	FSimpleDelegate OnResume;
	FSimpleDelegate OnLeaveMatch;

	TSharedPtr<SWidgetSwitcher> Switcher;
	TSharedPtr<SCSSettingsPanel> Settings;
};
