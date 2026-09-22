// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "UI/SCSProfilePanel.h"

#include "Account/CSAccountSubsystem.h"
#include "Dom/JsonObject.h"
#include "UI/CSUIStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CSProfile"

namespace
{
	/** "2h 14m" / "7m" - playtime reads better than a pile of seconds. */
	FText PlaytimeText(int32 Seconds)
	{
		const int32 Hours = Seconds / 3600;
		const int32 Minutes = (Seconds % 3600) / 60;
		return Hours > 0
			? FText::Format(LOCTEXT("PlayedHM", "{0}h {1}m"), FText::AsNumber(Hours), FText::AsNumber(Minutes))
			: FText::Format(LOCTEXT("PlayedM", "{0}m"), FText::AsNumber(Minutes));
	}

	FText Ratio(float Value)
	{
		return FText::AsNumber(FMath::RoundToFloat(Value * 100.f) / 100.f);
	}

	/** One number with a caption under it. */
	TSharedRef<SWidget> Stat(const FText& Caption, const TAttribute<FText>& Value, const FLinearColor& Color)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(Value).Font(CSUI::Font(26, true)).ColorAndOpacity(Color)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(Caption).Font(CSUI::Font(11, true)).ColorAndOpacity(CSUI::TextDim)
			];
	}
}

void SCSProfilePanel::Construct(const FArguments& InArgs)
{
	WorldContext = InArgs._WorldContext;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			CSUI::MakeHeader(LOCTEXT("Title", "PROFILE"),
				LOCTEXT("Sub", "Your account, your lifetime numbers and the best players. Stats are counted when a match ends; bots and players without an account are left out."))
		]
		+ SVerticalBox::Slot().AutoHeight()[ MakeIdentityCard() ]
		+ SVerticalBox::Slot().AutoHeight()[ CSUI::MakeSectionLabel(LOCTEXT("NameLabel", "NAME")) ]
		+ SVerticalBox::Slot().AutoHeight()[ MakeRenameRow() ]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				CSUI::MakeSectionLabel(LOCTEXT("BoardLabel", "TOP PLAYERS BY KILLS"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(150.f)
				[
					CSUI::MakeButton(LOCTEXT("Refresh", "REFRESH"),
						FOnClicked::CreateLambda([this]() { FetchBoard(); return FReply::Handled(); }),
						CSUI::EButtonKind::Normal,
						TAttribute<bool>::CreateLambda([this]() { return !bLoading; }), 14)
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight()[ MakeBoardHeader() ]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(BoardBox, SVerticalBox)
			]
		]
	];

	RebuildBoard();
}

UCSAccountSubsystem* SCSProfilePanel::GetAccount() const
{
	return WorldContext.IsValid() ? UCSAccountSubsystem::Get(WorldContext.Get()) : nullptr;
}

TSharedRef<SWidget> SCSProfilePanel::MakeIdentityCard()
{
	return SNew(SBorder)
		.BorderImage(CSUI::WhiteBrush())
		.BorderBackgroundColor(CSUI::PanelRaised)
		.Padding(FMargin(20.f, 16.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(LOCTEXT("SignedIn", "SIGNED IN WITH EPIC"))
				.Font(CSUI::Font(11, true)).ColorAndOpacity(CSUI::TextDim)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 14.f)
			[
				SNew(STextBlock).Font(CSUI::Font(30, true)).ColorAndOpacity(CSUI::Text)
				.Text_Lambda([this]()
				{
					const UCSAccountSubsystem* Account = GetAccount();
					return FText::FromString(Account && Account->IsReady() ? Account->GetNickname() : TEXT("-"));
				})
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					Stat(LOCTEXT("CapMatches", "MATCHES"), TAttribute<FText>::CreateLambda([this]()
					{
						const UCSAccountSubsystem* Account = GetAccount();
						return Account ? FText::AsNumber(Account->GetStats().Matches) : FText::GetEmpty();
					}), CSUI::Text)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					Stat(LOCTEXT("CapWins", "WINS"), TAttribute<FText>::CreateLambda([this]()
					{
						const UCSAccountSubsystem* Account = GetAccount();
						return Account ? FText::AsNumber(Account->GetStats().Wins) : FText::GetEmpty();
					}), CSUI::Money)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					Stat(LOCTEXT("CapKills", "KILLS"), TAttribute<FText>::CreateLambda([this]()
					{
						const UCSAccountSubsystem* Account = GetAccount();
						return Account ? FText::AsNumber(Account->GetStats().Kills) : FText::GetEmpty();
					}), CSUI::Accent)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					Stat(LOCTEXT("CapDeaths", "DEATHS"), TAttribute<FText>::CreateLambda([this]()
					{
						const UCSAccountSubsystem* Account = GetAccount();
						return Account ? FText::AsNumber(Account->GetStats().Deaths) : FText::GetEmpty();
					}), CSUI::TextDim)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					Stat(LOCTEXT("CapKD", "K/D"), TAttribute<FText>::CreateLambda([this]()
					{
						const UCSAccountSubsystem* Account = GetAccount();
						return Account ? Ratio(Account->GetStats().KD()) : FText::GetEmpty();
					}), CSUI::Text)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					Stat(LOCTEXT("CapHS", "HEADSHOTS"), TAttribute<FText>::CreateLambda([this]()
					{
						const UCSAccountSubsystem* Account = GetAccount();
						return Account ? FText::AsNumber(Account->GetStats().Headshots) : FText::GetEmpty();
					}), CSUI::Warning)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					Stat(LOCTEXT("CapTime", "PLAYED"), TAttribute<FText>::CreateLambda([this]()
					{
						const UCSAccountSubsystem* Account = GetAccount();
						return Account ? PlaytimeText(Account->GetStats().PlaytimeSeconds) : FText::GetEmpty();
					}), CSUI::Text)
				]
			]
		];
}

TSharedRef<SWidget> SCSProfilePanel::MakeRenameRow()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				CSUI::MakeRow(LOCTEXT("NameRow", "Display name"),
					SAssignNew(NameBox, SEditableTextBox)
					.Font(CSUI::Font(16))
					.HintText(LOCTEXT("NameHint", "3 to 20 characters"))
					.IsEnabled_Lambda([this]()
					{
						const UCSAccountSubsystem* Account = GetAccount();
						return Account && Account->IsReady() && !bRenaming;
					})
					.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Commit)
					{
						if (Commit == ETextCommit::OnEnter)
						{
							CommitNickname();
						}
					}))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
			[
				SNew(SBox).WidthOverride(150.f)
				[
					CSUI::MakeButton(TAttribute<FText>::CreateLambda([this]()
						{
							return bRenaming ? LOCTEXT("Saving", "SAVING...") : LOCTEXT("Save", "SAVE");
						}),
						FOnClicked::CreateLambda([this]() { CommitNickname(); return FReply::Handled(); }),
						CSUI::EButtonKind::Primary,
						TAttribute<bool>::CreateLambda([this]()
						{
							const UCSAccountSubsystem* Account = GetAccount();
							return Account && Account->IsReady() && !bRenaming;
						}), 16)
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(16.f, 4.f, 0.f, 0.f)
		[
			SNew(STextBlock).Font(CSUI::Font(13)).AutoWrapText(true)
			.Text_Lambda([this]()
			{
				if (!RenameStatus.IsEmpty())
				{
					return RenameStatus;
				}
				return LOCTEXT("NameNote", "The name is unique across every player and is what others see in the killfeed and the scoreboard.");
			})
			.ColorAndOpacity_Lambda([this]()
			{
				if (RenameStatus.IsEmpty())
				{
					return FSlateColor(CSUI::TextDim);
				}
				return FSlateColor(bRenameOk ? CSUI::Money : CSUI::Danger);
			})
		];
}

TSharedRef<SWidget> SCSProfilePanel::MakeBoardHeader()
{
	auto Column = [](const FText& Text, float Fill, EHorizontalAlignment Align) -> SHorizontalBox::FSlot::FSlotArguments
	{
		return MoveTemp(SHorizontalBox::Slot().FillWidth(Fill).HAlign(Align)
			[
				SNew(STextBlock).Text(Text).Font(CSUI::Font(12, true)).ColorAndOpacity(CSUI::TextDim)
			]);
	};

	return SNew(SBox).Padding(FMargin(16.f, 6.f, 16.f, 6.f))
		[
			SNew(SHorizontalBox)
			+ Column(LOCTEXT("ColRank", "#"), 0.08f, HAlign_Left)
			+ Column(LOCTEXT("ColName", "PLAYER"), 0.42f, HAlign_Left)
			+ Column(LOCTEXT("ColKills", "KILLS"), 0.12f, HAlign_Right)
			+ Column(LOCTEXT("ColDeaths", "DEATHS"), 0.12f, HAlign_Right)
			+ Column(LOCTEXT("ColKD", "K/D"), 0.12f, HAlign_Right)
			+ Column(LOCTEXT("ColMatches", "MATCHES"), 0.14f, HAlign_Right)
		];
}

void SCSProfilePanel::RebuildBoard()
{
	if (!BoardBox.IsValid())
	{
		return;
	}
	BoardBox->ClearChildren();

	// Nothing to show yet: say which of the three reasons it is.
	if (Rows.Num() == 0)
	{
		const FText Message = bLoading
			? LOCTEXT("BoardLoading", "Loading...")
			: (bLoadFailed
				? LOCTEXT("BoardFailed", "The board could not be loaded. Check the connection and press REFRESH.")
				: LOCTEXT("BoardEmpty", "Nobody has finished a match yet."));

		BoardBox->AddSlot().AutoHeight().Padding(16.f, 18.f)
		[
			SNew(STextBlock).Text(Message).Font(CSUI::Font(15))
			.ColorAndOpacity(bLoadFailed ? FSlateColor(CSUI::Danger) : FSlateColor(CSUI::TextDim))
		];
		return;
	}

	const UCSAccountSubsystem* Account = GetAccount();
	const FString MyId = Account ? Account->GetProfileId() : FString();

	auto Cell = [](const FText& Text, float Fill, EHorizontalAlignment Align, const FLinearColor& Color, bool bBold)
		-> SHorizontalBox::FSlot::FSlotArguments
	{
		return MoveTemp(SHorizontalBox::Slot().FillWidth(Fill).HAlign(Align).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Text).Font(CSUI::Font(16, bBold)).ColorAndOpacity(Color)
			]);
	};

	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const FCSLeaderboardRow& Row = Rows[Index];
		const bool bMe = !MyId.IsEmpty() && Row.Id == MyId;
		// Top three get the accent; your own row is highlighted whatever the rank.
		const FLinearColor NameColor = bMe ? CSUI::Accent : CSUI::Text;

		BoardBox->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SBorder)
			.BorderImage(CSUI::WhiteBrush())
			.BorderBackgroundColor(bMe ? CSUI::Hover : (Index % 2 ? CSUI::Panel : CSUI::PanelRaised))
			.Padding(FMargin(16.f, 9.f))
			[
				SNew(SHorizontalBox)
				+ Cell(FText::AsNumber(Index + 1), 0.08f, HAlign_Left, Index < 3 ? CSUI::Accent : CSUI::TextDim, true)
				+ Cell(FText::FromString(Row.Nickname), 0.42f, HAlign_Left, NameColor, bMe)
				+ Cell(FText::AsNumber(Row.Kills), 0.12f, HAlign_Right, CSUI::Text, false)
				+ Cell(FText::AsNumber(Row.Deaths), 0.12f, HAlign_Right, CSUI::TextDim, false)
				+ Cell(Ratio(Row.KD), 0.12f, HAlign_Right, CSUI::Text, false)
				+ Cell(FText::AsNumber(Row.Matches), 0.14f, HAlign_Right, CSUI::TextDim, false)
			]
		];
	}
}

void SCSProfilePanel::FetchBoard()
{
	UCSAccountSubsystem* Account = GetAccount();
	if (!Account || bLoading)
	{
		return;
	}

	bLoading = true;
	bLoadFailed = false;
	RebuildBoard();

	// The panel can be destroyed while the request is in flight (the player
	// leaves the menu), so the answer is only used if it is still alive.
	TWeakPtr<SCSProfilePanel> WeakSelf = SharedThis(this);
	Account->FetchLeaderboard([WeakSelf](bool bOk, const TArray<TSharedPtr<FJsonObject>>& JsonRows)
	{
		const TSharedPtr<SCSProfilePanel> Self = WeakSelf.Pin();
		if (!Self.IsValid())
		{
			return;
		}

		Self->bLoading = false;
		Self->bLoadFailed = !bOk;
		Self->Rows.Reset();
		for (const TSharedPtr<FJsonObject>& Json : JsonRows)
		{
			FCSLeaderboardRow& Row = Self->Rows.AddDefaulted_GetRef();
			Json->TryGetStringField(TEXT("id"), Row.Id);
			Json->TryGetStringField(TEXT("nickname"), Row.Nickname);
			double Number = 0.0;
			Row.Kills = Json->TryGetNumberField(TEXT("kills"), Number) ? FMath::RoundToInt(Number) : 0;
			Row.Deaths = Json->TryGetNumberField(TEXT("deaths"), Number) ? FMath::RoundToInt(Number) : 0;
			Row.Matches = Json->TryGetNumberField(TEXT("matches"), Number) ? FMath::RoundToInt(Number) : 0;
			Row.Wins = Json->TryGetNumberField(TEXT("wins"), Number) ? FMath::RoundToInt(Number) : 0;
			Row.KD = Json->TryGetNumberField(TEXT("kd"), Number) ? static_cast<float>(Number) : 0.f;
		}
		Self->RebuildBoard();
	});
}

void SCSProfilePanel::CommitNickname()
{
	UCSAccountSubsystem* Account = GetAccount();
	if (!Account || !NameBox.IsValid() || bRenaming)
	{
		return;
	}

	const FString Wanted = NameBox->GetText().ToString().TrimStartAndEnd();
	if (Wanted == Account->GetNickname())
	{
		RenameStatus = LOCTEXT("SameName", "That is already your name.");
		bRenameOk = false;
		return;
	}

	bRenaming = true;
	RenameStatus = FText::GetEmpty();

	TWeakPtr<SCSProfilePanel> WeakSelf = SharedThis(this);
	Account->SetNickname(Wanted, [WeakSelf](bool bOk, const FString& Error)
	{
		const TSharedPtr<SCSProfilePanel> Self = WeakSelf.Pin();
		if (!Self.IsValid())
		{
			return;
		}
		Self->bRenaming = false;
		Self->bRenameOk = bOk;
		Self->RenameStatus = bOk
			? LOCTEXT("Renamed", "Saved.")
			: FText::Format(LOCTEXT("RenameFailed", "Not saved: {0}"), FText::FromString(Error));
		if (bOk)
		{
			// The board shows the old name until it is read again.
			Self->FetchBoard();
		}
	});
}

void SCSProfilePanel::RequestRename(const FString& NewName)
{
	if (NameBox.IsValid())
	{
		NameBox->SetText(FText::FromString(NewName));
	}
	CommitNickname();
}

void SCSProfilePanel::Refresh()
{
	RenameStatus = FText::GetEmpty();
	if (const UCSAccountSubsystem* Account = GetAccount())
	{
		if (NameBox.IsValid())
		{
			NameBox->SetText(FText::FromString(Account->GetNickname()));
		}
	}
	FetchBoard();
}

#undef LOCTEXT_NAMESPACE
