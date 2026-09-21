// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Player/CSMenuPlayerController.h"

#include "Audio/CSAudio.h"
#include "Audio/CSAudioSettings.h"
#include "Components/AudioComponent.h"
#include "Core/CSLog.h"
#include "Settings/CSSettingsSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Multiplayer/CSSessionSubsystem.h"
#include "TimerManager.h"
#include "UI/SCSMainMenu.h"
#include "UnrealClient.h"

ACSMenuPlayerController::ACSMenuPlayerController()
{
	// Ticks for the focus guard below (and for UI input processing).
	PrimaryActorTick.bCanEverTick = true;
	bShowMouseCursor = true;
}

void ACSMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	SAssignNew(Menu, SCSMainMenu).WorldContext(this);
	GEngine->GameViewport->AddViewportWidgetContent(Menu.ToSharedRef(), 10);

	FInputModeUIOnly Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetWidgetToFocus(Menu);
	SetInputMode(Mode);
	SetShowMouseCursor(true);

	UE_LOG(LogCS, Log, TEXT("Main menu shown."));

	if (UCSSettingsSubsystem* Settings = UCSSettingsSubsystem::Get(this))
	{
		Settings->ReapplyAudio();
	}
	MenuMusic = CSAudio::PlayMusic(this, UCSAudioSettings::Get()->MenuMusic);

	// Self-tests run on the first visit only; a later visit (after leaving a
	// match) can test entering a second match with -cstestrejoin=NAME.
	static int32 MenuVisits = 0;
	++MenuVisits;
	FString RejoinRoom;
	if (MenuVisits == 2 && FParse::Value(FCommandLine::Get(), TEXT("cstestrejoin="), RejoinRoom))
	{
		FTimerHandle RejoinTimer;
		GetWorldTimerManager().SetTimer(RejoinTimer, [this, RejoinRoom]()
		{
			const UGameInstance* GI = GetGameInstance();
			const UCSSessionSubsystem* Session = GI ? GI->GetSubsystem<UCSSessionSubsystem>() : nullptr;
			UE_LOG(LogCS, Log, TEXT("REJOIN TEST: back in the menu (session state %s); creating '%s' again."),
				Session ? *UEnum::GetValueAsString(Session->GetSessionState()) : TEXT("?"), *RejoinRoom);
			if (Menu.IsValid())
			{
				Menu->CreateSession(RejoinRoom, 8);
			}
		}, 3.f, false);
		return;
	}
	if (MenuVisits > 1)
	{
		return;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("cstestmenu")) || FParse::Value(FCommandLine::Get(), TEXT("cstestmenu="), MenuTestAction))
	{
		FParse::Value(FCommandLine::Get(), TEXT("cstestmenu="), MenuTestAction);
		GetWorldTimerManager().SetTimer(MenuTestTimer, this, &ACSMenuPlayerController::RunMenuTest, 3.f, false);
	}
}

void ACSMenuPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Menu.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Menu.ToSharedRef());
	}
	Menu.Reset();
	Super::EndPlay(EndPlayReason);
}

void ACSMenuPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// Keep keyboard focus inside the menu (ESC navigation) unless the player
	// is typing into a text box.
	if (Menu.IsValid() && FSlateApplication::IsInitialized())
	{
		const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
		const bool bInside = Focused.IsValid() && (Focused == Menu || Menu->HasFocusedDescendants());
		if (!bInside)
		{
			FSlateApplication::Get().SetKeyboardFocus(Menu, EFocusCause::SetDirectly);
		}
	}
}

// ---------------------------------------------------------------------------
// Menu self-test
// ---------------------------------------------------------------------------

void ACSMenuPlayerController::Screenshot(const FString& Name)
{
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("CSTest") / (Name + TEXT(".png")));
	FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
	UE_LOG(LogCS, Log, TEXT("MENU TEST: screenshot -> %s"), *Path);
}

void ACSMenuPlayerController::RunMenuTest()
{
	UE_LOG(LogCS, Log, TEXT("MENU TEST: start, action '%s'."), *MenuTestAction);
	MenuTestStage = 0;
	MenuTestWaited = 0.f;
	GetWorldTimerManager().SetTimer(MenuTestTimer, this, &ACSMenuPlayerController::MenuTestStep, 1.f, true);
}

void ACSMenuPlayerController::MenuTestStep()
{
	if (!Menu.IsValid())
	{
		GetWorldTimerManager().ClearTimer(MenuTestTimer);
		return;
	}

	const UGameInstance* GI = GetGameInstance();
	const UCSSessionSubsystem* Session = GI ? GI->GetSubsystem<UCSSessionSubsystem>() : nullptr;

	// Actions that start a match.
	if (MenuTestAction == TEXT("quick"))
	{
		GetWorldTimerManager().ClearTimer(MenuTestTimer);
		Menu->ShowPage(SCSMainMenu::EPage::Play);
		Menu->QuickMatch();
		UE_LOG(LogCS, Log, TEXT("MENU TEST RESULT: quick match requested -> %s"),
			Session && Session->IsBusy() ? TEXT("REQUEST OK") : TEXT("REQUEST BROKEN"));
		return;
	}
	if (MenuTestAction == TEXT("practice"))
	{
		GetWorldTimerManager().ClearTimer(MenuTestTimer);
		Menu->ShowPage(SCSMainMenu::EPage::Play);
		UE_LOG(LogCS, Log, TEXT("MENU TEST RESULT: practice vs %d bot(s) requested."), Menu->GetSelectedBotCount());
		Menu->StartPractice();
		return;
	}
	if (MenuTestAction.StartsWith(TEXT("create:")))
	{
		GetWorldTimerManager().ClearTimer(MenuTestTimer);
		Menu->ShowPage(SCSMainMenu::EPage::Create);
		Menu->CreateSession(MenuTestAction.Mid(7), 8);
		UE_LOG(LogCS, Log, TEXT("MENU TEST RESULT: create '%s' requested -> %s"), *MenuTestAction.Mid(7),
			Session && Session->IsBusy() ? TEXT("REQUEST OK") : TEXT("REQUEST BROKEN"));
		return;
	}
	if (MenuTestAction.StartsWith(TEXT("browsejoin:")))
	{
		const FString Wanted = MenuTestAction.Mid(11);
		if (MenuTestStage == 0)
		{
			Menu->ShowPage(SCSMainMenu::EPage::Browser);
			MenuTestStage = 1;
			return;
		}

		MenuTestWaited += 1.f;
		const bool bListed = Session && Session->GetRoomList().ContainsByPredicate(
			[&Wanted](const FCSRoomInfo& R) { return R.Name == Wanted; });
		if (bListed)
		{
			Screenshot(TEXT("menu_browser"));
			UE_LOG(LogCS, Log, TEXT("MENU TEST RESULT: browser lists '%s' after %.0f s (%d room(s)) -> BROWSER OK"),
				*Wanted, MenuTestWaited, Session->GetRoomList().Num());
			GetWorldTimerManager().ClearTimer(MenuTestTimer);
			// Give the screenshot a frame, then join from the list.
			FTimerHandle JoinTimer;
			GetWorldTimerManager().SetTimer(JoinTimer, [this, Wanted]() { if (Menu.IsValid()) { Menu->JoinSession(Wanted); } }, 1.f, false);
		}
		else if (MenuTestWaited >= 40.f)
		{
			UE_LOG(LogCS, Log, TEXT("MENU TEST RESULT: '%s' never appeared in the browser (%d room(s), in lobby %s) -> BROWSER BROKEN"),
				*Wanted, Session ? Session->GetRoomList().Num() : -1, Session && Session->IsInLobby() ? TEXT("yes") : TEXT("no"));
			GetWorldTimerManager().ClearTimer(MenuTestTimer);
		}
		return;
	}

	// Default: tour the pages and screenshot each.
	static const SCSMainMenu::EPage Tour[] = {
		SCSMainMenu::EPage::Home, SCSMainMenu::EPage::Play, SCSMainMenu::EPage::Create, SCSMainMenu::EPage::Join,
		SCSMainMenu::EPage::Browser, SCSMainMenu::EPage::Inventory, SCSMainMenu::EPage::Settings };
	static const TCHAR* Names[] = { TEXT("menu_home"), TEXT("menu_play"), TEXT("menu_create"), TEXT("menu_join"),
		TEXT("menu_browser"), TEXT("menu_inventory"), TEXT("menu_settings") };

	const int32 Page = MenuTestStage / 2;
	if (Page >= UE_ARRAY_COUNT(Tour))
	{
		Menu->ShowPage(SCSMainMenu::EPage::Home);
		UE_LOG(LogCS, Log, TEXT("MENU TEST RESULT: page tour done -> TOUR OK"));
		GetWorldTimerManager().ClearTimer(MenuTestTimer);
		return;
	}
	if (MenuTestStage % 2 == 0)
	{
		Menu->ShowPage(Tour[Page]);
	}
	else
	{
		// The browser needs a few seconds to reach the lobby before its shot.
		if (Tour[Page] == SCSMainMenu::EPage::Browser && MenuTestWaited < 6.f)
		{
			MenuTestWaited += 1.f;
			return;
		}
		Screenshot(Names[Page]);
	}
	++MenuTestStage;
}
