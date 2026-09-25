// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v2.0 admin panel: a page in the main menu, shown to moderators and above.
// Every action goes through the admin Edge Function (AdminService), which
// checks the caller's role and permission against the database, refuses
// actions on players of the same or a higher role, and writes the security
// log with the reason and the old and new values. This page decides nothing -
// it only asks, and greys out what the role cannot do (the server refuses it
// anyway).
//
// PLAYERS    search, details (stats, recent matches, history), ban 1 h /
//            1 day / 7 days / permanently, kick, unban, role, nickname
//            (set or reset to an automatic one), reset stats
// MATCHES    all / suspicious / void, details with every participant, flag,
//            approve, void
// LOG        the security log (moderators: their own actions and the
//            automatic entries)
//
// Every action needs a reason, typed once in the REASON box.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FJsonObject;
class SEditableTextBox;
class SVerticalBox;
class SWidgetSwitcher;
class UCSAccountSubsystem;

struct FCSAdminPlayerRow
{
	FString Id;
	FString Nickname;
	FString Role;
	FString LastSeen;
	FString BannedUntil;
	bool bBanned = false;
	int32 Matches = 0;
	int32 Kills = 0;
	int32 Deaths = 0;
};

struct FCSAdminMatchRow
{
	FString Id;
	FString Mode;
	FString Map;
	FString StartedAt;
	FString Status;
	FString Host;
	TArray<FString> Reasons;
	bool bRanked = false;
	bool bSuspicious = false;
	bool bVoided = false;
	bool bApplied = false;
	int32 Players = 0;
};

class CSFUSION_API SCSAdminPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCSAdminPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, WorldContext)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Page opened: who am I, then the current tab's data. */
	void Refresh();

private:
	enum class ETab : int32 { Players, Matches, Log };

	UCSAccountSubsystem* GetAccount() const;
	/** The role may do this (from whoami). A hint for the buttons only. */
	bool Can(const TCHAR* Permission) const { return Permissions.Contains(Permission); }
	FString GetReason() const;

	TSharedRef<SWidget> MakeTabButton(const FText& Label, ETab Tab);
	TSharedRef<SWidget> MakePlayersTab();
	TSharedRef<SWidget> MakeMatchesTab();
	TSharedRef<SWidget> MakeLogTab();
	TSharedRef<SWidget> MakePlayerActions();
	TSharedRef<SWidget> MakeMatchActions();
	TSharedRef<SWidget> ActionButton(const TAttribute<FText>& Label, int32 Kind, const TCHAR* Permission,
		bool bNeedsPlayer, TFunction<void()> OnClick);

	void ShowTab(ETab Tab);
	void RebuildPlayers();
	void RebuildPlayerDetails();
	void RebuildMatches();
	void RebuildMatchDetails();
	void RebuildLog();

	void LoadWhoAmI();
	void LoadPlayers();
	void LoadPlayerDetails();
	void LoadMatches();
	void LoadMatchDetails();
	void LoadLog();

	/** One admin action; the reason is added; the affected lists reload after it. */
	void Act(const FString& Action, TSharedRef<FJsonObject> Params, const FText& Done);
	void ActOnPlayer(const FString& Action, TSharedRef<FJsonObject> Params, const FText& Done);
	void ActOnMatch(const FString& Action, const FText& Done);

	void SetStatus(const FText& Text, bool bError);

	TWeakObjectPtr<UObject> WorldContext;
	ETab CurrentTab = ETab::Players;

	TSharedPtr<SWidgetSwitcher> Switcher;
	TSharedPtr<SEditableTextBox> SearchBox;
	TSharedPtr<SEditableTextBox> ReasonBox;
	TSharedPtr<SEditableTextBox> NicknameBox;
	TSharedPtr<SVerticalBox> PlayersBox;
	TSharedPtr<SVerticalBox> PlayerDetailsBox;
	TSharedPtr<SVerticalBox> MatchesBox;
	TSharedPtr<SVerticalBox> MatchDetailsBox;
	TSharedPtr<SVerticalBox> LogBox;

	FString MyRole;
	TSet<FString> Permissions;

	TArray<FCSAdminPlayerRow> Players;
	FString SelectedPlayer;
	TSharedPtr<FJsonObject> PlayerDetails;

	TArray<FCSAdminMatchRow> Matches;
	FString MatchFilter = TEXT("all");
	FString SelectedMatch;
	TSharedPtr<FJsonObject> MatchDetails;

	TArray<TSharedPtr<FJsonObject>> LogRows;

	FText Status;
	bool bStatusError = false;
	bool bBusy = false;
	/** Reset stats asks twice: the first click arms it for a few seconds. */
	double ResetArmedUntil = 0.0;
};
