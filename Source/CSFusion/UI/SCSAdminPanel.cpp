// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "UI/SCSAdminPanel.h"

#include "Account/CSAccountSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UI/CSUIStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CSAdmin"

namespace
{
	/** "2026-09-23T12:40:11.123+00:00" -> "2026-09-23 12:40". */
	FString ShortDate(const FString& Iso)
	{
		return Iso.Len() < 16 ? Iso : Iso.Left(10) + TEXT(" ") + Iso.Mid(11, 5);
	}

	FString Str(const TSharedPtr<FJsonObject>& Json, const TCHAR* Name)
	{
		FString Value;
		if (Json.IsValid())
		{
			Json->TryGetStringField(Name, Value);
		}
		return Value;
	}

	int32 Num(const TSharedPtr<FJsonObject>& Json, const TCHAR* Name)
	{
		double Value = 0.0;
		return (Json.IsValid() && Json->TryGetNumberField(Name, Value)) ? FMath::RoundToInt(Value) : 0;
	}

	bool Bool(const TSharedPtr<FJsonObject>& Json, const TCHAR* Name)
	{
		bool Value = false;
		return Json.IsValid() && Json->TryGetBoolField(Name, Value) && Value;
	}

	TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& Json, const TCHAR* Name)
	{
		const TSharedPtr<FJsonObject>* Found = nullptr;
		return (Json.IsValid() && Json->TryGetObjectField(Name, Found)) ? *Found : nullptr;
	}

	TArray<TSharedPtr<FJsonObject>> Rows(const TSharedPtr<FJsonObject>& Json, const TCHAR* Name)
	{
		TArray<TSharedPtr<FJsonObject>> Out;
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (Json.IsValid() && Json->TryGetArrayField(Name, List))
		{
			for (const TSharedPtr<FJsonValue>& Value : *List)
			{
				if (TSharedPtr<FJsonObject> Row = Value.IsValid() ? Value->AsObject() : nullptr)
				{
					Out.Add(Row);
				}
			}
		}
		return Out;
	}

	/** Compact one-line JSON for old/new values in the log. */
	FString Compact(const TSharedPtr<FJsonObject>& Json)
	{
		if (!Json.IsValid() || Json->Values.Num() == 0)
		{
			return TEXT("-");
		}
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);
		return Out.Left(160);
	}

	FLinearColor RoleColor(const FString& Role)
	{
		if (Role == TEXT("superadmin") || Role == TEXT("admin"))
		{
			return CSUI::Accent;
		}
		return Role == TEXT("moderator") ? CSUI::Warning : CSUI::Text;
	}

	TSharedRef<SWidget> Line(const FText& Text, const FLinearColor& Color, int32 Size = 13, bool bBold = false)
	{
		return SNew(STextBlock).Text(Text).Font(CSUI::Font(Size, bBold)).ColorAndOpacity(Color).AutoWrapText(true);
	}
}

void SCSAdminPanel::Construct(const FArguments& InArgs)
{
	WorldContext = InArgs._WorldContext;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			CSUI::MakeHeader(LOCTEXT("Title", "ADMIN"),
				LOCTEXT("Sub", "Every action is checked by the server against your role and written to the security log with its reason."))
		]

		// Tabs, the role we act with, and the reason every action needs.
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)[ MakeTabButton(LOCTEXT("TabPlayers", "PLAYERS"), ETab::Players) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)[ MakeTabButton(LOCTEXT("TabMatches", "MATCHES"), ETab::Matches) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 16.f, 0.f)[ MakeTabButton(LOCTEXT("TabLog", "SECURITY LOG"), ETab::Log) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 16.f, 0.f)
			[
				SNew(STextBlock).Font(CSUI::Font(13, true))
				.Text_Lambda([this]() { return MyRole.IsEmpty() ? FText::GetEmpty() : FText::FromString(MyRole.ToUpper()); })
				.ColorAndOpacity_Lambda([this]() { return FSlateColor(RoleColor(MyRole)); })
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SAssignNew(ReasonBox, SEditableTextBox).Font(CSUI::Font(15))
				.HintText(LOCTEXT("ReasonHint", "REASON for the next action (required, goes to the security log)"))
			]
		]

		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SAssignNew(Switcher, SWidgetSwitcher)
			+ SWidgetSwitcher::Slot()[ MakePlayersTab() ]
			+ SWidgetSwitcher::Slot()[ MakeMatchesTab() ]
			+ SWidgetSwitcher::Slot()[ MakeLogTab() ]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
		[
			SNew(STextBlock).Font(CSUI::Font(14)).AutoWrapText(true)
			.Text_Lambda([this]() { return Status; })
			.ColorAndOpacity_Lambda([this]() { return FSlateColor(bStatusError ? CSUI::Danger : CSUI::Money); })
		]
	];
}

UCSAccountSubsystem* SCSAdminPanel::GetAccount() const
{
	return WorldContext.IsValid() ? UCSAccountSubsystem::Get(WorldContext.Get()) : nullptr;
}

FString SCSAdminPanel::GetReason() const
{
	return ReasonBox.IsValid() ? ReasonBox->GetText().ToString().TrimStartAndEnd() : FString();
}

void SCSAdminPanel::SetStatus(const FText& Text, bool bError)
{
	Status = Text;
	bStatusError = bError;
}

TSharedRef<SWidget> SCSAdminPanel::MakeTabButton(const FText& Label, ETab Tab)
{
	return SNew(SButton).IsFocusable(false)
		.ButtonStyle(&CSUI::ButtonStyle(CSUI::EButtonKind::Nav))
		.ContentPadding(FMargin(14.f, 8.f))
		.OnClicked_Lambda([this, Tab]() { ShowTab(Tab); return FReply::Handled(); })
		[
			SNew(STextBlock).Text(Label).Font(CSUI::Font(15, true))
			.ColorAndOpacity_Lambda([this, Tab]() { return FSlateColor(CurrentTab == Tab ? CSUI::Accent : CSUI::TextDim); })
		];
}

TSharedRef<SWidget> SCSAdminPanel::ActionButton(const TAttribute<FText>& Label, int32 Kind, const TCHAR* Permission,
	bool bNeedsPlayer, TFunction<void()> OnClick)
{
	const FString Perm = Permission ? Permission : TEXT("");
	return SNew(SBox).HeightOverride(38.f)
		[
			CSUI::MakeButton(Label, FOnClicked::CreateLambda([OnClick]() { OnClick(); return FReply::Handled(); }),
				static_cast<CSUI::EButtonKind>(Kind),
				TAttribute<bool>::CreateLambda([this, Perm, bNeedsPlayer]()
				{
					const bool bTarget = bNeedsPlayer ? !SelectedPlayer.IsEmpty() : !SelectedMatch.IsEmpty();
					return !bBusy && bTarget && (Perm.IsEmpty() || Can(*Perm));
				}), 13)
		];
}

// ---------------------------------------------------------------------------
// Players tab
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SCSAdminPanel::MakePlayersTab()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.52f).Padding(0.f, 0.f, 16.f, 0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SAssignNew(SearchBox, SEditableTextBox).Font(CSUI::Font(15))
					.HintText(LOCTEXT("SearchHint", "Nickname, profile id or Epic id (empty = recently seen)"))
					.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Commit)
					{
						if (Commit == ETextCommit::OnEnter)
						{
							LoadPlayers();
						}
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SBox).WidthOverride(120.f)
					[
						CSUI::MakeButton(LOCTEXT("Search", "SEARCH"), FOnClicked::CreateLambda([this]() { LoadPlayers(); return FReply::Handled(); }),
							CSUI::EButtonKind::Primary, TAttribute<bool>::CreateLambda([this]() { return !bBusy; }), 14)
					]
				]
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SScrollBox) + SScrollBox::Slot()[ SAssignNew(PlayersBox, SVerticalBox) ]
			]
		]
		+ SHorizontalBox::Slot().FillWidth(0.48f)
		[
			SNew(SBorder).BorderImage(CSUI::WhiteBrush()).BorderBackgroundColor(CSUI::PanelRaised).Padding(FMargin(16.f, 12.f))
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()[ SAssignNew(PlayerDetailsBox, SVerticalBox) ]
				+ SScrollBox::Slot().Padding(0.f, 10.f, 0.f, 0.f)[ MakePlayerActions() ]
			]
		];
}

TSharedRef<SWidget> SCSAdminPanel::MakePlayerActions()
{
	using CSUI::EButtonKind;
	auto Ban = [this](int32 Hours, const FText& Done)
	{
		const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetNumberField(TEXT("hours"), Hours);
		ActOnPlayer(TEXT("ban"), Params, Done);
	};
	auto SetRole = [this](const TCHAR* Role, const FText& Done)
	{
		const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("role"), Role);
		ActOnPlayer(TEXT("set_role"), Params, Done);
	};
	const int32 Normal = static_cast<int32>(EButtonKind::Normal);
	const int32 Danger = static_cast<int32>(EButtonKind::Danger);

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ CSUI::MakeSectionLabel(LOCTEXT("BanLabel", "BAN / KICK")) ]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)[ ActionButton(LOCTEXT("Ban1h", "1 HOUR"), Normal, TEXT("players.ban_temp"), true, [Ban]() { Ban(1, LOCTEXT("Banned1h", "Banned for an hour.")); }) ]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)[ ActionButton(LOCTEXT("Ban1d", "1 DAY"), Normal, TEXT("players.ban_temp"), true, [Ban]() { Ban(24, LOCTEXT("Banned1d", "Banned for a day.")); }) ]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)[ ActionButton(LOCTEXT("Ban7d", "7 DAYS"), Normal, TEXT("players.ban_temp"), true, [Ban]() { Ban(24 * 7, LOCTEXT("Banned7d", "Banned for a week.")); }) ]
			+ SHorizontalBox::Slot().FillWidth(1.f)[ ActionButton(LOCTEXT("BanForever", "PERMANENT"), Danger, TEXT("players.ban_permanent"), true, [Ban]() { Ban(0, LOCTEXT("BannedForever", "Banned permanently.")); }) ]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)
			[
				ActionButton(LOCTEXT("Kick", "KICK (10 MIN)"), Normal, TEXT("players.kick"), true, [this]()
				{
					const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
					Params->SetNumberField(TEXT("minutes"), 10);
					ActOnPlayer(TEXT("kick"), Params, LOCTEXT("Kicked", "Kicked: no sign-in or match for 10 minutes."));
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				ActionButton(LOCTEXT("Unban", "UNBAN"), Normal, TEXT("players.unban_temp"), true, [this]()
				{
					ActOnPlayer(TEXT("unban"), MakeShared<FJsonObject>(), LOCTEXT("Unbanned", "Unbanned."));
				})
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[ CSUI::MakeSectionLabel(LOCTEXT("RoleLabel", "ROLE")) ]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)[ ActionButton(LOCTEXT("RolePlayer", "PLAYER"), Normal, TEXT("players.role_set"), true, [SetRole]() { SetRole(TEXT("player"), LOCTEXT("NowPlayer", "Role: player.")); }) ]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)[ ActionButton(LOCTEXT("RoleMod", "MODERATOR"), Normal, TEXT("players.role_set"), true, [SetRole]() { SetRole(TEXT("moderator"), LOCTEXT("NowMod", "Role: moderator.")); }) ]
			+ SHorizontalBox::Slot().FillWidth(1.f)[ ActionButton(LOCTEXT("RoleAdmin", "ADMIN"), Normal, TEXT("players.role_set_admin"), true, [SetRole]() { SetRole(TEXT("admin"), LOCTEXT("NowAdmin", "Role: admin.")); }) ]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[ CSUI::MakeSectionLabel(LOCTEXT("NameLabel", "NICKNAME")) ]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
			[
				SAssignNew(NicknameBox, SEditableTextBox).Font(CSUI::Font(15)).HintText(LOCTEXT("NameHint", "New nickname (3-20)"))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(SBox).WidthOverride(110.f)
				[
					ActionButton(LOCTEXT("SetName", "SET"), Normal, TEXT("players.nickname_set"), true, [this]()
					{
						const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
						Params->SetStringField(TEXT("nickname"), NicknameBox.IsValid() ? NicknameBox->GetText().ToString() : FString());
						ActOnPlayer(TEXT("set_nickname"), Params, LOCTEXT("NameSet", "Nickname changed."));
					})
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(150.f)
				[
					ActionButton(LOCTEXT("ResetName", "AUTOMATIC"), Normal, TEXT("players.nickname_reset"), true, [this]()
					{
						ActOnPlayer(TEXT("reset_nickname"), MakeShared<FJsonObject>(), LOCTEXT("NameReset", "Nickname reset to an automatic one."));
					})
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
		[
			ActionButton(LOCTEXT("ResetStats", "RESET STATS (click twice)"), Danger, TEXT("players.stats_reset"), true, [this]()
			{
				const double Now = FPlatformTime::Seconds();
				if (Now > ResetArmedUntil)
				{
					ResetArmedUntil = Now + 4.0;
					SetStatus(LOCTEXT("ResetArmed", "Click again within 4 seconds to reset this player's stats."), true);
					return;
				}
				ResetArmedUntil = 0.0;
				ActOnPlayer(TEXT("reset_stats"), MakeShared<FJsonObject>(), LOCTEXT("StatsReset", "Stats reset."));
			})
		];
}

void SCSAdminPanel::RebuildPlayers()
{
	if (!PlayersBox.IsValid())
	{
		return;
	}
	PlayersBox->ClearChildren();
	if (Players.Num() == 0)
	{
		PlayersBox->AddSlot().AutoHeight().Padding(12.f, 14.f)
		[
			Line(bBusy ? LOCTEXT("Loading", "Loading...") : LOCTEXT("NoPlayers", "No players found."), CSUI::TextDim, 15)
		];
		return;
	}
	for (const FCSAdminPlayerRow& Row : Players)
	{
		const FString Id = Row.Id;
		const FLinearColor Color = Row.bBanned ? CSUI::Danger : RoleColor(Row.Role);
		FString Tags;
		if (Row.Role != TEXT("player") && !Row.Role.IsEmpty())
		{
			Tags += TEXT("  [") + Row.Role.ToUpper() + TEXT("]");
		}
		if (Row.bBanned)
		{
			Tags += TEXT("  [BANNED]");
		}
		PlayersBox->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SButton).IsFocusable(false)
			.ButtonStyle(&CSUI::ButtonStyle(CSUI::EButtonKind::Nav))
			.ContentPadding(FMargin(12.f, 7.f))
			.OnClicked_Lambda([this, Id]()
			{
				SelectedPlayer = Id;
				PlayerDetails.Reset();
				RebuildPlayerDetails();
				LoadPlayerDetails();
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.5f)
				[
					SNew(STextBlock).Font(CSUI::Font(15, true)).Text(FText::FromString(Row.Nickname + Tags))
					.ColorAndOpacity_Lambda([this, Id, Color]() { return FSlateColor(SelectedPlayer == Id ? CSUI::Accent : Color); })
				]
				+ SHorizontalBox::Slot().FillWidth(0.2f)
				[
					SNew(STextBlock).Font(CSUI::Font(14)).ColorAndOpacity(CSUI::Text)
					.Text(FText::FromString(FString::Printf(TEXT("%d / %d"), Row.Kills, Row.Deaths)))
				]
				+ SHorizontalBox::Slot().FillWidth(0.3f)
				[
					SNew(STextBlock).Font(CSUI::Font(13)).ColorAndOpacity(CSUI::TextDim).Text(FText::FromString(ShortDate(Row.LastSeen)))
				]
			]
		];
	}
}

void SCSAdminPanel::RebuildPlayerDetails()
{
	if (!PlayerDetailsBox.IsValid())
	{
		return;
	}
	PlayerDetailsBox->ClearChildren();
	if (SelectedPlayer.IsEmpty())
	{
		PlayerDetailsBox->AddSlot().AutoHeight()[ Line(LOCTEXT("PickPlayer", "Pick a player on the left."), CSUI::TextDim, 15) ];
		return;
	}
	const TSharedPtr<FJsonObject> Profile = Obj(PlayerDetails, TEXT("profile"));
	if (!Profile.IsValid())
	{
		PlayerDetailsBox->AddSlot().AutoHeight()[ Line(LOCTEXT("LoadingPlayer", "Loading the player..."), CSUI::TextDim, 15) ];
		return;
	}
	const TSharedPtr<FJsonObject> Stats = Obj(PlayerDetails, TEXT("stats"));
	const FString Role = Str(Profile, TEXT("role"));

	PlayerDetailsBox->AddSlot().AutoHeight()[ Line(FText::FromString(Str(Profile, TEXT("nickname"))), CSUI::Text, 24, true) ];
	PlayerDetailsBox->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[
		Line(FText::Format(LOCTEXT("RoleLine", "{0}  -  {1}"), FText::FromString(Role.ToUpper()),
			Bool(Profile, TEXT("banned"))
				? FText::Format(LOCTEXT("BannedUntil", "BANNED until {0}"), FText::FromString(ShortDate(Str(Profile, TEXT("banned_until")))))
				: LOCTEXT("NotBanned", "not banned")),
			Bool(Profile, TEXT("banned")) ? CSUI::Danger : RoleColor(Role), 13, true)
	];
	PlayerDetailsBox->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[
		Line(FText::Format(LOCTEXT("Ids", "Profile {0}{1}"), FText::FromString(Str(Profile, TEXT("id"))),
			Str(Profile, TEXT("epic_account_id")).IsEmpty() ? FText::GetEmpty()
				: FText::Format(LOCTEXT("EpicId", "\nEpic {0}"), FText::FromString(Str(Profile, TEXT("epic_account_id"))))),
			CSUI::TextDim, 12)
	];
	PlayerDetailsBox->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[
		Line(FText::Format(LOCTEXT("Dates", "Registered {0}  -  last seen {1}"),
			FText::FromString(ShortDate(Str(Profile, TEXT("created_at")))), FText::FromString(ShortDate(Str(Profile, TEXT("last_seen_at"))))),
			CSUI::TextDim, 12)
	];
	const int32 Kills = Num(Stats, TEXT("kills"));
	const int32 Deaths = Num(Stats, TEXT("deaths"));
	PlayerDetailsBox->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 2.f)
	[
		Line(FText::Format(LOCTEXT("StatsLine", "Ranked: {0} matches, {1} wins, {2} kills, {3} deaths (K/D {4}), {5} headshots, {6} damage, {7} min played"),
			FText::AsNumber(Num(Stats, TEXT("matches"))), FText::AsNumber(Num(Stats, TEXT("wins"))), FText::AsNumber(Kills), FText::AsNumber(Deaths),
			FText::AsNumber(Deaths > 0 ? FMath::RoundToFloat(100.f * Kills / Deaths) / 100.f : static_cast<float>(Kills)),
			FText::AsNumber(Num(Stats, TEXT("headshots"))), FText::AsNumber(Num(Stats, TEXT("damage"))),
			FText::AsNumber(Num(Stats, TEXT("playtime_seconds")) / 60)), CSUI::Text, 13)
	];
	PlayerDetailsBox->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[
		Line(FText::Format(LOCTEXT("PracticeLine", "Practice: {0} matches, {1} kills, {2} deaths"),
			FText::AsNumber(Num(Stats, TEXT("practice_matches"))), FText::AsNumber(Num(Stats, TEXT("practice_kills"))),
			FText::AsNumber(Num(Stats, TEXT("practice_deaths")))), CSUI::TextDim, 13)
	];

	const TArray<TSharedPtr<FJsonObject>> Recent = Rows(PlayerDetails, TEXT("recent_matches"));
	PlayerDetailsBox->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[ CSUI::MakeSectionLabel(LOCTEXT("RecentLabel", "RECENT MATCHES")) ];
	if (Recent.Num() == 0)
	{
		PlayerDetailsBox->AddSlot().AutoHeight()[ Line(LOCTEXT("NoMatches", "None yet."), CSUI::TextDim, 12) ];
	}
	for (int32 i = 0; i < FMath::Min(8, Recent.Num()); ++i)
	{
		const TSharedPtr<FJsonObject>& M = Recent[i];
		const FString Flags = FString(Bool(M, TEXT("voided")) ? TEXT(" VOID") : TEXT(""))
			+ (Bool(M, TEXT("suspicious")) ? TEXT(" SUSPICIOUS") : TEXT(""))
			+ (Bool(M, TEXT("ranked")) ? TEXT("") : TEXT(" practice"));
		PlayerDetailsBox->AddSlot().AutoHeight()
		[
			Line(FText::FromString(FString::Printf(TEXT("%s  %s %s  %d/%d  hs %d%s%s"), *ShortDate(Str(M, TEXT("started_at"))),
				*Str(M, TEXT("mode")), *Str(M, TEXT("map")), Num(M, TEXT("kills")), Num(M, TEXT("deaths")), Num(M, TEXT("headshots")),
				Bool(M, TEXT("won")) ? TEXT("  won") : TEXT(""), *Flags)), Bool(M, TEXT("suspicious")) ? CSUI::Warning : CSUI::TextDim, 12)
		];
	}

	const TArray<TSharedPtr<FJsonObject>> History = Rows(PlayerDetails, TEXT("security"));
	if (History.Num() > 0)
	{
		PlayerDetailsBox->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[ CSUI::MakeSectionLabel(LOCTEXT("HistoryLabel", "HISTORY")) ];
		for (int32 i = 0; i < FMath::Min(6, History.Num()); ++i)
		{
			const TSharedPtr<FJsonObject>& H = History[i];
			PlayerDetailsBox->AddSlot().AutoHeight()
			[
				Line(FText::FromString(FString::Printf(TEXT("%s  %s  %s: %s"), *ShortDate(Str(H, TEXT("created_at"))),
					*Str(H, TEXT("actor")), *Str(H, TEXT("action")), *Str(H, TEXT("reason")))), CSUI::TextDim, 12)
			];
		}
	}
	if (NicknameBox.IsValid())
	{
		NicknameBox->SetText(FText::FromString(Str(Profile, TEXT("nickname"))));
	}
}

// ---------------------------------------------------------------------------
// Matches tab
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SCSAdminPanel::MakeMatchesTab()
{
	auto Filter = [this](const FText& Label, const TCHAR* Value) -> TSharedRef<SWidget>
	{
		const FString Wanted = Value;
		return SNew(SButton).IsFocusable(false)
			.ButtonStyle(&CSUI::ButtonStyle(CSUI::EButtonKind::Nav))
			.ContentPadding(FMargin(12.f, 6.f))
			.OnClicked_Lambda([this, Wanted]() { MatchFilter = Wanted; LoadMatches(); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(Label).Font(CSUI::Font(13, true))
				.ColorAndOpacity_Lambda([this, Wanted]() { return FSlateColor(MatchFilter == Wanted ? CSUI::Accent : CSUI::TextDim); })
			];
	};

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.55f).Padding(0.f, 0.f, 16.f, 0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[ Filter(LOCTEXT("FilterAll", "ALL"), TEXT("all")) ]
				+ SHorizontalBox::Slot().AutoWidth()[ Filter(LOCTEXT("FilterSusp", "SUSPICIOUS"), TEXT("suspicious")) ]
				+ SHorizontalBox::Slot().AutoWidth()[ Filter(LOCTEXT("FilterVoid", "VOID"), TEXT("voided")) ]
				+ SHorizontalBox::Slot().AutoWidth()[ Filter(LOCTEXT("FilterOpen", "RUNNING"), TEXT("open")) ]
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SScrollBox) + SScrollBox::Slot()[ SAssignNew(MatchesBox, SVerticalBox) ]
			]
		]
		+ SHorizontalBox::Slot().FillWidth(0.45f)
		[
			SNew(SBorder).BorderImage(CSUI::WhiteBrush()).BorderBackgroundColor(CSUI::PanelRaised).Padding(FMargin(16.f, 12.f))
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()[ SAssignNew(MatchDetailsBox, SVerticalBox) ]
				+ SScrollBox::Slot().Padding(0.f, 10.f, 0.f, 0.f)[ MakeMatchActions() ]
			]
		];
}

TSharedRef<SWidget> SCSAdminPanel::MakeMatchActions()
{
	const int32 Normal = static_cast<int32>(CSUI::EButtonKind::Normal);
	const int32 Danger = static_cast<int32>(CSUI::EButtonKind::Danger);
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ CSUI::MakeSectionLabel(LOCTEXT("MatchActions", "DECISION")) ]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)
			[
				ActionButton(LOCTEXT("Flag", "FLAG SUSPICIOUS"), Normal, TEXT("matches.flag"), false,
					[this]() { ActOnMatch(TEXT("match_flag"), LOCTEXT("Flagged", "Flagged: the match no longer counts until approved.")); })
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)
			[
				ActionButton(LOCTEXT("Approve", "APPROVE"), Normal, TEXT("matches.approve"), false,
					[this]() { ActOnMatch(TEXT("match_approve"), LOCTEXT("Approved", "Approved: the match counts.")); })
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				ActionButton(LOCTEXT("Void", "VOID"), Danger, TEXT("matches.void"), false,
					[this]() { ActOnMatch(TEXT("match_void"), LOCTEXT("Voided", "Void: the match counts for nobody.")); })
			]
		];
}

void SCSAdminPanel::RebuildMatches()
{
	if (!MatchesBox.IsValid())
	{
		return;
	}
	MatchesBox->ClearChildren();
	if (Matches.Num() == 0)
	{
		MatchesBox->AddSlot().AutoHeight().Padding(12.f, 14.f)
		[
			Line(bBusy ? LOCTEXT("LoadingMatches", "Loading...") : LOCTEXT("NoMatchRows", "No matches."), CSUI::TextDim, 15)
		];
		return;
	}
	for (const FCSAdminMatchRow& Row : Matches)
	{
		const FString Id = Row.Id;
		const FString Flags = FString(Row.bVoided ? TEXT("  VOID") : TEXT(""))
			+ (Row.bSuspicious ? TEXT("  SUSPICIOUS") : TEXT(""))
			+ (Row.Status == TEXT("open") ? TEXT("  running") : (Row.bRanked ? TEXT("  ranked") : TEXT("  practice")));
		const FLinearColor Color = Row.bVoided ? CSUI::TextDim : (Row.bSuspicious ? CSUI::Warning : CSUI::Text);
		MatchesBox->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SButton).IsFocusable(false)
			.ButtonStyle(&CSUI::ButtonStyle(CSUI::EButtonKind::Nav))
			.ContentPadding(FMargin(12.f, 7.f))
			.OnClicked_Lambda([this, Id]()
			{
				SelectedMatch = Id;
				MatchDetails.Reset();
				RebuildMatchDetails();
				LoadMatchDetails();
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Font(CSUI::Font(14))
				.Text(FText::FromString(FString::Printf(TEXT("%s  %s %s  %d player(s)  host %s%s"), *ShortDate(Row.StartedAt),
					*Row.Mode, *Row.Map, Row.Players, *Row.Host, *Flags)))
				.ColorAndOpacity_Lambda([this, Id, Color]() { return FSlateColor(SelectedMatch == Id ? CSUI::Accent : Color); })
			]
		];
	}
}

void SCSAdminPanel::RebuildMatchDetails()
{
	if (!MatchDetailsBox.IsValid())
	{
		return;
	}
	MatchDetailsBox->ClearChildren();
	if (SelectedMatch.IsEmpty())
	{
		MatchDetailsBox->AddSlot().AutoHeight()[ Line(LOCTEXT("PickMatch", "Pick a match on the left."), CSUI::TextDim, 15) ];
		return;
	}
	const TSharedPtr<FJsonObject> M = Obj(MatchDetails, TEXT("match"));
	if (!M.IsValid())
	{
		MatchDetailsBox->AddSlot().AutoHeight()[ Line(LOCTEXT("LoadingMatch", "Loading the match..."), CSUI::TextDim, 15) ];
		return;
	}

	FString Reasons;
	const TArray<TSharedPtr<FJsonValue>>* ReasonList = nullptr;
	if (M->TryGetArrayField(TEXT("suspicious_reasons"), ReasonList))
	{
		for (const TSharedPtr<FJsonValue>& R : *ReasonList)
		{
			Reasons += (Reasons.IsEmpty() ? TEXT("") : TEXT(", ")) + R->AsString();
		}
	}

	MatchDetailsBox->AddSlot().AutoHeight()
	[
		Line(FText::FromString(FString::Printf(TEXT("%s  %s"), *Str(M, TEXT("mode")), *Str(M, TEXT("map")))), CSUI::Text, 22, true)
	];
	MatchDetailsBox->AddSlot().AutoHeight().Padding(0.f, 2.f)
	[
		Line(FText::FromString(FString::Printf(TEXT("Room '%s'\n%s - %s  (%s)\nwinner team %d, rounds %d, bots %s\n%s%s%s"),
			*Str(M, TEXT("room")), *ShortDate(Str(M, TEXT("started_at"))), *ShortDate(Str(M, TEXT("ended_at"))), *Str(M, TEXT("status")),
			Num(M, TEXT("winner_team")), Num(M, TEXT("rounds")), Bool(M, TEXT("bots")) ? TEXT("yes") : TEXT("no"),
			Bool(M, TEXT("ranked")) ? TEXT("ranked") : TEXT("practice"),
			Bool(M, TEXT("applied")) ? TEXT(", counted") : TEXT(", not counted"),
			Bool(M, TEXT("voided")) ? TEXT(", VOID") : TEXT(""))), CSUI::TextDim, 12)
	];
	if (!Reasons.IsEmpty())
	{
		MatchDetailsBox->AddSlot().AutoHeight().Padding(0.f, 4.f)
		[
			Line(FText::Format(LOCTEXT("Reasons", "Suspicious: {0}"), FText::FromString(Reasons)), CSUI::Warning, 13, true)
		];
	}
	MatchDetailsBox->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)[ CSUI::MakeSectionLabel(LOCTEXT("MatchPlayers", "PLAYERS")) ];
	for (const TSharedPtr<FJsonObject>& P : Rows(MatchDetails, TEXT("players")))
	{
		MatchDetailsBox->AddSlot().AutoHeight()
		[
			Line(FText::FromString(FString::Printf(TEXT("%s  team %d  %d/%d  hs %d  dmg %d  $%d%s"), *Str(P, TEXT("nickname")),
				Num(P, TEXT("team")), Num(P, TEXT("kills")), Num(P, TEXT("deaths")), Num(P, TEXT("headshots")), Num(P, TEXT("damage")),
				Num(P, TEXT("money")), Bool(P, TEXT("won")) ? TEXT("  won") : TEXT(""))), CSUI::Text, 13)
		];
	}
}

// ---------------------------------------------------------------------------
// Security log tab
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SCSAdminPanel::MakeLogTab()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				Line(LOCTEXT("LogHelp", "Who did what to whom, why, and what changed. Moderators see their own actions and the automatic entries."), CSUI::TextDim, 13)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(130.f)
				[
					CSUI::MakeButton(LOCTEXT("RefreshLog", "REFRESH"), FOnClicked::CreateLambda([this]() { LoadLog(); return FReply::Handled(); }),
						CSUI::EButtonKind::Normal, TAttribute<bool>::CreateLambda([this]() { return !bBusy; }), 14)
				]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SScrollBox) + SScrollBox::Slot()[ SAssignNew(LogBox, SVerticalBox) ]
		];
}

void SCSAdminPanel::RebuildLog()
{
	if (!LogBox.IsValid())
	{
		return;
	}
	LogBox->ClearChildren();
	if (LogRows.Num() == 0)
	{
		LogBox->AddSlot().AutoHeight().Padding(12.f, 14.f)[ Line(LOCTEXT("EmptyLog", "Nothing logged yet."), CSUI::TextDim, 15) ];
		return;
	}
	for (const TSharedPtr<FJsonObject>& Row : LogRows)
	{
		const FString Source = Str(Row, TEXT("source"));
		const FString Actor = Str(Row, TEXT("actor"));
		const FString Target = Str(Row, TEXT("target"));
		LogBox->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SBorder).BorderImage(CSUI::WhiteBrush()).BorderBackgroundColor(CSUI::Panel).Padding(FMargin(10.f, 6.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					Line(FText::FromString(FString::Printf(TEXT("%s  [%s]  %s%s  %s%s"), *ShortDate(Str(Row, TEXT("created_at"))), *Source,
						Actor.IsEmpty() ? TEXT("system") : *Actor,
						Str(Row, TEXT("actor_role")).IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Str(Row, TEXT("actor_role"))),
						*Str(Row, TEXT("action")), Target.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" -> %s"), *Target))),
						Source == TEXT("admin") ? CSUI::Text : CSUI::Warning, 13, true)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					Line(FText::FromString(FString::Printf(TEXT("%s    %s  =>  %s"), *Str(Row, TEXT("reason")),
						*Compact(Obj(Row, TEXT("old_value"))), *Compact(Obj(Row, TEXT("new_value"))))), CSUI::TextDim, 12)
				]
			]
		];
	}
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

void SCSAdminPanel::Refresh()
{
	LoadWhoAmI();
	ShowTab(CurrentTab);
}

void SCSAdminPanel::ShowTab(ETab Tab)
{
	CurrentTab = Tab;
	if (Switcher.IsValid())
	{
		Switcher->SetActiveWidgetIndex(static_cast<int32>(Tab));
	}
	switch (Tab)
	{
	case ETab::Players: LoadPlayers(); break;
	case ETab::Matches: LoadMatches(); break;
	case ETab::Log:     LoadLog(); break;
	}
}

void SCSAdminPanel::LoadWhoAmI()
{
	UCSAccountSubsystem* Account = GetAccount();
	if (!Account)
	{
		return;
	}
	TWeakPtr<SCSAdminPanel> WeakSelf = SharedThis(this);
	Account->AdminCall(TEXT("whoami"), MakeShared<FJsonObject>(), [WeakSelf](bool bOk, int32 Code, const TSharedPtr<FJsonObject>& Response)
	{
		const TSharedPtr<SCSAdminPanel> Self = WeakSelf.Pin();
		if (!Self.IsValid())
		{
			return;
		}
		Self->Permissions.Reset();
		const TSharedPtr<FJsonObject> Result = Obj(Response, TEXT("result"));
		if (!bOk || !Result.IsValid())
		{
			Self->MyRole.Reset();
			Self->SetStatus(FText::Format(LOCTEXT("NotStaff", "The server does not accept this account as staff ({0})."), FText::AsNumber(Code)), true);
			return;
		}
		Self->MyRole = Str(Result, TEXT("role"));
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (Result->TryGetArrayField(TEXT("permissions"), List))
		{
			for (const TSharedPtr<FJsonValue>& P : *List)
			{
				Self->Permissions.Add(P->AsString());
			}
		}
	});
}

void SCSAdminPanel::LoadPlayers()
{
	UCSAccountSubsystem* Account = GetAccount();
	if (!Account || !Account->IsStaff())
	{
		SetStatus(LOCTEXT("NoRights", "This account has no staff role."), true);
		return;
	}
	bBusy = true;
	RebuildPlayers();

	const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("query"), SearchBox.IsValid() ? SearchBox->GetText().ToString() : FString());
	Params->SetNumberField(TEXT("limit"), 100);
	TWeakPtr<SCSAdminPanel> WeakSelf = SharedThis(this);
	Account->AdminCall(TEXT("players"), Params, [WeakSelf](bool bOk, int32 Code, const TSharedPtr<FJsonObject>& Response)
	{
		const TSharedPtr<SCSAdminPanel> Self = WeakSelf.Pin();
		if (!Self.IsValid())
		{
			return;
		}
		Self->bBusy = false;
		Self->Players.Reset();
		if (bOk)
		{
			for (const TSharedPtr<FJsonObject>& Json : Rows(Response, TEXT("result")))
			{
				FCSAdminPlayerRow& Row = Self->Players.AddDefaulted_GetRef();
				Row.Id = Str(Json, TEXT("id"));
				Row.Nickname = Str(Json, TEXT("nickname"));
				Row.Role = Str(Json, TEXT("role"));
				Row.LastSeen = Str(Json, TEXT("last_seen_at"));
				Row.BannedUntil = Str(Json, TEXT("banned_until"));
				Row.bBanned = Bool(Json, TEXT("banned"));
				Row.Matches = Num(Json, TEXT("matches"));
				Row.Kills = Num(Json, TEXT("kills"));
				Row.Deaths = Num(Json, TEXT("deaths"));
			}
			Self->SetStatus(FText::Format(LOCTEXT("Loaded", "{0} player(s)."), FText::AsNumber(Self->Players.Num())), false);
		}
		else
		{
			Self->SetStatus(FText::Format(LOCTEXT("LoadFailed", "Could not load the players ({0}): {1}"), FText::AsNumber(Code),
				FText::FromString(Str(Response, TEXT("error")))), true);
		}
		Self->RebuildPlayers();
	});
}

void SCSAdminPanel::LoadPlayerDetails()
{
	UCSAccountSubsystem* Account = GetAccount();
	if (!Account || SelectedPlayer.IsEmpty())
	{
		return;
	}
	const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("profile_id"), SelectedPlayer);
	const FString Wanted = SelectedPlayer;
	TWeakPtr<SCSAdminPanel> WeakSelf = SharedThis(this);
	Account->AdminCall(TEXT("player"), Params, [WeakSelf, Wanted](bool bOk, int32 Code, const TSharedPtr<FJsonObject>& Response)
	{
		const TSharedPtr<SCSAdminPanel> Self = WeakSelf.Pin();
		if (!Self.IsValid() || Self->SelectedPlayer != Wanted)
		{
			return;
		}
		Self->PlayerDetails = bOk ? Obj(Response, TEXT("result")) : nullptr;
		if (!bOk)
		{
			Self->SetStatus(FText::Format(LOCTEXT("DetailsFailed", "Could not load the player ({0}): {1}"), FText::AsNumber(Code),
				FText::FromString(Str(Response, TEXT("error")))), true);
		}
		Self->RebuildPlayerDetails();
	});
}

void SCSAdminPanel::LoadMatches()
{
	UCSAccountSubsystem* Account = GetAccount();
	if (!Account || !Account->IsStaff())
	{
		return;
	}
	bBusy = true;
	RebuildMatches();
	const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("filter"), MatchFilter);
	Params->SetNumberField(TEXT("limit"), 100);
	TWeakPtr<SCSAdminPanel> WeakSelf = SharedThis(this);
	Account->AdminCall(TEXT("matches"), Params, [WeakSelf](bool bOk, int32 Code, const TSharedPtr<FJsonObject>& Response)
	{
		const TSharedPtr<SCSAdminPanel> Self = WeakSelf.Pin();
		if (!Self.IsValid())
		{
			return;
		}
		Self->bBusy = false;
		Self->Matches.Reset();
		if (bOk)
		{
			for (const TSharedPtr<FJsonObject>& Json : Rows(Response, TEXT("result")))
			{
				FCSAdminMatchRow& Row = Self->Matches.AddDefaulted_GetRef();
				Row.Id = Str(Json, TEXT("id"));
				Row.Mode = Str(Json, TEXT("mode"));
				Row.Map = Str(Json, TEXT("map"));
				Row.StartedAt = Str(Json, TEXT("started_at"));
				Row.Status = Str(Json, TEXT("status"));
				Row.Host = Str(Json, TEXT("host"));
				Row.bRanked = Bool(Json, TEXT("ranked"));
				Row.bSuspicious = Bool(Json, TEXT("suspicious"));
				Row.bVoided = Bool(Json, TEXT("voided"));
				Row.bApplied = Bool(Json, TEXT("applied"));
				Row.Players = Num(Json, TEXT("players"));
			}
			Self->SetStatus(FText::Format(LOCTEXT("MatchesLoaded", "{0} match(es)."), FText::AsNumber(Self->Matches.Num())), false);
		}
		else
		{
			Self->SetStatus(FText::Format(LOCTEXT("MatchesFailed", "Could not load the matches ({0}): {1}"), FText::AsNumber(Code),
				FText::FromString(Str(Response, TEXT("error")))), true);
		}
		Self->RebuildMatches();
	});
}

void SCSAdminPanel::LoadMatchDetails()
{
	UCSAccountSubsystem* Account = GetAccount();
	if (!Account || SelectedMatch.IsEmpty())
	{
		return;
	}
	const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("match_id"), SelectedMatch);
	const FString Wanted = SelectedMatch;
	TWeakPtr<SCSAdminPanel> WeakSelf = SharedThis(this);
	Account->AdminCall(TEXT("match"), Params, [WeakSelf, Wanted](bool bOk, int32, const TSharedPtr<FJsonObject>& Response)
	{
		const TSharedPtr<SCSAdminPanel> Self = WeakSelf.Pin();
		if (!Self.IsValid() || Self->SelectedMatch != Wanted)
		{
			return;
		}
		Self->MatchDetails = bOk ? Obj(Response, TEXT("result")) : nullptr;
		Self->RebuildMatchDetails();
	});
}

void SCSAdminPanel::LoadLog()
{
	UCSAccountSubsystem* Account = GetAccount();
	if (!Account || !Account->IsStaff())
	{
		return;
	}
	const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetNumberField(TEXT("limit"), 150);
	TWeakPtr<SCSAdminPanel> WeakSelf = SharedThis(this);
	Account->AdminCall(TEXT("log"), Params, [WeakSelf](bool bOk, int32 Code, const TSharedPtr<FJsonObject>& Response)
	{
		const TSharedPtr<SCSAdminPanel> Self = WeakSelf.Pin();
		if (!Self.IsValid())
		{
			return;
		}
		Self->LogRows = bOk ? Rows(Response, TEXT("result")) : TArray<TSharedPtr<FJsonObject>>();
		if (!bOk)
		{
			Self->SetStatus(FText::Format(LOCTEXT("LogFailed", "Could not load the log ({0})."), FText::AsNumber(Code)), true);
		}
		Self->RebuildLog();
	});
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void SCSAdminPanel::Act(const FString& Action, TSharedRef<FJsonObject> Params, const FText& Done)
{
	UCSAccountSubsystem* Account = GetAccount();
	if (!Account || bBusy)
	{
		return;
	}
	const FString Reason = GetReason();
	if (Reason.Len() < 3)
	{
		SetStatus(LOCTEXT("NeedReason", "Write a REASON first (at least 3 characters): it goes to the security log."), true);
		return;
	}
	Params->SetStringField(TEXT("reason"), Reason);

	bBusy = true;
	TWeakPtr<SCSAdminPanel> WeakSelf = SharedThis(this);
	Account->AdminCall(Action, Params, [WeakSelf, Done](bool bOk, int32 Code, const TSharedPtr<FJsonObject>& Response)
	{
		const TSharedPtr<SCSAdminPanel> Self = WeakSelf.Pin();
		if (!Self.IsValid())
		{
			return;
		}
		Self->bBusy = false;
		if (!bOk)
		{
			Self->SetStatus(FText::Format(LOCTEXT("ActFailed", "Refused ({0}): {1}"), FText::AsNumber(Code),
				FText::FromString(Str(Response, TEXT("error")))), true);
			return;
		}
		Self->SetStatus(Done, false);
		if (Self->ReasonBox.IsValid())
		{
			Self->ReasonBox->SetText(FText::GetEmpty());
		}
		if (Self->CurrentTab == ETab::Players)
		{
			Self->LoadPlayers();
			Self->LoadPlayerDetails();
		}
		else if (Self->CurrentTab == ETab::Matches)
		{
			Self->LoadMatches();
			Self->LoadMatchDetails();
		}
	});
}

void SCSAdminPanel::ActOnPlayer(const FString& Action, TSharedRef<FJsonObject> Params, const FText& Done)
{
	if (SelectedPlayer.IsEmpty())
	{
		return;
	}
	Params->SetStringField(TEXT("profile_id"), SelectedPlayer);
	Act(Action, Params, Done);
}

void SCSAdminPanel::ActOnMatch(const FString& Action, const FText& Done)
{
	if (SelectedMatch.IsEmpty())
	{
		return;
	}
	const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("match_id"), SelectedMatch);
	Act(Action, Params, Done);
}

#undef LOCTEXT_NAMESPACE
