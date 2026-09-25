// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v2.0 admin panel: a page in the main menu, shown only to profiles with
// is_admin. Every action goes through the admin Edge Function, which checks
// the admin flag against the database again and logs what was done - this
// page decides nothing, it only asks.
//
// Players: search by nickname, pick one, then ban (1 hour, a day, a week,
// permanently), unban, give or take admin rights, rename, reset stats. The
// journal shows the latest admin actions.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FJsonObject;
class SEditableTextBox;
class SVerticalBox;
class UCSAccountSubsystem;

struct FCSAdminPlayerRow
{
	FString Id;
	FString Nickname;
	FString LastSeen;
	FString BannedUntil;
	bool bAdmin = false;
	int32 Matches = 0;
	int32 Kills = 0;
	int32 Deaths = 0;

	bool IsBanned() const;
};

class CSFUSION_API SCSAdminPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCSAdminPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, WorldContext)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Page opened: reload the player list and the journal. */
	void Refresh();

private:
	UCSAccountSubsystem* GetAccount() const;

	TSharedRef<SWidget> MakePlayerList();
	TSharedRef<SWidget> MakeDetails();
	TSharedRef<SWidget> MakeJournal();
	void RebuildPlayers();
	void RebuildJournal();

	void LoadPlayers();
	void LoadJournal();
	const FCSAdminPlayerRow* GetSelected() const;

	/** One admin action on the selected player; reloads the list after it. */
	void Act(const FString& Action, TSharedRef<FJsonObject> Params, const FText& Done);

	TWeakObjectPtr<UObject> WorldContext;

	TSharedPtr<SEditableTextBox> SearchBox;
	TSharedPtr<SEditableTextBox> RenameBox;
	TSharedPtr<SVerticalBox> PlayersBox;
	TSharedPtr<SVerticalBox> JournalBox;

	TArray<FCSAdminPlayerRow> Players;
	TArray<FString> Journal;
	FString SelectedId;

	FText Status;
	bool bStatusError = false;
	bool bBusy = false;
	/** Reset stats asks twice: the first click arms it for a few seconds. */
	double ResetArmedUntil = 0.0;
};
