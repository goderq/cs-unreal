// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "UI/SCSAdminPanel.h"

#include "Account/CSAccountSubsystem.h"
#include "Dom/JsonObject.h"
#include "UI/CSUIStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CSAdmin"

namespace
{
	/** "2026-09-23T12:40:11.123+00:00" -> "2026-09-23 12:40". */
	FString ShortDate(const FString& Iso)
	{
		if (Iso.Len() < 16)
		{
			return Iso;
		}
		return Iso.Left(10) + TEXT(" ") + Iso.Mid(11, 5);
	}

	int32 NumberField(const TSharedPtr<FJsonObject>& Json, const TCHAR* Name)
	{
		double Value = 0.0;
		return (Json.IsValid() && Json->TryGetNumberField(Name, Value)) ? FMath::RoundToInt(Value) : 0;
	}
}

bool FCSAdminPlayerRow::IsBanned() const
{
	FDateTime Until;
	return !BannedUntil.IsEmpty() && FDateTime::ParseIso8601(*BannedUntil, Until) && Until > FDateTime::UtcNow();
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
				LOCTEXT("Sub", "Players, bans and admin rights. Every action is checked by the server and written to the journal."))
		]

		// Search.
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SAssignNew(SearchBox, SEditableTextBox)
				.Font(CSUI::Font(16))
				.HintText(LOCTEXT("SearchHint", "Search by nickname (empty = recently seen)"))
				.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Commit)
				{
					if (Commit == ETextCommit::OnEnter)
					{
						LoadPlayers();
					}
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(10.f, 0.f, 0.f, 0.f)
			[
				SNew(SBox).WidthOverride(150.f)
				[
					CSUI::MakeButton(LOCTEXT("Search", "SEARCH"), FOnClicked::CreateLambda([this]() { LoadPlayers(); return FReply::Handled(); }),
						CSUI::EButtonKind::Primary, TAttribute<bool>::CreateLambda([this]() { return !bBusy; }), 15)
				]
			]
		]

		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.55f).Padding(0.f, 0.f, 16.f, 0.f)
			[
				MakePlayerList()
			]
			+ SHorizontalBox::Slot().FillWidth(0.45f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ MakeDetails() ]
				+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 14.f, 0.f, 0.f)[ MakeJournal() ]
			]
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

TSharedRef<SWidget> SCSAdminPanel::MakePlayerList()
{
	auto Column = [](const FText& Text, float Fill) -> SHorizontalBox::FSlot::FSlotArguments
	{
		return MoveTemp(SHorizontalBox::Slot().FillWidth(Fill)
			[
				SNew(STextBlock).Text(Text).Font(CSUI::Font(12, true)).ColorAndOpacity(CSUI::TextDim)
			]);
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(12.f, 0.f, 12.f, 6.f)
		[
			SNew(SHorizontalBox)
			+ Column(LOCTEXT("ColPlayer", "PLAYER"), 0.4f)
			+ Column(LOCTEXT("ColKD", "K / D"), 0.18f)
			+ Column(LOCTEXT("ColMatches", "MATCHES"), 0.14f)
			+ Column(LOCTEXT("ColSeen", "LAST SEEN"), 0.28f)
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(PlayersBox, SVerticalBox)
			]
		];
}

TSharedRef<SWidget> SCSAdminPanel::MakeDetails()
{
	auto Action = [this](const TAttribute<FText>& Label, CSUI::EButtonKind Kind, TFunction<void()> OnClick) -> TSharedRef<SWidget>
	{
		return SNew(SBox).HeightOverride(40.f)
			[
				CSUI::MakeButton(Label, FOnClicked::CreateLambda([OnClick]() { OnClick(); return FReply::Handled(); }), Kind,
					TAttribute<bool>::CreateLambda([this]() { return !bBusy && GetSelected() != nullptr; }), 14)
			];
	};
	auto Ban = [this](int32 Hours, const FText& Done)
	{
		const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("profile_id"), SelectedId);
		Params->SetNumberField(TEXT("hours"), Hours);
		Act(TEXT("ban"), Params, Done);
	};

	return SNew(SBorder)
		.BorderImage(CSUI::WhiteBrush())
		.BorderBackgroundColor(CSUI::PanelRaised)
		.Padding(FMargin(18.f, 14.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Font(CSUI::Font(24, true)).ColorAndOpacity(CSUI::Text)
				.Text_Lambda([this]()
				{
					const FCSAdminPlayerRow* Row = GetSelected();
					return Row ? FText::FromString(Row->Nickname) : LOCTEXT("NoneSelected", "Pick a player on the left");
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 12.f)
			[
				SNew(STextBlock).Font(CSUI::Font(13)).ColorAndOpacity(CSUI::TextDim).AutoWrapText(true)
				.Text_Lambda([this]()
				{
					const FCSAdminPlayerRow* Row = GetSelected();
					if (!Row)
					{
						return FText::GetEmpty();
					}
					return FText::Format(LOCTEXT("Details", "{0} kills, {1} deaths, {2} matches  -  {3}{4}"),
						FText::AsNumber(Row->Kills), FText::AsNumber(Row->Deaths), FText::AsNumber(Row->Matches),
						Row->bAdmin ? LOCTEXT("IsAdmin", "ADMIN  ") : FText::GetEmpty(),
						Row->IsBanned() ? FText::Format(LOCTEXT("BannedUntil", "banned until {0}"), FText::FromString(ShortDate(Row->BannedUntil)))
							: LOCTEXT("NotBanned", "not banned"));
				})
			]

			+ SVerticalBox::Slot().AutoHeight()[ CSUI::MakeSectionLabel(LOCTEXT("BanLabel", "BAN")) ]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 6.f, 0.f)[ Action(LOCTEXT("Ban1h", "1 HOUR"), CSUI::EButtonKind::Normal, [Ban]() { Ban(1, LOCTEXT("Banned1h", "Banned for an hour.")); }) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 6.f, 0.f)[ Action(LOCTEXT("Ban1d", "1 DAY"), CSUI::EButtonKind::Normal, [Ban]() { Ban(24, LOCTEXT("Banned1d", "Banned for a day.")); }) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 6.f, 0.f)[ Action(LOCTEXT("Ban7d", "7 DAYS"), CSUI::EButtonKind::Normal, [Ban]() { Ban(24 * 7, LOCTEXT("Banned7d", "Banned for a week.")); }) ]
				+ SHorizontalBox::Slot().FillWidth(1.f)[ Action(LOCTEXT("BanForever", "FOREVER"), CSUI::EButtonKind::Danger, [Ban]() { Ban(0, LOCTEXT("BannedForever", "Banned permanently.")); }) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
			[
				Action(LOCTEXT("Unban", "UNBAN"), CSUI::EButtonKind::Normal, [this]()
				{
					const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
					Params->SetStringField(TEXT("profile_id"), SelectedId);
					Act(TEXT("unban"), Params, LOCTEXT("Unbanned", "Unbanned."));
				})
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[ CSUI::MakeSectionLabel(LOCTEXT("RightsLabel", "RIGHTS AND NAME")) ]
			+ SVerticalBox::Slot().AutoHeight()
			[
				Action(TAttribute<FText>::CreateLambda([this]()
					{
						const FCSAdminPlayerRow* Row = GetSelected();
						return (Row && Row->bAdmin) ? LOCTEXT("RemoveAdmin", "REMOVE ADMIN RIGHTS") : LOCTEXT("MakeAdmin", "MAKE ADMIN");
					}), CSUI::EButtonKind::Normal, [this]()
				{
					const FCSAdminPlayerRow* Row = GetSelected();
					if (!Row)
					{
						return;
					}
					const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
					Params->SetStringField(TEXT("profile_id"), SelectedId);
					Params->SetBoolField(TEXT("is_admin"), !Row->bAdmin);
					Act(TEXT("set_admin"), Params, Row->bAdmin ? LOCTEXT("AdminRemoved", "Admin rights removed.") : LOCTEXT("AdminGiven", "Now an admin."));
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
				[
					SAssignNew(RenameBox, SEditableTextBox).Font(CSUI::Font(15)).HintText(LOCTEXT("RenameHint", "New nickname"))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(130.f)
					[
						Action(LOCTEXT("Rename", "RENAME"), CSUI::EButtonKind::Normal, [this]()
						{
							const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
							Params->SetStringField(TEXT("profile_id"), SelectedId);
							Params->SetStringField(TEXT("nickname"), RenameBox.IsValid() ? RenameBox->GetText().ToString() : FString());
							Act(TEXT("rename"), Params, LOCTEXT("Renamed", "Renamed."));
						})
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
			[
				Action(LOCTEXT("ResetStats", "RESET STATS (click twice)"), CSUI::EButtonKind::Danger, [this]()
				{
					const double Now = FPlatformTime::Seconds();
					if (Now > ResetArmedUntil)
					{
						ResetArmedUntil = Now + 4.0;
						Status = LOCTEXT("ResetArmed", "Click again within 4 seconds to reset this player's stats.");
						bStatusError = true;
						return;
					}
					ResetArmedUntil = 0.0;
					const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
					Params->SetStringField(TEXT("profile_id"), SelectedId);
					Act(TEXT("reset_stats"), Params, LOCTEXT("StatsReset", "Stats reset."));
				})
			]
		];
}

TSharedRef<SWidget> SCSAdminPanel::MakeJournal()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ CSUI::MakeSectionLabel(LOCTEXT("JournalLabel", "JOURNAL")) ]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(JournalBox, SVerticalBox)
			]
		];
}

const FCSAdminPlayerRow* SCSAdminPanel::GetSelected() const
{
	return Players.FindByPredicate([this](const FCSAdminPlayerRow& Row) { return Row.Id == SelectedId; });
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
			SNew(STextBlock).Font(CSUI::Font(15)).ColorAndOpacity(CSUI::TextDim)
			.Text(bBusy ? LOCTEXT("Loading", "Loading...") : LOCTEXT("NoPlayers", "No players found."))
		];
		return;
	}

	for (const FCSAdminPlayerRow& Row : Players)
	{
		const FString Id = Row.Id;
		const FLinearColor NameColor = Row.IsBanned() ? CSUI::Danger : (Row.bAdmin ? CSUI::Accent : CSUI::Text);
		PlayersBox->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SButton).IsFocusable(false)
			.ButtonStyle(&CSUI::ButtonStyle(CSUI::EButtonKind::Nav))
			.ContentPadding(FMargin(12.f, 8.f))
			.OnClicked_Lambda([this, Id]()
			{
				SelectedId = Id;
				if (const FCSAdminPlayerRow* Picked = GetSelected(); Picked && RenameBox.IsValid())
				{
					RenameBox->SetText(FText::FromString(Picked->Nickname));
				}
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.4f)
				[
					SNew(STextBlock).Font(CSUI::Font(15, true))
					.Text(FText::FromString(Row.Nickname + (Row.bAdmin ? TEXT("  [A]") : TEXT("")) + (Row.IsBanned() ? TEXT("  [BAN]") : TEXT(""))))
					.ColorAndOpacity_Lambda([this, Id, NameColor]() { return FSlateColor(SelectedId == Id ? CSUI::Accent : NameColor); })
				]
				+ SHorizontalBox::Slot().FillWidth(0.18f)
				[
					SNew(STextBlock).Font(CSUI::Font(14)).ColorAndOpacity(CSUI::Text)
					.Text(FText::FromString(FString::Printf(TEXT("%d / %d"), Row.Kills, Row.Deaths)))
				]
				+ SHorizontalBox::Slot().FillWidth(0.14f)
				[
					SNew(STextBlock).Font(CSUI::Font(14)).ColorAndOpacity(CSUI::TextDim).Text(FText::AsNumber(Row.Matches))
				]
				+ SHorizontalBox::Slot().FillWidth(0.28f)
				[
					SNew(STextBlock).Font(CSUI::Font(13)).ColorAndOpacity(CSUI::TextDim).Text(FText::FromString(ShortDate(Row.LastSeen)))
				]
			]
		];
	}
}

void SCSAdminPanel::RebuildJournal()
{
	if (!JournalBox.IsValid())
	{
		return;
	}
	JournalBox->ClearChildren();
	for (const FString& Line : Journal)
	{
		JournalBox->AddSlot().AutoHeight().Padding(2.f, 2.f)
		[
			SNew(STextBlock).Font(CSUI::Font(12)).ColorAndOpacity(CSUI::TextDim).AutoWrapText(true).Text(FText::FromString(Line))
		];
	}
}

void SCSAdminPanel::LoadPlayers()
{
	UCSAccountSubsystem* Account = GetAccount();
	if (!Account || !Account->IsAdmin())
	{
		Status = LOCTEXT("NotAdmin", "This account has no admin rights.");
		bStatusError = true;
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
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (bOk && Response.IsValid() && Response->TryGetArrayField(TEXT("players"), List))
		{
			for (const TSharedPtr<FJsonValue>& Value : *List)
			{
				const TSharedPtr<FJsonObject> Json = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!Json.IsValid())
				{
					continue;
				}
				FCSAdminPlayerRow& Row = Self->Players.AddDefaulted_GetRef();
				Json->TryGetStringField(TEXT("id"), Row.Id);
				Json->TryGetStringField(TEXT("nickname"), Row.Nickname);
				Json->TryGetStringField(TEXT("last_seen_at"), Row.LastSeen);
				Json->TryGetStringField(TEXT("banned_until"), Row.BannedUntil);
				Json->TryGetBoolField(TEXT("is_admin"), Row.bAdmin);
				// One-to-one relation: PostgREST sends an object (older versions an array).
				TSharedPtr<FJsonObject> Stats;
				const TSharedPtr<FJsonObject>* StatsObject = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* StatsArray = nullptr;
				if (Json->TryGetObjectField(TEXT("player_stats"), StatsObject))
				{
					Stats = *StatsObject;
				}
				else if (Json->TryGetArrayField(TEXT("player_stats"), StatsArray) && StatsArray->Num() > 0)
				{
					Stats = (*StatsArray)[0]->AsObject();
				}
				Row.Matches = NumberField(Stats, TEXT("matches"));
				Row.Kills = NumberField(Stats, TEXT("kills"));
				Row.Deaths = NumberField(Stats, TEXT("deaths"));
			}
			Self->Status = FText::Format(LOCTEXT("Loaded", "{0} player(s)."), FText::AsNumber(Self->Players.Num()));
			Self->bStatusError = false;
		}
		else
		{
			Self->Status = FText::Format(LOCTEXT("LoadFailed", "Could not load the players (server answered {0})."), FText::AsNumber(Code));
			Self->bStatusError = true;
		}
		Self->RebuildPlayers();
	});
}

void SCSAdminPanel::LoadJournal()
{
	UCSAccountSubsystem* Account = GetAccount();
	if (!Account || !Account->IsAdmin())
	{
		return;
	}
	const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetNumberField(TEXT("limit"), 40);
	TWeakPtr<SCSAdminPanel> WeakSelf = SharedThis(this);
	Account->AdminCall(TEXT("log"), Params, [WeakSelf](bool bOk, int32, const TSharedPtr<FJsonObject>& Response)
	{
		const TSharedPtr<SCSAdminPanel> Self = WeakSelf.Pin();
		if (!Self.IsValid())
		{
			return;
		}
		Self->Journal.Reset();
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (bOk && Response.IsValid() && Response->TryGetArrayField(TEXT("log"), List))
		{
			for (const TSharedPtr<FJsonValue>& Value : *List)
			{
				const TSharedPtr<FJsonObject> Json = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!Json.IsValid())
				{
					continue;
				}
				FString When;
				FString Action;
				FString AdminName;
				FString TargetName;
				Json->TryGetStringField(TEXT("created_at"), When);
				Json->TryGetStringField(TEXT("action"), Action);
				const TSharedPtr<FJsonObject>* AdminJson = nullptr;
				if (Json->TryGetObjectField(TEXT("admin"), AdminJson))
				{
					(*AdminJson)->TryGetStringField(TEXT("nickname"), AdminName);
				}
				const TSharedPtr<FJsonObject>* TargetJson = nullptr;
				if (Json->TryGetObjectField(TEXT("target"), TargetJson))
				{
					(*TargetJson)->TryGetStringField(TEXT("nickname"), TargetName);
				}
				Self->Journal.Add(FString::Printf(TEXT("%s  %s: %s %s"), *ShortDate(When), *AdminName, *Action, *TargetName));
			}
		}
		Self->RebuildJournal();
	});
}

void SCSAdminPanel::Act(const FString& Action, TSharedRef<FJsonObject> Params, const FText& Done)
{
	UCSAccountSubsystem* Account = GetAccount();
	if (!Account || SelectedId.IsEmpty() || bBusy)
	{
		return;
	}
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
		FString Error;
		if (Response.IsValid())
		{
			Response->TryGetStringField(TEXT("error"), Error);
		}
		Self->Status = bOk ? Done : FText::Format(LOCTEXT("ActFailed", "Refused ({0}): {1}"), FText::AsNumber(Code), FText::FromString(Error));
		Self->bStatusError = !bOk;
		if (bOk)
		{
			Self->LoadPlayers();
			Self->LoadJournal();
		}
	});
}

void SCSAdminPanel::Refresh()
{
	LoadPlayers();
	LoadJournal();
}

#undef LOCTEXT_NAMESPACE
