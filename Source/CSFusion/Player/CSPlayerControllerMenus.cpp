// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// In-match menus of ACSPlayerController: inventory screen and ESC menu, plus
// the Stage 5 UI self-test.

#include "Player/CSPlayerController.h"

#include "Camera/CameraComponent.h"
#include "Characters/CSCharacter.h"
#include "Core/CSLog.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EnhancedInputSubsystems.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/CSInputConfig.h"
#include "InputMappingContext.h"
#include "Inventory/CSPlayerInventory.h"
#include "Misc/Paths.h"
#include "Multiplayer/CSSessionSubsystem.h"
#include "Settings/CSSettingsSubsystem.h"
#include "TimerManager.h"
#include "UI/CSHUD.h"
#include "UI/CSUIStyle.h"
#include "UI/SCSInventoryPanel.h"
#include "UI/SCSPauseMenu.h"
#include "UnrealClient.h"
#include "Weapons/CSWeaponComponent.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"

namespace
{
	constexpr int32 GMenuZOrder = 50;
}

void ACSPlayerController::ShowMenuWidget(const TSharedRef<SWidget>& Widget, const TSharedRef<SWidget>& Focus)
{
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->AddViewportWidgetContent(Widget, GMenuZOrder);
	}

	// Nothing keeps firing or running while a menu is up.
	if (ACSCharacter* CSPawn = Cast<ACSCharacter>(GetPawn()))
	{
		if (UCSWeaponComponent* Weapon = CSPawn->GetWeaponComponent())
		{
			Weapon->StopFire();
			Weapon->SetAiming(false);
		}
	}
	FlushPressedKeys();

	RefreshMenuInputMode();
	FSlateApplication::Get().SetKeyboardFocus(Focus, EFocusCause::SetDirectly);
}

void ACSPlayerController::HideMenuWidget(const TSharedPtr<SWidget>& Widget)
{
	if (Widget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
	}
}

void ACSPlayerController::RefreshMenuInputMode()
{
	if (bPauseOpen || bInventoryOpen)
	{
		FInputModeUIOnly Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		if (bPauseOpen && PauseMenu.IsValid())
		{
			Mode.SetWidgetToFocus(PauseMenu);
		}
		else if (InventoryPanel.IsValid())
		{
			Mode.SetWidgetToFocus(InventoryPanel);
		}
		SetInputMode(Mode);
		SetShowMouseCursor(true);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
		SetShowMouseCursor(false);
		FlushPressedKeys();
	}
}

void ACSPlayerController::ToggleInventoryScreen()
{
	if (!IsLocalController() || bPauseOpen)
	{
		return;
	}

	if (bInventoryOpen)
	{
		bInventoryOpen = false;
		HideMenuWidget(InventoryHost);
		RefreshMenuInputMode();
		return;
	}

	if (!InventoryHost.IsValid())
	{
		// The key that closes the screen is whatever the player bound it to.
		FKey CloseKey = EKeys::Tab;
		{
			const UCSSettingsSubsystem* Settings = UCSSettingsSubsystem::Get(this);
			const UCSInputConfig* Config = GetDefault<UCSInputConfig>();
			CloseKey = Settings ? Settings->GetKeyFor(TEXT("ToggleInventory"), Config->Key_ToggleInventory) : Config->Key_ToggleInventory;
		}

		InventoryHost = SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SImage).Image(CSUI::WhiteBrush()).ColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.55f))
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(FMargin(16.f, 32.f))
			[
				SNew(SBox).WidthOverride(1240.f).HeightOverride(780.f)
				[
					SNew(SBorder)
					.BorderImage(CSUI::WhiteBrush())
					.BorderBackgroundColor(CSUI::Panel)
					.Padding(FMargin(36.f, 30.f))
					[
						SAssignNew(InventoryPanel, SCSInventoryPanel)
						.WorldContext(this)
						.ShowCloseButton(true)
						.CloseKey(CloseKey)
						.OnClose_Lambda([this]() { if (bInventoryOpen) { ToggleInventoryScreen(); } })
					]
				]
			];
	}

	bInventoryOpen = true;
	ShowMenuWidget(InventoryHost.ToSharedRef(), InventoryPanel.ToSharedRef());
}

void ACSPlayerController::TogglePauseMenu()
{
	if (!IsLocalController())
	{
		return;
	}

	if (bPauseOpen)
	{
		bPauseOpen = false;
		HideMenuWidget(PauseMenu);
		RefreshMenuInputMode();
		return;
	}

	if (bInventoryOpen)
	{
		// ESC on the inventory screen just closes it.
		ToggleInventoryScreen();
		return;
	}

	if (!PauseMenu.IsValid())
	{
		SAssignNew(PauseMenu, SCSPauseMenu)
			.WorldContext(this)
			.OnResume_Lambda([this]() { if (bPauseOpen) { TogglePauseMenu(); } })
			.OnLeaveMatch_Lambda([this]()
			{
				CloseMenus();
				if (UCSSessionSubsystem* Session = GetSessionSubsystem())
				{
					Session->LeaveToMainMenu();
				}
			});
	}

	PauseMenu->ResetToMain();
	bPauseOpen = true;
	ShowMenuWidget(PauseMenu.ToSharedRef(), PauseMenu.ToSharedRef());
}

void ACSPlayerController::CloseMenus()
{
	const bool bWasOpen = bPauseOpen || bInventoryOpen;
	if (bPauseOpen)
	{
		bPauseOpen = false;
		HideMenuWidget(PauseMenu);
	}
	if (bInventoryOpen)
	{
		bInventoryOpen = false;
		HideMenuWidget(InventoryHost);
	}
	if (bWasOpen && IsLocalController())
	{
		RefreshMenuInputMode();
	}
}

void ACSPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// Keyboard focus can be lost to a click on empty space or a widget being
	// rebuilt; without it ESC/TAB would no longer reach the open menu (game
	// input is off while a menu is up). Put it back.
	if (!IsLocalController() || !FSlateApplication::IsInitialized())
	{
		return;
	}
	const TSharedPtr<SWidget> Wanted = bPauseOpen ? StaticCastSharedPtr<SWidget>(PauseMenu)
		: (bInventoryOpen ? StaticCastSharedPtr<SWidget>(InventoryPanel) : nullptr);
	if (Wanted.IsValid())
	{
		const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
		const bool bInside = Focused.IsValid() && (Focused == Wanted || Wanted->HasFocusedDescendants());
		const bool bTyping = Focused.IsValid() && Focused->GetType() == TEXT("SEditableText");
		if (!bInside && !bTyping)
		{
			FSlateApplication::Get().SetKeyboardFocus(Wanted, EFocusCause::SetDirectly);
		}
	}
}

// ---------------------------------------------------------------------------
// Stage 5 self-test (-cstestui)
// ---------------------------------------------------------------------------

void ACSPlayerController::TestScreenshot(const FString& Name)
{
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("CSTest") / (Name + TEXT(".png")));
	FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
	UE_LOG(LogCS, Log, TEXT("UI TEST: screenshot requested -> %s"), *Path);
}

void ACSPlayerController::CSTestUI()
{
	// Drives the real code paths (the same functions the keys and buttons
	// call) and checks the results against live state.
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	if (!Self)
	{
		UE_LOG(LogCS, Warning, TEXT("UI TEST: no pawn."));
		return;
	}

	TestScreenshot(TEXT("hud"));

	GetWorldTimerManager().SetTimer(TestUITimer, [this]()
	{
		ToggleInventoryScreen();
		const bool bFocused = InventoryPanel.IsValid() && FSlateApplication::Get().GetKeyboardFocusedWidget() == InventoryPanel;
		UE_LOG(LogCS, Log, TEXT("UI TEST RESULT: inventory open -> %s (focused %s, cursor %s)"),
			bInventoryOpen ? TEXT("OPEN OK") : TEXT("OPEN BROKEN"), bFocused ? TEXT("yes") : TEXT("no"),
			ShouldShowMouseCursor() ? TEXT("yes") : TEXT("no"));
		TestScreenshot(TEXT("inventory"));

		GetWorldTimerManager().SetTimer(TestUITimer, [this]()
		{
			ToggleInventoryScreen();
			UE_LOG(LogCS, Log, TEXT("UI TEST RESULT: inventory close -> %s"), !bInventoryOpen ? TEXT("CLOSE OK") : TEXT("CLOSE BROKEN"));

			TogglePauseMenu();
			UE_LOG(LogCS, Log, TEXT("UI TEST RESULT: ESC menu open -> %s"), bPauseOpen ? TEXT("OPEN OK") : TEXT("OPEN BROKEN"));
			TestScreenshot(TEXT("pause"));

			GetWorldTimerManager().SetTimer(TestUITimer, [this]()
			{
				if (PauseMenu.IsValid())
				{
					PauseMenu->ShowSettings();
				}
				TestScreenshot(TEXT("settings"));

				GetWorldTimerManager().SetTimer(TestUITimer, [this]()
				{
					TogglePauseMenu();
					UE_LOG(LogCS, Log, TEXT("UI TEST RESULT: ESC menu close -> %s (cursor %s)"),
						!bPauseOpen ? TEXT("CLOSE OK") : TEXT("CLOSE BROKEN"), ShouldShowMouseCursor() ? TEXT("yes") : TEXT("no"));

					// Settings are applied live: FOV on the camera, rebinding in the mapping.
					UCSSettingsSubsystem* Settings = UCSSettingsSubsystem::Get(this);
					ACSCharacter* CSPawn = Cast<ACSCharacter>(GetPawn());
					if (!Settings || !CSPawn)
					{
						return;
					}
					const FCSPlayerPreferences Saved = Settings->GetPreferences();
					FCSPlayerPreferences Test = Saved;
					Test.FieldOfView = 88.f;
					Test.MouseSensitivity = 1.7f;
					Test.KeyOverrides.Add(TEXT("Interact"), EKeys::F);
					Settings->SetPreferences(Test, /*bSave*/ false);

					// Enhanced Input rebuilds its key mappings on the next tick, so check a
					// moment later rather than in the same frame.
					GetWorldTimerManager().SetTimer(TestStepTimer, [this, Saved]()
					{
						UCSSettingsSubsystem* LiveSettings = UCSSettingsSubsystem::Get(this);
						ACSCharacter* LivePawn = Cast<ACSCharacter>(GetPawn());
						if (!LiveSettings || !LivePawn)
						{
							return;
						}
						const UCameraComponent* Camera = LivePawn->FindComponentByClass<UCameraComponent>();
						const float Fov = Camera ? Camera->FieldOfView : -1.f;
						bool bRebound = false;
						if (const UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
						{
							// Ask the live input system which keys now drive the interact action.
							const UInputAction* Interact = LivePawn->GetInputConfig() ? LivePawn->GetInputConfig()->IA_Interact.Get() : nullptr;
							const TArray<FKey> Keys = Interact ? Input->QueryKeysMappedToAction(Interact) : TArray<FKey>();
							bRebound = Keys.Contains(EKeys::F) && !Keys.Contains(EKeys::E);
						}
						UE_LOG(LogCS, Log, TEXT("UI TEST RESULT: settings apply -> %s (FOV %.0f, interact rebound to F %s)"),
							(FMath::IsNearlyEqual(Fov, 88.f) && bRebound) ? TEXT("SETTINGS OK") : TEXT("SETTINGS BROKEN"),
							Fov, bRebound ? TEXT("yes") : TEXT("no"));

						LiveSettings->SetPreferences(Saved, /*bSave*/ false);
					}, 0.5f, false);

					// Combat HUD: run together with -cstestkill, which kills the
					// other client at 22 s; by now the kill feed must show it.
					GetWorldTimerManager().SetTimer(TestUITimer, [this]()
					{
						const ACSHUD* CSHud = Cast<ACSHUD>(GetHUD());
						UE_LOG(LogCS, Log, TEXT("UI TEST RESULT: combat HUD -> kill feed entries %d, last hit marker %.1f s ago"),
							CSHud ? CSHud->GetKillFeedCount() : -1, CSHud ? CSHud->GetSecondsSinceHitMarker() : -1.0);
						TestScreenshot(TEXT("hud_combat"));
					}, 11.f, false);
				}, 1.5f, false);
			}, 1.5f, false);
		}, 1.5f, false);
	}, 1.0f, false);
}
