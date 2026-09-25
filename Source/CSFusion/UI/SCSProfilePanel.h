// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Profile page: who you are signed in as, your lifetime numbers and the top
// players. v2.0: the nickname can no longer be changed (it comes from Epic on
// the first sign-in; only an admin may change it).
//
// Everything here is read through UCSAccountSubsystem, which talks to the
// backend. The page owns no state of its own beyond the last leaderboard it
// received, so it is safe to leave open: the rows are
// only refreshed when the page is opened or REFRESH is pressed.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

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

	/** Called when the page is opened: re-reads the board. */
	void Refresh();

private:
	UCSAccountSubsystem* GetAccount() const;

	TSharedRef<SWidget> MakeIdentityCard();
	TSharedRef<SWidget> MakeBoardHeader();
	void RebuildBoard();

	void FetchBoard();

	TWeakObjectPtr<UObject> WorldContext;

	TSharedPtr<SVerticalBox> BoardBox;

	TArray<FCSLeaderboardRow> Rows;

	/** Leaderboard state: a request is out, or the last one failed. */
	bool bLoading = false;
	bool bLoadFailed = false;
};
