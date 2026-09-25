// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "UI/SCSMainMenu.h"

#include "Core/CSLog.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/ConfigCacheIni.h"
#include "Account/CSAccountSubsystem.h"
#include "Core/CSModeSettings.h"
#include "UI/CSUIStyle.h"
#include "UI/SCSSettingsPanel.h"
#include "UI/SCSAdminPanel.h"
#include "UI/SCSProfilePanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CSMainMenu"

namespace
{
	/** Photon Cloud region codes offered in the menu. Empty = best ping. */
	const TCHAR* GRegionCodes[] = { TEXT(""), TEXT("eu"), TEXT("us"), TEXT("usw"), TEXT("ru"), TEXT("asia"), TEXT("jp"), TEXT("kr"), TEXT("sa"), TEXT("au") };
	const TCHAR* GRegionNames[] = { TEXT("Default (Europe)"), TEXT("Europe"), TEXT("USA East"), TEXT("USA West"), TEXT("Russia"), TEXT("Asia"), TEXT("Japan"), TEXT("Korea"), TEXT("South America"), TEXT("Australia") };

	constexpr int32 GMinPlayers = 2;
	constexpr int32 GMaxPlayers = 16;

	FString ProjectVersion()
	{
		FString Version;
		GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), Version, GGameIni);
		return Version.IsEmpty() ? TEXT("dev") : Version;
	}
}

void SCSMainMenu::Construct(const FArguments& InArgs)
{
	WorldContext = InArgs._WorldContext;

	ChildSlot
	[
		SNew(SOverlay)

		// Backdrop: flat, dark, with a thin accent strip.
		+ SOverlay::Slot()
		[
			SNew(SImage).Image(CSUI::WhiteBrush()).ColorAndOpacity(CSUI::Backdrop)
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Fill)
		[
			SNew(SBox).WidthOverride(4.f)
			[
				SNew(SImage).Image(CSUI::WhiteBrush()).ColorAndOpacity(CSUI::Accent)
			]
		]

		+ SOverlay::Slot().Padding(FMargin(64.f, 56.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(330.f)
				[
					MakeNav()
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(48.f, 0.f, 0.f, 0.f))
			[
				SNew(SBorder)
				.BorderImage(CSUI::WhiteBrush())
				.BorderBackgroundColor(CSUI::Panel)
				.Padding(FMargin(40.f, 36.f))
				[
					SAssignNew(Switcher, SWidgetSwitcher)
					+ SWidgetSwitcher::Slot()[ MakeHomePage() ]
					+ SWidgetSwitcher::Slot()[ MakePlayPage() ]
					+ SWidgetSwitcher::Slot()[ MakeCreatePage() ]
					+ SWidgetSwitcher::Slot()[ MakeJoinPage() ]
					+ SWidgetSwitcher::Slot()[ MakeBrowserPage() ]
					+ SWidgetSwitcher::Slot()
					[
						SAssignNew(SettingsPanel, SCSSettingsPanel)
						.WorldContext(WorldContext)
						.OnClose_Lambda([this]() { ShowPage(EPage::Home); })
					]
					+ SWidgetSwitcher::Slot()
					[
						SAssignNew(ProfilePanel, SCSProfilePanel)
						.WorldContext(WorldContext)
					]
					+ SWidgetSwitcher::Slot()
					[
						SAssignNew(AdminPanel, SCSAdminPanel)
						.WorldContext(WorldContext)
					]
				]
			]
		]

		+ SOverlay::Slot()
		[
			MakeBusyOverlay()
		]
	];

	if (UCSSessionSubsystem* Session = GetSession())
	{
		RoomListHandle = Session->OnRoomListChanged.AddSP(this, &SCSMainMenu::RebuildRoomList);
	}
	RebuildRoomList();
}

SCSMainMenu::~SCSMainMenu()
{
	if (UCSSessionSubsystem* Session = GetSession())
	{
		Session->OnRoomListChanged.Remove(RoomListHandle);
	}
}

UCSSessionSubsystem* SCSMainMenu::GetSession() const
{
	const UWorld* World = WorldContext.IsValid() ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UCSSessionSubsystem>() : nullptr;
}

FString SCSMainMenu::GetSelectedRegion() const
{
	const int32 Index = RegionSelector.IsValid() ? RegionSelector->GetSelectedIndex() : 0;
	return GRegionCodes[FMath::Clamp(Index, 0, static_cast<int32>(UE_ARRAY_COUNT(GRegionCodes)) - 1)];
}

FCSSessionRequest SCSMainMenu::MakeRequest() const
{
	FCSSessionRequest Request;
	Request.Region = GetSelectedRegion();
	Request.bSelectRegion = !Request.Region.IsEmpty();
	Request.Mode = SelectedMode;
	Request.MapId = GetSelectedMap();
	Request.BotCount = GetSelectedBotCount();
	Request.BotDifficulty = GetSelectedBotDifficulty();
	return Request;
}

int32 SCSMainMenu::GetSelectedBotCount() const
{
	return BotCountSelector.IsValid() ? BotCountSelector->GetSelectedIndex() : 3;
}

ECSBotDifficulty SCSMainMenu::GetSelectedBotDifficulty() const
{
	const int32 Index = BotDifficultySelector.IsValid() ? BotDifficultySelector->GetSelectedIndex() : 1;
	return static_cast<ECSBotDifficulty>(FMath::Clamp(Index, 0, 2));
}

void SCSMainMenu::StartPractice()
{
	LocalMessage.Reset();
	if (UCSSessionSubsystem* Session = GetSession())
	{
		UE_LOG(LogCSNet, Log, TEXT("Menu: offline practice with %d bot(s)."), GetSelectedBotCount());
		Session->StartOfflinePractice(FMath::Max(1, GetSelectedBotCount()), GetSelectedBotDifficulty(), SelectedMode, GetSelectedMap());
	}
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SCSMainMenu::MakeNav()
{
	auto Nav = [this](const FText& Label, EPage Page, bool bSub = false) -> TSharedRef<SWidget>
	{
		return SNew(SBox).Padding(FMargin(bSub ? 18.f : 0.f, 2.f, 0.f, 2.f))
			[
				SNew(SButton).IsFocusable(false)
				.ButtonStyle(&CSUI::ButtonStyle(CSUI::EButtonKind::Nav))
				.ContentPadding(FMargin(16.f, bSub ? 7.f : 12.f))
				.OnClicked_Lambda([this, Page]() { ShowPage(Page); return FReply::Handled(); })
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
					[
						SNew(SBox).WidthOverride(3.f).HeightOverride(bSub ? 16.f : 24.f)
						[
							SNew(SImage).Image(CSUI::WhiteBrush())
							.ColorAndOpacity_Lambda([this, Page]()
							{
								const bool bActive = CurrentPage == Page
									|| (Page == EPage::Play && (CurrentPage == EPage::Create || CurrentPage == EPage::Join || CurrentPage == EPage::Browser));
								return FSlateColor(bActive ? CSUI::Accent : FLinearColor::Transparent);
							})
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(Label).Font(CSUI::Font(bSub ? 16 : 24, !bSub))
						.ColorAndOpacity_Lambda([this, Page]() { return FSlateColor(CurrentPage == Page ? CSUI::Text : CSUI::TextDim); })
					]
				]
			];
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock).Text(LOCTEXT("GameTitleA", "CS ")).Font(CSUI::Font(46, true)).ColorAndOpacity(CSUI::Text)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock).Text(LOCTEXT("GameTitleB", "FUSION")).Font(CSUI::Font(46, true)).ColorAndOpacity(CSUI::Accent)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 18.f)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("Version", "multiplayer fps  -  v{0}"), FText::FromString(ProjectVersion())))
			.Font(CSUI::Font(13)).ColorAndOpacity(CSUI::TextDim)
		]

		// v1.2: who is signed in, and their lifetime numbers.
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 26.f)
		[
			SNew(SButton).IsFocusable(false)
			.ButtonStyle(&CSUI::ButtonStyle(CSUI::EButtonKind::Normal))
			.ContentPadding(FMargin(16.f, 12.f))
			.ToolTipText(LOCTEXT("ProfileTip", "Profile: your numbers and the top players"))
			.OnClicked_Lambda([this]() { ShowPage(EPage::Profile); return FReply::Handled(); })
			.Visibility_Lambda([this]()
			{
				const UCSAccountSubsystem* Account = UCSAccountSubsystem::Get(WorldContext.Get());
				return (Account && Account->IsReady()) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(CSUI::Font(11, true)).ColorAndOpacity(CSUI::TextDim)
					.Text(LOCTEXT("SignedIn", "SIGNED IN"))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(CSUI::Font(20, true)).ColorAndOpacity(CSUI::Text)
					.Text_Lambda([this]()
					{
						const UCSAccountSubsystem* Account = UCSAccountSubsystem::Get(WorldContext.Get());
						return FText::FromString(Account ? Account->GetNickname() : FString());
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(STextBlock).Font(CSUI::Font(12)).ColorAndOpacity(CSUI::TextDim)
					.Text_Lambda([this]()
					{
						const UCSAccountSubsystem* Account = UCSAccountSubsystem::Get(WorldContext.Get());
						if (!Account)
						{
							return FText::GetEmpty();
						}
						const FCSAccountStats& Stats = Account->GetStats();
						return FText::Format(LOCTEXT("StatsLine", "{0} matches  -  {1} wins  -  {2} / {3} K/D {4}"),
							FText::AsNumber(Stats.Matches), FText::AsNumber(Stats.Wins),
							FText::AsNumber(Stats.Kills), FText::AsNumber(Stats.Deaths),
							FText::AsNumber(FMath::RoundToFloat(Stats.KD() * 100.f) / 100.f));
					})
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight()[ Nav(LOCTEXT("Play", "PLAY"), EPage::Play) ]
		+ SVerticalBox::Slot().AutoHeight()[ Nav(LOCTEXT("QuickNav", "Quick Match"), EPage::Play, true) ]
		+ SVerticalBox::Slot().AutoHeight()[ Nav(LOCTEXT("CreateNav", "Create Session"), EPage::Create, true) ]
		+ SVerticalBox::Slot().AutoHeight()[ Nav(LOCTEXT("JoinNav", "Join Session"), EPage::Join, true) ]
		+ SVerticalBox::Slot().AutoHeight()[ Nav(LOCTEXT("BrowserNav", "Session Browser"), EPage::Browser, true) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[ Nav(LOCTEXT("Profile", "PROFILE"), EPage::Profile) ]
		+ SVerticalBox::Slot().AutoHeight()[ Nav(LOCTEXT("Settings", "SETTINGS"), EPage::Settings) ]
		// v2.0: only for profiles with admin rights.
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([this]()
			{
				const UCSAccountSubsystem* Account = UCSAccountSubsystem::Get(WorldContext.Get());
				return (Account && Account->IsAdmin()) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				Nav(LOCTEXT("Admin", "ADMIN"), EPage::Admin)
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SButton).IsFocusable(false)
			.ButtonStyle(&CSUI::ButtonStyle(CSUI::EButtonKind::Nav))
			.ContentPadding(FMargin(31.f, 12.f, 16.f, 12.f))
			.OnClicked_Lambda([this]() { Quit(); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(LOCTEXT("Quit", "QUIT")).Font(CSUI::Font(24, true)).ColorAndOpacity(CSUI::TextDim)
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)[ SNew(SBox) ]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(this, &SCSMainMenu::GetStatusText)
			.Font(CSUI::Font(13))
			.ColorAndOpacity_Lambda([this]()
			{
				const UCSSessionSubsystem* Session = GetSession();
				return FSlateColor(Session && !Session->GetLastError().IsEmpty() ? CSUI::Danger : CSUI::TextDim);
			})
			.AutoWrapText(true)
		];
}

TSharedRef<SWidget> SCSMainMenu::MakeHomePage()
{
	auto Line = [](const FText& Key, const FText& Action) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 3.f)
			[
				SNew(SBox).WidthOverride(170.f)
				[
					SNew(STextBlock).Text(Key).Font(CSUI::Font(15, true)).ColorAndOpacity(CSUI::Accent)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 3.f)
			[
				SNew(STextBlock).Text(Action).Font(CSUI::Font(15)).ColorAndOpacity(CSUI::Text)
			];
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			CSUI::MakeHeader(LOCTEXT("Welcome", "WELCOME"),
				LOCTEXT("WelcomeBody", "Three modes - Deathmatch, Team Deathmatch and 5 vs 5 - on three maps. Earn money for kills and rounds and spend it in the shop. Weapons are bought, not found; whatever a player carried drops when they die."))
		]
		+ SVerticalBox::Slot().AutoHeight()[ CSUI::MakeSectionLabel(LOCTEXT("ControlsLabel", "CONTROLS")) ]
		+ SVerticalBox::Slot().AutoHeight()[ Line(LOCTEXT("KMove", "W A S D"), LOCTEXT("AMove", "Move  -  Space jump, Shift sprint, Ctrl crouch")) ]
		+ SVerticalBox::Slot().AutoHeight()[ Line(LOCTEXT("KFire", "LMB / RMB"), LOCTEXT("AFire", "Fire / aim")) ]
		+ SVerticalBox::Slot().AutoHeight()[ Line(LOCTEXT("KReload", "R"), LOCTEXT("AReload", "Reload")) ]
		+ SVerticalBox::Slot().AutoHeight()[ Line(LOCTEXT("KPick", "E / G"), LOCTEXT("APick", "Pick up / drop the weapon in hand")) ]
		+ SVerticalBox::Slot().AutoHeight()[ Line(LOCTEXT("KSlots", "1 - 7"), LOCTEXT("ASlots", "1 pistol, 2-7 inventory: weapons and grenades are held, medkit / armor used")) ]
		+ SVerticalBox::Slot().AutoHeight()[ Line(LOCTEXT("KShop", "B"), LOCTEXT("AShop", "Shop - while spawn-protected (DM / TDM) or during buy time (5 vs 5)")) ]
		+ SVerticalBox::Slot().AutoHeight()[ Line(LOCTEXT("KTab", "TAB  /  I"), LOCTEXT("ATab", "Scoreboard (hold)  /  inventory")) ]
		+ SVerticalBox::Slot().AutoHeight()[ Line(LOCTEXT("KEsc", "ESC"), LOCTEXT("AEsc", "Menu  -  resume, settings, leave match")) ]
		+ SVerticalBox::Slot().FillHeight(1.f)[ SNew(SBox) ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
		[
			SNew(SBox).WidthOverride(320.f)
			[
				CSUI::MakeButton(LOCTEXT("PlayNow", "PLAY"), FOnClicked::CreateLambda([this]() { ShowPage(EPage::Play); return FReply::Handled(); }),
					CSUI::EButtonKind::Primary, true, 22)
			]
		];
}

TSharedRef<SWidget> SCSMainMenu::MakeChoiceTile(const FText& Title, const FText& TagText, const FText& Body, const FLinearColor& Color,
	TFunction<bool()> IsSelected, TFunction<void()> OnPick, float Height)
{
	return SNew(SBox).HeightOverride(Height)
		[
			SNew(SButton).IsFocusable(false)
			.ButtonStyle(&CSUI::ButtonStyle(CSUI::EButtonKind::Normal))
			.ContentPadding(FMargin(0.f))
			.OnClicked_Lambda([OnPick]() { OnPick(); return FReply::Handled(); })
			[
				SNew(SOverlay)
				// Tint of the tile's colour; stronger when chosen.
				+ SOverlay::Slot()
				[
					SNew(SImage).Image(CSUI::WhiteBrush())
					.ColorAndOpacity_Lambda([IsSelected, Color]() { return FSlateColor(FLinearColor(Color.R, Color.G, Color.B, IsSelected() ? 0.22f : 0.06f)); })
				]
				+ SOverlay::Slot().VAlign(VAlign_Top)
				[
					SNew(SBox).HeightOverride(4.f)
					[
						SNew(SImage).Image(CSUI::WhiteBrush())
						.ColorAndOpacity_Lambda([IsSelected, Color]() { return FSlateColor(IsSelected() ? Color : FLinearColor(1.f, 1.f, 1.f, 0.08f)); })
					]
				]
				+ SOverlay::Slot().Padding(FMargin(18.f, 16.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(TagText).Font(CSUI::Font(11, true))
						.ColorAndOpacity_Lambda([IsSelected, Color]() { return FSlateColor(IsSelected() ? Color : CSUI::TextDim); })
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 6.f)
					[
						SNew(STextBlock).Text(Title).Font(CSUI::Font(21, true)).ColorAndOpacity(CSUI::Text)
					]
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SNew(STextBlock).Text(Body).Font(CSUI::Font(12)).ColorAndOpacity(CSUI::TextDim).AutoWrapText(true)
					]
				]
				+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(FMargin(0.f, 14.f, 14.f, 0.f))
				[
					SNew(STextBlock).Text(LOCTEXT("Chosen", "SELECTED")).Font(CSUI::Font(10, true)).ColorAndOpacity(Color)
					.Visibility_Lambda([IsSelected]() { return IsSelected() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				]
			]
		];
}

FName SCSMainMenu::GetSelectedMap() const
{
	const TArray<FCSMapInfo>& Maps = UCSModeSettings::Get()->Maps;
	return Maps.IsValidIndex(SelectedMapIndex) ? Maps[SelectedMapIndex].Id : NAME_None;
}

void SCSMainMenu::SelectMap(int32 Index)
{
	SelectedMapIndex = FMath::Clamp(Index, 0, FMath::Max(0, UCSModeSettings::Get()->Maps.Num() - 1));
}

TSharedRef<SWidget> SCSMainMenu::MakePlayPage()
{
	TArray<FText> Regions;
	for (const TCHAR* Name : GRegionNames)
	{
		Regions.Add(FText::FromString(Name));
	}

	// --- Modes ---
	struct FModeCard { ECSGameModeType Mode; FText Tag; FText Body; FLinearColor Color; };
	const FModeCard Modes[] = {
		{ ECSGameModeType::Deathmatch, LOCTEXT("DMTag", "FREE FOR ALL"),
			LOCTEXT("DMBody", "Everyone against everyone. Respawn with 7 s of protection and a shop. First to 30 kills."), CSUI::Accent },
		{ ECSGameModeType::TeamDeathmatch, LOCTEXT("TDMTag", "TEAMS  -  RESPAWN"),
			LOCTEXT("TDMBody", "Alpha vs Bravo with respawns, spawn protection and a shop. First team to 75 kills."), CSUI::TeamAlpha },
		{ ECSGameModeType::Competitive, LOCTEXT("CompTag", "TEAMS  -  ROUNDS"),
			LOCTEXT("CompBody", "Five against five, one life per round, 15 s buy time, money for kills and rounds. First to 8 rounds. Bots fill the teams."), CSUI::TeamBravo },
	};
	TSharedRef<SHorizontalBox> ModeRow = SNew(SHorizontalBox);
	for (int32 i = 0; i < UE_ARRAY_COUNT(Modes); ++i)
	{
		const ECSGameModeType Mode = Modes[i].Mode;
		ModeRow->AddSlot().FillWidth(1.f).Padding(i == 0 ? 0.f : 10.f, 0.f, 0.f, 0.f)
		[
			MakeChoiceTile(UCSModeSettings::ModeName(Mode), Modes[i].Tag, Modes[i].Body, Modes[i].Color,
				[this, Mode]() { return SelectedMode == Mode; }, [this, Mode]() { SelectedMode = Mode; }, 150.f)
		];
	}

	// --- Maps ---
	TSharedRef<SHorizontalBox> MapRow = SNew(SHorizontalBox);
	const TArray<FCSMapInfo>& Maps = UCSModeSettings::Get()->Maps;
	for (int32 i = 0; i < Maps.Num(); ++i)
	{
		MapRow->AddSlot().FillWidth(1.f).Padding(i == 0 ? 0.f : 10.f, 0.f, 0.f, 0.f)
		[
			MakeChoiceTile(FText::FromString(Maps[i].DisplayName.ToUpper()), LOCTEXT("MapTag", "MAP"), FText::FromString(Maps[i].Description),
				CSUI::Money, [this, i]() { return SelectedMapIndex == i; }, [this, i]() { SelectedMapIndex = i; }, 120.f)
		];
	}

	auto Action = [](const FText& Label, FOnClicked OnClicked, CSUI::EButtonKind Kind) -> TSharedRef<SWidget>
	{
		return SNew(SBox).HeightOverride(52.f)
			[
				CSUI::MakeButton(Label, OnClicked, Kind, true, 17)
			];
	};

	return SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				CSUI::MakeHeader(LOCTEXT("PlayTitle", "PLAY"), LOCTEXT("PlaySub", "Pick a mode and a map, then find or create a match. Joining an existing match uses its own mode and map."))
			]
			+ SVerticalBox::Slot().AutoHeight()[ CSUI::MakeSectionLabel(LOCTEXT("ModeLabel", "MODE")) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)[ ModeRow ]
			+ SVerticalBox::Slot().AutoHeight()[ CSUI::MakeSectionLabel(LOCTEXT("MapLabel", "MAP")) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)[ MapRow ]
			+ SVerticalBox::Slot().AutoHeight()[ CSUI::MakeSectionLabel(LOCTEXT("OptionsLabel", "OPTIONS")) ]
			+ SVerticalBox::Slot().AutoHeight()
			[
				CSUI::MakeRow(LOCTEXT("Region", "Region"), SAssignNew(RegionSelector, SCSSelector).Options(Regions))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				CSUI::MakeRow(LOCTEXT("Bots", "Bots (if you create the match; 5 vs 5 fills teams itself)"),
					SAssignNew(BotCountSelector, SCSSelector)
					.Options({ FText::AsNumber(0), FText::AsNumber(1), FText::AsNumber(2), FText::AsNumber(3), FText::AsNumber(4),
						FText::AsNumber(5), FText::AsNumber(6), FText::AsNumber(7), FText::AsNumber(8) })
					.SelectedIndex(3))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 18.f)
			[
				CSUI::MakeRow(LOCTEXT("BotDifficulty", "Bot difficulty"),
					SAssignNew(BotDifficultySelector, SCSSelector)
					.Options({ LOCTEXT("Easy", "Easy"), LOCTEXT("Normal", "Normal"), LOCTEXT("Hard", "Hard") })
					.SelectedIndex(1))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.4f)
				[
					Action(LOCTEXT("Quick", "QUICK MATCH"), FOnClicked::CreateLambda([this]() { QuickMatch(); return FReply::Handled(); }), CSUI::EButtonKind::Primary)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(8.f, 0.f, 0.f, 0.f)
				[
					Action(LOCTEXT("Practice", "PRACTICE VS BOTS"), FOnClicked::CreateLambda([this]() { StartPractice(); return FReply::Handled(); }), CSUI::EButtonKind::Normal)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					Action(LOCTEXT("Create", "CREATE SESSION"), FOnClicked::CreateLambda([this]() { ShowPage(EPage::Create); return FReply::Handled(); }), CSUI::EButtonKind::Normal)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(8.f, 0.f, 0.f, 0.f)
				[
					Action(LOCTEXT("Join", "JOIN BY NAME"), FOnClicked::CreateLambda([this]() { ShowPage(EPage::Join); return FReply::Handled(); }), CSUI::EButtonKind::Normal)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(8.f, 0.f, 0.f, 0.f)
				[
					Action(LOCTEXT("Browser", "SESSION BROWSER"), FOnClicked::CreateLambda([this]() { ShowPage(EPage::Browser); return FReply::Handled(); }), CSUI::EButtonKind::Normal)
				]
			]
		];
}

TSharedRef<SWidget> SCSMainMenu::MakeCreatePage()
{
	TArray<FText> Counts;
	for (int32 i = GMinPlayers; i <= GMaxPlayers; ++i)
	{
		Counts.Add(FText::AsNumber(i));
	}

	const FString DefaultName = FString::Printf(TEXT("room-%04d"), FMath::RandRange(0, 9999));

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			CSUI::MakeHeader(LOCTEXT("CreateTitle", "CREATE SESSION"), LOCTEXT("CreateSub", "If a session with this name already exists you join it instead."))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			CSUI::MakeRow(LOCTEXT("RoomName", "Session name"),
				SAssignNew(CreateNameBox, SEditableTextBox)
				.Text(FText::FromString(DefaultName))
				.Font(CSUI::Font(16)))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			CSUI::MakeRow(LOCTEXT("MaxPlayers", "Max players"),
				SAssignNew(MaxPlayersSelector, SCSSelector).Options(Counts).SelectedIndex(8 - GMinPlayers))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() { return FText::Format(LOCTEXT("RegionNote", "Region: {0} (change it on the Play page)"), FText::FromString(GRegionNames[RegionSelector.IsValid() ? RegionSelector->GetSelectedIndex() : 0])); })
			.Font(CSUI::Font(13)).ColorAndOpacity(CSUI::TextDim)
		]
		+ SVerticalBox::Slot().FillHeight(1.f)[ SNew(SBox) ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
		[
			SNew(SBox).WidthOverride(320.f)
			[
				CSUI::MakeButton(LOCTEXT("CreateBtn", "CREATE"),
					FOnClicked::CreateLambda([this]()
					{
						CreateSession(CreateNameBox->GetText().ToString(), GMinPlayers + MaxPlayersSelector->GetSelectedIndex());
						return FReply::Handled();
					}),
					CSUI::EButtonKind::Primary, true, 20)
			]
		];
}

TSharedRef<SWidget> SCSMainMenu::MakeJoinPage()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			CSUI::MakeHeader(LOCTEXT("JoinTitle", "JOIN SESSION"), LOCTEXT("JoinSub", "Type the session name exactly as the host created it."))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			CSUI::MakeRow(LOCTEXT("RoomNameJoin", "Session name"),
				SAssignNew(JoinNameBox, SEditableTextBox)
				.HintText(LOCTEXT("JoinHint", "room-1234"))
				.Font(CSUI::Font(16))
				.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type Commit)
				{
					if (Commit == ETextCommit::OnEnter)
					{
						JoinSession(Text.ToString());
					}
				}))
		]
		+ SVerticalBox::Slot().FillHeight(1.f)[ SNew(SBox) ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
		[
			SNew(SBox).WidthOverride(320.f)
			[
				CSUI::MakeButton(LOCTEXT("JoinBtn", "JOIN"),
					FOnClicked::CreateLambda([this]() { JoinSession(JoinNameBox->GetText().ToString()); return FReply::Handled(); }),
					CSUI::EButtonKind::Primary, true, 20)
			]
		];
}

TSharedRef<SWidget> SCSMainMenu::MakeBrowserPage()
{
	auto Header = [](const FText& Text, float Fill) -> SHorizontalBox::FSlot::FSlotArguments
	{
		return MoveTemp(SHorizontalBox::Slot().FillWidth(Fill)
			[
				SNew(STextBlock).Text(Text).Font(CSUI::Font(12, true)).ColorAndOpacity(CSUI::TextDim)
			]);
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				CSUI::MakeHeader(LOCTEXT("BrowserTitle", "SESSION BROWSER"), LOCTEXT("BrowserSub", "Open matches in the selected region. The list updates live."))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
			[
				CSUI::MakeButton(LOCTEXT("Refresh", "REFRESH"), FOnClicked::CreateLambda([this]()
				{
					if (UCSSessionSubsystem* Session = GetSession())
					{
						Session->StartBrowsing(GetSelectedRegion());
					}
					return FReply::Handled();
				}))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(16.f, 0.f, 16.f, 8.f))
		[
			SNew(SHorizontalBox)
			+ Header(LOCTEXT("ColName", "SESSION"), 0.5f)
			+ Header(LOCTEXT("ColPlayers", "PLAYERS"), 0.2f)
			+ Header(LOCTEXT("ColState", "STATUS"), 0.15f)
			+ Header(FText::GetEmpty(), 0.15f)
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(RoomListBox, SVerticalBox)
			]
		];
}

TSharedRef<SWidget> SCSMainMenu::MakeBusyOverlay()
{
	return SNew(SOverlay)
		.Visibility(this, &SCSMainMenu::GetBusyVisibility)
		+ SOverlay::Slot()
		[
			SNew(SImage).Image(CSUI::WhiteBrush()).ColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.7f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(CSUI::WhiteBrush())
			.BorderBackgroundColor(CSUI::Panel)
			.Padding(FMargin(48.f, 36.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Text(this, &SCSMainMenu::GetBusyText).Font(CSUI::Font(22, true)).ColorAndOpacity(CSUI::Text)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f, 0.f, 22.f)
				[
					SNew(STextBlock).Text(LOCTEXT("BusyHint", "Connecting to Photon Cloud. This usually takes a few seconds."))
					.Font(CSUI::Font(14)).ColorAndOpacity(CSUI::TextDim)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					CSUI::MakeButton(LOCTEXT("Cancel", "CANCEL"), FOnClicked::CreateLambda([this]()
					{
						if (UCSSessionSubsystem* Session = GetSession())
						{
							Session->Disconnect();
						}
						LocalMessage = LOCTEXT("Cancelled", "Cancelled.").ToString();
						return FReply::Handled();
					}))
				]
			]
		];
}

// ---------------------------------------------------------------------------
// Behaviour
// ---------------------------------------------------------------------------

void SCSMainMenu::ShowPage(EPage Page)
{
	UCSSessionSubsystem* Session = GetSession();
	if (Session && CurrentPage == EPage::Browser && Page != EPage::Browser)
	{
		Session->StopBrowsing();
	}

	CurrentPage = Page;
	Switcher->SetActiveWidgetIndex(static_cast<int32>(Page));

	if (Page == EPage::Browser && Session)
	{
		Session->StartBrowsing(GetSelectedRegion());
	}
	else if (Page == EPage::Settings)
	{
		SettingsPanel->Refresh();
	}
	else if (Page == EPage::Profile)
	{
		ProfilePanel->Refresh();
	}
	else if (Page == EPage::Admin)
	{
		AdminPanel->Refresh();
	}
	else if (Page == EPage::Create)
	{
		FSlateApplication::Get().SetKeyboardFocus(CreateNameBox);
	}
	else if (Page == EPage::Join)
	{
		FSlateApplication::Get().SetKeyboardFocus(JoinNameBox);
	}
}

void SCSMainMenu::QuickMatch()
{
	LocalMessage.Reset();
	if (UCSSessionSubsystem* Session = GetSession())
	{
		UE_LOG(LogCSNet, Log, TEXT("Menu: Quick Match (region '%s')."), *GetSelectedRegion());
		Session->QuickMatch(MakeRequest());
	}
}

void SCSMainMenu::CreateSession(const FString& RoomName, int32 MaxPlayers)
{
	LocalMessage.Reset();
	const FString Name = RoomName.TrimStartAndEnd();
	if (Name.IsEmpty())
	{
		LocalMessage = LOCTEXT("NeedName", "Enter a session name.").ToString();
		return;
	}
	if (UCSSessionSubsystem* Session = GetSession())
	{
		FCSSessionRequest Request = MakeRequest();
		Request.RoomName = Name;
		Request.MaxPlayers = FMath::Clamp(MaxPlayers, GMinPlayers, GMaxPlayers);
		UE_LOG(LogCSNet, Log, TEXT("Menu: Create session '%s' (max %d)."), *Name, Request.MaxPlayers);
		Session->HostOrJoin(Request);
	}
}

void SCSMainMenu::JoinSession(const FString& RoomName)
{
	LocalMessage.Reset();
	const FString Name = RoomName.TrimStartAndEnd();
	if (Name.IsEmpty())
	{
		LocalMessage = LOCTEXT("NeedJoinName", "Enter the session name to join.").ToString();
		return;
	}
	if (UCSSessionSubsystem* Session = GetSession())
	{
		FCSSessionRequest Request = MakeRequest();
		Request.RoomName = Name;
		UE_LOG(LogCSNet, Log, TEXT("Menu: Join session '%s'."), *Name);
		Session->JoinByName(Request);
	}
}

void SCSMainMenu::Quit()
{
	UWorld* World = WorldContext.IsValid() ? WorldContext->GetWorld() : nullptr;
	UKismetSystemLibrary::QuitGame(World, World ? World->GetFirstPlayerController() : nullptr, EQuitPreference::Quit, false);
}

void SCSMainMenu::RebuildRoomList()
{
	if (!RoomListBox.IsValid())
	{
		return;
	}
	RoomListBox->ClearChildren();

	const UCSSessionSubsystem* Session = GetSession();
	const TArray<FCSRoomInfo> Rooms = Session ? Session->GetRoomList() : TArray<FCSRoomInfo>();

	if (Rooms.Num() == 0)
	{
		RoomListBox->AddSlot().AutoHeight().Padding(16.f, 24.f)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				const UCSSessionSubsystem* S = GetSession();
				if (S && S->IsInLobby())
				{
					return LOCTEXT("NoRooms", "No open sessions in this region. Create one, or use Quick Match - it creates a session when none exists.");
				}
				return LOCTEXT("Loading", "Connecting to the lobby...");
			})
			.Font(CSUI::Font(15)).ColorAndOpacity(CSUI::TextDim).AutoWrapText(true)
		];
		return;
	}

	for (const FCSRoomInfo& Room : Rooms)
	{
		const FString Name = Room.Name;
		const bool bJoinable = Room.IsJoinable();
		// v1.1: Quick Match rooms are named "QM <mode> <map> [#n]" - show that readably.
		FString Display = Name;
		if (Name.StartsWith(TEXT("QM ")))
		{
			TArray<FString> Parts;
			Name.ParseIntoArrayWS(Parts);
			ECSGameModeType Mode = ECSGameModeType::Deathmatch;
			if (Parts.Num() >= 3 && UCSModeSettings::ParseModeTag(Parts[1], Mode))
			{
				const FCSMapInfo* Map = UCSModeSettings::Get()->FindMap(FName(*Parts[2]));
				Display = FString::Printf(TEXT("Quick Match  -  %s  -  %s%s"), *UCSModeSettings::ModeName(Mode).ToString(),
					Map ? *Map->DisplayName : *Parts[2], Parts.Num() > 3 ? *(TEXT("  ") + Parts[3]) : TEXT(""));
			}
		}

		RoomListBox->AddSlot().AutoHeight().Padding(0.f, 3.f)
		[
			SNew(SBorder)
			.BorderImage(CSUI::WhiteBrush())
			.BorderBackgroundColor(CSUI::PanelRaised)
			.Padding(FMargin(16.f, 8.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(Display)).Font(CSUI::Font(16, true)).ColorAndOpacity(CSUI::Text)
				]
				+ SHorizontalBox::Slot().FillWidth(0.2f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("Players", "{0} / {1}"), FText::AsNumber(Room.PlayerCount), FText::AsNumber(Room.MaxPlayers)))
					.Font(CSUI::Font(15)).ColorAndOpacity(CSUI::Text)
				]
				+ SHorizontalBox::Slot().FillWidth(0.15f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(!Room.bIsOpen ? LOCTEXT("Closed", "Closed") : (bJoinable ? LOCTEXT("Open", "Open") : LOCTEXT("Full", "Full")))
					.Font(CSUI::Font(14)).ColorAndOpacity(bJoinable ? CSUI::Accent : CSUI::Danger)
				]
				+ SHorizontalBox::Slot().FillWidth(0.15f).VAlign(VAlign_Center)
				[
					CSUI::MakeButton(LOCTEXT("JoinRow", "JOIN"),
						FOnClicked::CreateLambda([this, Name]() { JoinSession(Name); return FReply::Handled(); }),
						CSUI::EButtonKind::Primary, bJoinable, 14)
				]
			]
		];
	}
}

FText SCSMainMenu::GetStatusText() const
{
	const UCSSessionSubsystem* Session = GetSession();
	if (!LocalMessage.IsEmpty())
	{
		return FText::FromString(LocalMessage);
	}
	if (Session && !Session->GetLastError().IsEmpty())
	{
		return FText::FromString(Session->GetLastError());
	}
	if (Session && Session->GetSessionState() == ECSSessionState::Connected)
	{
		return LOCTEXT("Online", "Online - connected to Photon Cloud.");
	}
	return LOCTEXT("Offline", "Offline. Choose PLAY to find a match.");
}

EVisibility SCSMainMenu::GetBusyVisibility() const
{
	const UCSSessionSubsystem* Session = GetSession();
	const bool bBusy = Session && (Session->IsBusy() || Session->GetSessionState() == ECSSessionState::JoiningRoom
		|| Session->GetSessionState() == ECSSessionState::InRoom);
	return bBusy ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SCSMainMenu::GetBusyText() const
{
	const UCSSessionSubsystem* Session = GetSession();
	switch (Session ? Session->GetSessionState() : ECSSessionState::None)
	{
	case ECSSessionState::Connecting:	return LOCTEXT("Connecting", "CONNECTING...");
	case ECSSessionState::JoiningRoom:	return LOCTEXT("Joining", "JOINING MATCH...");
	case ECSSessionState::InRoom:		return LOCTEXT("LoadingMap", "LOADING MAP...");
	default:							return LOCTEXT("Working", "PLEASE WAIT...");
	}
}

FReply SCSMainMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		switch (CurrentPage)
		{
		case EPage::Create:
		case EPage::Join:
		case EPage::Browser:
			ShowPage(EPage::Play);
			return FReply::Handled();
		case EPage::Home:
			return FReply::Handled();
		default:
			ShowPage(EPage::Home);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
