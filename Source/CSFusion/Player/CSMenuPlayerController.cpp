// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Player/CSMenuPlayerController.h"

#include "Audio/CSAudio.h"
#include "Audio/CSAudioSettings.h"
#include "Components/AudioComponent.h"
#include "Core/CSLog.h"
#include "Dom/JsonObject.h"
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
#include "UI/SCSAdminPanel.h"
#include "UI/SCSLoginScreen.h"
#include "UI/SCSMainMenu.h"
#include "Account/CSAccountSubsystem.h"
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

	if (UCSSettingsSubsystem* Settings = UCSSettingsSubsystem::Get(this))
	{
		Settings->ReapplyAudio();
	}
	MenuMusic = CSAudio::PlayMusic(this, UCSAudioSettings::Get()->MenuMusic);

	// v1.2: an Epic account is required to play online. Self-tests and
	// offline debugging pass -noaccount and go straight to the menu; v2.0
	// lets the player choose offline practice on the sign-in screen.
	UCSAccountSubsystem* Account = UCSAccountSubsystem::Get(this);
	if (Account)
	{
		AccountChangedHandle = Account->OnAccountChanged.AddUObject(this, &ACSMenuPlayerController::HandleAccountChanged);
	}
	const bool bNeedsSignIn = Account && Account->IsSignInRequired() && !Account->IsReady() && !Account->IsOffline();
	if (bNeedsSignIn)
	{
		ShowLoginScreen();
		return;
	}

	ShowMainMenu();
}

void ACSMenuPlayerController::HandleAccountChanged()
{
	const UCSAccountSubsystem* Account = UCSAccountSubsystem::Get(this);
	if (!Account || !Account->IsSignInRequired() || !Menu.IsValid())
	{
		return;
	}
	const ECSAccountState State = Account->GetState();
	if (State == ECSAccountState::SignedOut || State == ECSAccountState::Failed || State == ECSAccountState::SigningIn)
	{
		UE_LOG(LogCS, Log, TEXT("Account state %s - back to the sign-in screen."), *UEnum::GetValueAsString(State));
		GEngine->GameViewport->RemoveViewportWidgetContent(Menu.ToSharedRef());
		Menu.Reset();
		ShowLoginScreen();
	}
}

void ACSMenuPlayerController::ShowLoginScreen()
{
	if (LoginScreen.IsValid())
	{
		return;
	}
	SAssignNew(LoginScreen, SCSLoginScreen)
		.WorldContext(this)
		.OnSignedIn(FSimpleDelegate::CreateUObject(this, &ACSMenuPlayerController::ShowMainMenu));

	GEngine->GameViewport->AddViewportWidgetContent(LoginScreen.ToSharedRef(), 10);
	FInputModeUIOnly Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetWidgetToFocus(LoginScreen);
	SetInputMode(Mode);
	SetShowMouseCursor(true);
	UE_LOG(LogCS, Log, TEXT("Sign-in screen shown."));

	// -cstestlogin: screenshot the sign-in screen and report its state.
	if (FParse::Param(FCommandLine::Get(), TEXT("cstestlogin")))
	{
		GetWorldTimerManager().SetTimer(MenuTestTimer, [this]()
		{
			const UCSAccountSubsystem* Account = UCSAccountSubsystem::Get(this);
			Screenshot(TEXT("login"));
			UE_LOG(LogCS, Log, TEXT("LOGIN TEST BACKEND: %s"), *UCSAccountSubsystem::DescribeBackend());
			UE_LOG(LogCS, Log, TEXT("LOGIN TEST RESULT: screen up, account state %s, error '%s' -> %s"),
				*UEnum::GetValueAsString(Account ? Account->GetState() : ECSAccountState::SignedOut),
				Account ? *Account->GetLastError() : TEXT(""),
				LoginScreen.IsValid() ? TEXT("LOGIN SCREEN OK") : TEXT("LOGIN SCREEN BROKEN"));
		}, 4.f, false);
	}
}

void ACSMenuPlayerController::ShowMainMenu()
{
	if (Menu.IsValid())
	{
		return;
	}
	if (LoginScreen.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(LoginScreen.ToSharedRef());
		LoginScreen.Reset();
	}

	SAssignNew(Menu, SCSMainMenu).WorldContext(this);
	GEngine->GameViewport->AddViewportWidgetContent(Menu.ToSharedRef(), 10);

	FInputModeUIOnly Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetWidgetToFocus(Menu);
	SetInputMode(Mode);
	SetShowMouseCursor(true);

	UE_LOG(LogCS, Log, TEXT("Main menu shown."));

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
	if (UCSAccountSubsystem* Account = UCSAccountSubsystem::Get(this))
	{
		Account->OnAccountChanged.Remove(AccountChangedHandle);
	}
	if (GEngine && GEngine->GameViewport)
	{
		if (Menu.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(Menu.ToSharedRef());
		}
		if (LoginScreen.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(LoginScreen.ToSharedRef());
		}
	}
	Menu.Reset();
	LoginScreen.Reset();
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

void ACSMenuPlayerController::RunBackendProbe()
{
	UCSAccountSubsystem* Account = UCSAccountSubsystem::Get(this);
	if (!Account || !Account->IsReady())
	{
		UE_LOG(LogCS, Log, TEXT("BACKEND PROBE RESULT: not signed in (state %s) -> PROBE SKIPPED"),
			Account ? *UEnum::GetValueAsString(Account->GetState()) : TEXT("?"));
		return;
	}
	const bool bStaff = Account->IsStaff();
	const FString Me = Account->GetProfileId();
	const FString Nobody = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	UE_LOG(LogCS, Log, TEXT("BACKEND PROBE: signed in as role '%s' - every step skips the client's own checks."), *Account->GetRole());

	struct FStep
	{
		FString Name;
		FString Function;
		TSharedRef<FJsonObject> Body;
		TArray<int32> Expected;
	};
	auto Body = [](std::initializer_list<TPair<const TCHAR*, FString>> Fields)
	{
		const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		for (const TPair<const TCHAR*, FString>& Field : Fields)
		{
			Json->SetStringField(Field.Key, Field.Value);
		}
		return Json;
	};
	TSharedRef<TArray<FStep>> Steps = MakeShared<TArray<FStep>>();
	// A player is refused everything in the admin service; staff can look,
	// but can never act on themselves.
	Steps->Add({ TEXT("admin whoami"), TEXT("admin"), Body({ { TEXT("action"), TEXT("whoami") } }), bStaff ? TArray<int32>{ 200 } : TArray<int32>{ 403 } });
	Steps->Add({ TEXT("admin players"), TEXT("admin"), Body({ { TEXT("action"), TEXT("players") } }), bStaff ? TArray<int32>{ 200 } : TArray<int32>{ 403 } });
	Steps->Add({ TEXT("admin ban myself"), TEXT("admin"), Body({ { TEXT("action"), TEXT("ban") }, { TEXT("profile_id"), Me }, { TEXT("reason"), TEXT("probe") } }), { 403 } });
	Steps->Add({ TEXT("admin make myself superadmin"), TEXT("admin"), Body({ { TEXT("action"), TEXT("set_role") }, { TEXT("profile_id"), Me }, { TEXT("role"), TEXT("superadmin") }, { TEXT("reason"), TEXT("probe") } }), { 400, 403 } });
	Steps->Add({ TEXT("admin rename myself"), TEXT("admin"), Body({ { TEXT("action"), TEXT("set_nickname") }, { TEXT("profile_id"), Me }, { TEXT("nickname"), TEXT("ProbeName") }, { TEXT("reason"), TEXT("probe") } }), { 403 } });
	// Match records: reports and tickets for a match that does not exist, and a bogus mode.
	Steps->Add({ TEXT("report an unknown match"), TEXT("match"), Body({ { TEXT("action"), TEXT("report") }, { TEXT("match_id"), Nobody } }), { 404 } });
	Steps->Add({ TEXT("ticket for an unknown match"), TEXT("match"), Body({ { TEXT("action"), TEXT("ticket") }, { TEXT("match_id"), Nobody } }), { 404 } });
	Steps->Add({ TEXT("start a match with a bogus mode"), TEXT("match"), Body({ { TEXT("action"), TEXT("start") }, { TEXT("mode"), TEXT("GODMODE") }, { TEXT("map"), TEXT("Depot") } }), { 400 } });
	// Phase 2 (B11): only the host of a real match reports anti-cheat incidents.
	Steps->Add({ TEXT("incident for an unknown match"), TEXT("match"), Body({ { TEXT("action"), TEXT("incident") }, { TEXT("match_id"), Nobody }, { TEXT("kind"), TEXT("removed") } }), { 404 } });

	TSharedRef<int32> Index = MakeShared<int32>(0);
	TSharedRef<int32> Failed = MakeShared<int32>(0);
	TSharedRef<TFunction<void()>> Next = MakeShared<TFunction<void()>>();
	TWeakObjectPtr<ACSMenuPlayerController> WeakThis(this);
	*Next = [WeakThis, Steps, Index, Failed, Next]()
	{
		ACSMenuPlayerController* Self = WeakThis.Get();
		UCSAccountSubsystem* Acc = Self ? UCSAccountSubsystem::Get(Self) : nullptr;
		if (!Acc)
		{
			return;
		}
		if (*Index >= Steps->Num())
		{
			UE_LOG(LogCS, Log, TEXT("BACKEND PROBE RESULT: %d step(s), %d answered as expected -> %s"),
				Steps->Num(), Steps->Num() - *Failed, *Failed == 0 ? TEXT("BACKEND PROBE OK") : TEXT("BACKEND PROBE BROKEN"));
			return;
		}
		const FStep& Step = (*Steps)[*Index];
		Acc->DebugCallFunction(Step.Function, Step.Body, [Steps, Index, Failed, Next](int32 Code, const TSharedPtr<FJsonObject>& Json)
		{
			const FStep& Done = (*Steps)[*Index];
			const bool bOk = Done.Expected.Contains(Code);
			FString Error;
			if (Json.IsValid())
			{
				Json->TryGetStringField(TEXT("error"), Error);
			}
			// Staff may look (200); everything else must be refused.
			const TCHAR* Verdict = bOk ? (Code < 300 ? TEXT("ALLOWED OK") : TEXT("REFUSED OK"))
				: (Code < 300 ? TEXT("NOT STOPPED") : TEXT("WRONG ANSWER - BROKEN"));
			UE_LOG(LogCS, Log, TEXT("BACKEND PROBE RESULT: %s -> %d '%s' -> %s"), *Done.Name, Code, *Error, Verdict);
			*Failed += bOk ? 0 : 1;
			++(*Index);
			(*Next)();
		});
	};
	(*Next)();
}

void ACSMenuPlayerController::AdminTestStep()
{
#if UE_BUILD_SHIPPING
	GetWorldTimerManager().ClearTimer(MenuTestTimer);
#else
	const UCSAccountSubsystem* Account = UCSAccountSubsystem::Get(this);
	const TSharedPtr<SCSAdminPanel> Panel = Menu.IsValid() ? Menu->GetAdminPanel() : nullptr;
	if (!Account || !Account->IsStaff() || !Panel.IsValid())
	{
		UE_LOG(LogCS, Log, TEXT("ADMIN TEST RESULT: this account is not staff -> ADMIN SKIPPED"));
		GetWorldTimerManager().ClearTimer(MenuTestTimer);
		return;
	}
	if (MenuTestStage == 0)
	{
		Menu->ShowPage(SCSMainMenu::EPage::Admin);
		MenuTestWaited = 0.f;
		++MenuTestStage;
		return;
	}
	// Each view gets three seconds for the backend to answer, then a screenshot,
	// and one more second for the screenshot to be taken (it is taken with a
	// later frame) before the next view: player list, a player, match list, a
	// match, the log.
	static const TCHAR* Shots[] = { TEXT("admin_players"), TEXT("admin_player"), TEXT("admin_matches"), TEXT("admin_match"), TEXT("admin_log") };
	const int32 Shot = MenuTestStage - 1;
	MenuTestWaited += 1.f;
	if (MenuTestWaited < 3.f)
	{
		return;
	}
	if (MenuTestWaited < 4.f)
	{
		Screenshot(Shots[Shot]);
		UE_LOG(LogCS, Log, TEXT("ADMIN TEST: %s - %s"), Shots[Shot], *Panel->TestDescribe());
		return;
	}
	MenuTestWaited = 0.f;
	switch (Shot)
	{
	case 0: Panel->TestSelectFirstRow(); break;
	case 1: Panel->TestShowTab(1); break;
	case 2: Panel->TestSelectFirstRow(); break;
	case 3: Panel->TestShowTab(2); break;
	default:
		UE_LOG(LogCS, Log, TEXT("ADMIN TEST RESULT: %s -> %s"), *Panel->TestDescribe(),
			Panel->TestHasData() ? TEXT("ADMIN OK") : TEXT("ADMIN BROKEN"));
		GetWorldTimerManager().ClearTimer(MenuTestTimer);
		return;
	}
	++MenuTestStage;
#endif
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

	// v2.0 security probe against the real backend, as a modified client would
	// do it: every client-side gate is skipped, the server must refuse.
	if (MenuTestAction == TEXT("backendprobe"))
	{
		GetWorldTimerManager().ClearTimer(MenuTestTimer);
		RunBackendProbe();
		return;
	}

	// v2.0 admin page with live data: every tab and the first row of each.
	if (MenuTestAction == TEXT("admin"))
	{
		AdminTestStep();
		return;
	}

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
		SCSMainMenu::EPage::Browser, SCSMainMenu::EPage::Settings,
		SCSMainMenu::EPage::Profile };
	static const TCHAR* Names[] = { TEXT("menu_home"), TEXT("menu_play"), TEXT("menu_create"), TEXT("menu_join"),
		TEXT("menu_browser"), TEXT("menu_settings"), TEXT("menu_profile") };

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
		MenuTestWaited = 0.f;
	}
	else
	{
		// Two pages are not ready the instant they are shown: the browser has
		// to reach the lobby, and the profile is waiting for the leaderboard.
		const float NeedsWait = Tour[Page] == SCSMainMenu::EPage::Browser ? 6.f
			: (Tour[Page] == SCSMainMenu::EPage::Profile ? 3.f : 0.f);
		if (MenuTestWaited < NeedsWait)
		{
			MenuTestWaited += 1.f;
			return;
		}
		Screenshot(Names[Page]);
	}
	++MenuTestStage;
}
