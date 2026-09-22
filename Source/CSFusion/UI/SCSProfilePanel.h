// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Profile page: who you are signed in as, your lifetime numbers, renaming,
// and the top players.
//
// Everything here is read through UCSAccountSubsystem, which talks to the
// backend. The page owns no state of its own beyond what is being typed and
// the last leaderboard it received, so it is safe to leave open: the rows are
// only refreshed when the page is opened or REFRESH is pressed.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class SVerticalBox;
class UCSAccountSubsystem;

/** One row of the leaderboard view, already parsed out of the JSON. */
struct FCSLeaderboardRow
{
	FString Id;
	FString Nickname;
	int32 Kills = 0;
	int32 Deaths = 0;
	int32 Matches = 0;
	int32 Wins = 0;
	float KD = 0.f;
};

class CSFUSION_API SCSProfilePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCSProfilePanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, WorldContext)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Called when the page is opened: refills the name box and re-reads the board. */
	void Refresh();

	/** Types a name into the box and saves it - what the SAVE button does (self-test). */
	void RequestRename(const FString& NewName);

private:
	UCSAccountSubsystem* GetAccount() const;

	TSharedRef<SWidget> MakeIdentityCard();
	TSharedRef<SWidget> MakeRenameRow();
	TSharedRef<SWidget> MakeBoardHeader();
	void RebuildBoard();

	void FetchBoard();
	void CommitNickname();

	TWeakObjectPtr<UObject> WorldContext;

	TSharedPtr<SEditableTextBox> NameBox;
	TSharedPtr<SVerticalBox> BoardBox;

	TArray<FCSLeaderboardRow> Rows;

	/** Rename result, shown under the box; green when it worked. */
	FText RenameStatus;
	bool bRenameOk = false;
	bool bRenaming = false;

	/** Leaderboard state: a request is out, or the last one failed. */
	bool bLoading = false;
	bool bLoadFailed = false;
};
