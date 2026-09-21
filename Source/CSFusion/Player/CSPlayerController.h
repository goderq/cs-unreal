// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Under Fusion each client spawns and possesses its own PlayerController
// locally through the standard HandleStartingNewPlayer / RestartPlayer chain,
// so this class is purely a local-player object: input mode, view setup, and
// the bridge between the session layer and the UI.
//
// It never carries authoritative gameplay state.

#pragma once

#include "CoreMinimal.h"
#include "Core/CSCoreTypes.h"
#include "GameFramework/PlayerController.h"
#include "CSPlayerController.generated.h"

class UCSSessionSubsystem;
class UUserWidget;

UCLASS()
class CSFUSION_API ACSPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACSPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;

	/**
	 * Guarantees PlayerInput is a UEnhancedPlayerInput.
	 *
	 * Enhanced Input only evaluates mappings through
	 * UEnhancedInputLocalPlayerSubsystem::GetPlayerInput(), which is
	 * Cast<UEnhancedPlayerInput>(PlayerController->PlayerInput) re-read on
	 * every call. When that cast fails the subsystem does nothing at all -
	 * AddMappingContext still records the context so HasMappingContext keeps
	 * returning true, bindings still register on the EnhancedInputComponent,
	 * and not one line of error is logged. The pawn simply never receives
	 * input.
	 *
	 * DefaultPlayerInputClass in DefaultInput.ini is supposed to set this, but
	 * relying on it means a silent total loss of input if it fails to resolve,
	 * so the class is pinned here where a failure is impossible and visible.
	 */
	virtual void InitInputSystem() override;

	/** Switch between game input and UI input without duplicating boilerplate. */
	UFUNCTION(BlueprintCallable, Category = "CS|UI")
	void SetUIInputMode(bool bUIMode, UUserWidget* FocusWidget = nullptr);

	UFUNCTION(BlueprintPure, Category = "CS|Session")
	UCSSessionSubsystem* GetSessionSubsystem() const;

	// --- In-match menus (Slate, see UI/) -------------------------------------

	/** Inventory screen on/off (Tab by default). */
	void ToggleInventoryScreen();

	/** ESC menu on/off. Closes the inventory screen first if it is open. */
	void TogglePauseMenu();

	void CloseMenus();

	bool IsInventoryOpen() const { return bInventoryOpen; }
	bool IsPauseMenuOpen() const { return bPauseOpen; }

	virtual void PlayerTick(float DeltaTime) override;

protected:
	UFUNCTION()
	void HandleSessionStateChanged(ECSSessionState NewState);

	UFUNCTION()
	void HandleMasterClientChanged(bool bIsLocalPlayerMaster);

	/** `cs.netinfo` - prints session diagnostics to the log and screen. */
	UFUNCTION(Exec)
	void CSNetInfo();

	/**
	 * Input self-test, enabled with -cstestinput.
	 *
	 * Feeds W and mouse deltas into PlayerInput through APlayerController::
	 * InputKey - the same entry the platform layer uses - so the whole Enhanced
	 * Input chain runs, but without depending on the OS giving the window
	 * focus (which SendInput-based probing cannot guarantee). Logs how far the
	 * pawn moved and how far the view turned.
	 */
	UFUNCTION(Exec)
	void CSTestInput();

	void ArmSelfTest();

	/** Stage 5 self-test, enabled with -cstestui: inventory screen, ESC menu, settings. */
	UFUNCTION(Exec)
	void CSTestUI();
	FTimerHandle TestUITimer;
	FTimerHandle TestLeaveTimer;

	/** Stage 8 anti-cheat self-test, enabled with -cstestcheat. */
	UFUNCTION(Exec)
	void CSTestCheat();
	FTimerHandle TestCheatTimer;
	int32 CheatRoundsBefore = 0;
	TWeakObjectPtr<class ACSWorldPickup> CheatFarPickup;

	/** Stage 8 performance sample, enabled with -cstestperf: 20 s of frame times. */
	UFUNCTION(Exec)
	void CSTestPerf();
	FTimerHandle TestPerfTimer;
	FDelegateHandle TestPerfTickHandle;
	TArray<float> PerfFrameMs;

	/** Stage 7 bot self-test, enabled with -cstestbots (use with -bots=N). */
	UFUNCTION(Exec)
	void CSTestBots();
	FTimerHandle TestBotsTimer;
	TMap<int32, FVector> BotTestStart;
	TMap<int32, float> BotTestMoved;
	TMap<int32, int32> BotTestMaxItems;
	int32 BotTestHits = 0;
	int32 BotTestKills = 0;
	float BotTestElapsed = 0.f;
	FDelegateHandle BotTestEventHandle;

	/** Stage 6 visual self-test, enabled with -cstestvisual. */
	UFUNCTION(Exec)
	void CSTestVisual();
	FTimerHandle TestVisualTimer;
	/** -cstestwalk: the pawn walks a square (forward, right, back, left) for the animation tests. */
	FTimerHandle TestWalkTimer;
	float TestWalkTime = 0.f;
	FVector TestWalkDir = FVector::ZeroVector;

	/** Writes a screenshot to Saved/CSTest/<Name>.png (used by the UI tests). */
	void TestScreenshot(const FString& Name);

	/** Adds the widget to the viewport and gives it input focus. */
	void ShowMenuWidget(const TSharedRef<SWidget>& Widget, const TSharedRef<SWidget>& Focus);
	void HideMenuWidget(const TSharedPtr<SWidget>& Widget);

	/** UI-only input with a cursor while any menu is open, game input otherwise. */
	void RefreshMenuInputMode();

	TSharedPtr<class SCSPauseMenu> PauseMenu;
	TSharedPtr<class SCSInventoryPanel> InventoryPanel;
	TSharedPtr<SWidget> InventoryHost;
	bool bPauseOpen = false;
	bool bInventoryOpen = false;

	// --- Stage 4 self-tests (CSPlayerControllerLootTests.cpp) ---
	UFUNCTION(Exec) void CSTestGrab();
	UFUNCTION(Exec) void CSTestKill();
	UFUNCTION(Exec) void CSTestWatchLeave();
	UFUNCTION(Exec) void CSTestDoubleDrop();
	void TestKillStep();
	void TestKillVerifyLoot();
	void TestWalkUpAndPress(class ACSWorldPickup* Pickup);
	FTimerHandle TestKillTimer;
	FTimerHandle Stage4ArmTimers[4];
	FTimerHandle TestStepTimer;
	TWeakObjectPtr<class ACSCharacter> TestVictim;
	int32 TestVictimId = 0;
	int32 TestVictimItemsBefore = 0;
	int32 TestShotsFired = 0;
	int32 TestClaimAmmo = 0;

	/** Contested-pickup self-test, enabled with -cstestcontest on two clients. */
	UFUNCTION(Exec)
	void CSTestContest();
	FTimerHandle TestContestTimer;
	int32 TestContestItem = INDEX_NONE;

	/** Stage 3 self-test, enabled with -cstestloot: pickup, equip, fire, drop. */
	UFUNCTION(Exec)
	void CSTestLoot();
	void PressKey(const FKey& Key);
	FTimerHandle TestLootTimer;
	int32 TestLootItemIndex = INDEX_NONE;
	int32 TestPickupsBefore = 0;

	/** Two-client combat self-test, enabled with -cstestshoot. */
	UFUNCTION(Exec)
	void CSTestShoot();
	FTimerHandle TestShootTimer;
	int32 TestShootVictimId = 0;
	float TestShootVictimHpBefore = -1.f;
	virtual void PostSeamlessTravel() override;
	void TestInputRelease();
	void TestFireCheck();
	int32 TestRoundsBefore = -1;

	FTimerHandle TestInputTimer;
	FVector TestStartLocation = FVector::ZeroVector;
	FRotator TestStartRotation = FRotator::ZeroRotator;
};
