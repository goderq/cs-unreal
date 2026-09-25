// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Main menu: PLAY (Quick Match, Create Session, Join Session, Session
// Browser), INVENTORY, SETTINGS, QUIT.
//
// Everything network-related goes through UCSSessionSubsystem; this widget
// only collects input and shows state. While a connect/join is in flight a
// blocking overlay shows progress and offers CANCEL; errors land in the
// status line instead of silently doing nothing.

#pragma once

#include "CoreMinimal.h"
#include "Multiplayer/CSSessionSubsystem.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class SVerticalBox;
class SWidgetSwitcher;
class SCSSelector;
class SCSSettingsPanel;
class SCSProfilePanel;
class SCSAdminPanel;

class CSFUSION_API SCSMainMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCSMainMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, WorldContext)
	SLATE_END_ARGS()

	// The order is the order of the switcher slots in Construct.
	enum class EPage : int32 { Home, Play, Create, Join, Browser, Settings, Profile, Admin };

	void Construct(const FArguments& InArgs);
	virtual ~SCSMainMenu() override;

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	void ShowPage(EPage Page);
	EPage GetPage() const { return CurrentPage; }

	// --- Actions (buttons call these; so does the menu self-test) ------------
	void QuickMatch();
	void CreateSession(const FString& RoomName, int32 MaxPlayers);
	void JoinSession(const FString& RoomName);
	void Quit();

	/** Offline match against the selected number of bots. */
	void StartPractice();
	int32 GetSelectedBotCount() const;
	ECSBotDifficulty GetSelectedBotDifficulty() const;

	/** Region code picked on the Play page; empty = best ping. */
	FString GetSelectedRegion() const;


	// v1.1 mode and map choice (Play page).
	ECSGameModeType GetSelectedMode() const { return SelectedMode; }
	FName GetSelectedMap() const;
	void SelectMode(ECSGameModeType Mode) { SelectedMode = Mode; }
	void SelectMap(int32 Index);

private:
	UCSSessionSubsystem* GetSession() const;
	FCSSessionRequest MakeRequest() const;

	TSharedRef<SWidget> MakeNav();
	TSharedRef<SWidget> MakeHomePage();
	TSharedRef<SWidget> MakePlayPage();
	TSharedRef<SWidget> MakeCreatePage();
	TSharedRef<SWidget> MakeJoinPage();
	TSharedRef<SWidget> MakeBrowserPage();
	TSharedRef<SWidget> MakeBusyOverlay();
	/** Big selectable card for a mode or a map. */
	TSharedRef<SWidget> MakeChoiceTile(const FText& Title, const FText& TagText, const FText& Body, const FLinearColor& Color,
		TFunction<bool()> IsSelected, TFunction<void()> OnPick, float Height);

	void RebuildRoomList();

	FText GetStatusText() const;
	FText GetBusyText() const;
	EVisibility GetBusyVisibility() const;

	TWeakObjectPtr<UObject> WorldContext;
	EPage CurrentPage = EPage::Home;

	TSharedPtr<SWidgetSwitcher> Switcher;
	TSharedPtr<SCSSelector> RegionSelector;
	TSharedPtr<SCSSelector> BotCountSelector;
	TSharedPtr<SCSSelector> BotDifficultySelector;
	TSharedPtr<SCSSelector> MaxPlayersSelector;
	TSharedPtr<SEditableTextBox> CreateNameBox;
	TSharedPtr<SEditableTextBox> JoinNameBox;
	TSharedPtr<SVerticalBox> RoomListBox;
	TSharedPtr<SCSSettingsPanel> SettingsPanel;
	TSharedPtr<SCSProfilePanel> ProfilePanel;
	TSharedPtr<SCSAdminPanel> AdminPanel;

	FDelegateHandle RoomListHandle;
	FString LocalMessage;

	ECSGameModeType SelectedMode = ECSGameModeType::Deathmatch;
	int32 SelectedMapIndex = 0;
};
