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

/**
 * First statement of every self-test entry point: self-tests never run in
 * Shipping (docs/AUDIT.md B12). The flags that start them are not even read
 * there, and the console is off; this also covers any other way in.
 */
#define CS_SELF_TEST_ONLY() if (CSSelfTestsDisabled()) { return; }

/** True in Shipping. Out of line on purpose: a compile-time constant here makes MSVC flag the test bodies as unreachable (C4702 is an error). */
CSFUSION_API bool CSSelfTestsDisabled();

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


	/** ESC menu on/off. Closes the inventory screen first if it is open. */
	void TogglePauseMenu();

	void CloseMenus();

	/** v1.1 buy menu on/off (B). Only opens while the shop is open for this player. */
	void ToggleShopScreen();
	bool IsShopOpen() const { return bShopOpen; }

	bool IsPauseMenuOpen() const { return bPauseOpen; }

	/** Scoreboard key held (the HUD also shows it by itself after a round). */
	void SetScoreboardHeld(bool bHeld) { bScoreboardHeld = bHeld; }
	bool IsScoreboardHeld() const { return bScoreboardHeld; }

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

	/** Phase 2 RPC-sender self-test on the non-master client, enabled with -cstestspoof (A1). */
	void CSTestSpoof();
	FTimerHandle TestSpoofTimer;
	int32 SpoofVictimId = 0;
	int32 SpoofVictimSlot = -1;
	/** Phase 2 B11 self-tests on the non-master client: -cstestfreeze, -cstestremoval. */
	void CSTestFreeze();
	void CSTestRemoval();
	int32 RemovalTeleports = 0;
	/** -cstestwallhop (B10): step through walls at a legal average speed. */
	void CSTestWallHop();
	int32 WallHops = 0;
	/** -cstestnorecoil (B8): a burst at one point claiming to aim, as a no-recoil cheat would. */
	void CSTestNoRecoil();
	FVector NoRecoilAim = FVector::ForwardVector;
	int32 NoRecoilShots = 0;
	int32 CheatRoundsBefore = 0;
	TWeakObjectPtr<class ACSWorldPickup> CheatFarPickup;

	/** Stage 8 performance sample, enabled with -cstestperf: 20 s of frame times. */
	UFUNCTION(Exec)
	void CSTestPerf();
	FTimerHandle TestPerfTimer;
	FDelegateHandle TestPerfTickHandle;
	TArray<float> PerfFrameMs;

	/** v1.0 round-flow self-test, enabled with -cstestround (use -roundtime=20). */
	UFUNCTION(Exec)
	void CSTestRound();
	FTimerHandle TestRoundTimer;
	bool RoundTestSawPostMatch = false;
	float RoundTestElapsed = 0.f;
	int32 RoundTestKillsAtEnd = 0;

	/** v1.1 self-tests (CSPlayerControllerModeTests.cpp). */
	UFUNCTION(Exec)
	void CSTestTour();
	UFUNCTION(Exec)
	void CSTestModes();
	UFUNCTION(Exec)
	void CSTestComp();
	UFUNCTION(Exec)
	void CSTestPoses();
	TWeakObjectPtr<class ACSCharacter> PoseBot;
	FTimerHandle TestModesTimer;
	TArray<FTransform> TourPoints;
	int32 TourIndex = -1;
	int32 ModesStep = 0;
	int32 ModesMoneyBefore = 0;
	int32 ModesGrenadesBefore = 0;
	bool ModesGrenadeThrown = false;
	int32 CompRoundAtStart = 0;
	float CompElapsed = 0.f;
	int32 CompChecked = 0;
	int32 CompMoneyBefore = 0;

	/** v1.0 weapon presentation self-test, enabled with -cstestweapons. */
	UFUNCTION(Exec)
	void CSTestWeapons();
	void WeaponTestNext();
	FTimerHandle TestWeaponsTimer;
	TArray<int32> WeaponTestSlots;
	int32 WeaponTestIndex = -1;
	float WeaponTestGap = -1.f;

	/** Stage 7 bot self-test, enabled with -cstestbots (use with -bots=N). */
	UFUNCTION(Exec)
	void CSTestBots();
	FTimerHandle TestBotsTimer;
	TMap<int32, FVector> BotTestStart;
	TMap<int32, float> BotTestMoved;
	TMap<int32, int32> BotTestMaxItems;
	TMap<int32, FVector> BotTestLastPos;
	TMap<int32, float> BotTestStill;
	float BotTestMaxStill = 0.f;
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

	/**
	 * Self-tests: walk the pawn to Dest at a legal speed, then call OnArrived.
	 * Tests used to teleport, which the Stage 8 cheat guard (rightly) punishes.
	 */
	void TestMoveTo(const FVector& Dest, TFunction<void()> OnArrived);

	/**
	 * Self-tests that need the other client: if not bReady, re-run Fn in 2 s
	 * (up to 20 times) and return true so the caller returns. B joins only after
	 * A is in the room, so A's timed tests can fire before B exists.
	 */
	bool TestRetryUntil(bool bReady, FTimerHandle& Timer, void (ACSPlayerController::*Fn)(), const TCHAR* What);
	int32 TestWaitTries = 0;
	int32 WalkPressRetries = 0;
	FTimerHandle TestMoveTimer;
	FVector TestMoveDest = FVector::ZeroVector;
	TFunction<void()> TestMoveDone;
	/** Remaining navigation path points of the current TestMoveTo. */
	TArray<FVector> TestMoveWaypoints;

	/** Writes a screenshot to Saved/CSTest/<Name>.png (used by the UI tests). */
	void TestScreenshot(const FString& Name);

	/** Adds the widget to the viewport and gives it input focus. */
	void ShowMenuWidget(const TSharedRef<SWidget>& Widget, const TSharedRef<SWidget>& Focus);
	void HideMenuWidget(const TSharedPtr<SWidget>& Widget);

	/** UI-only input with a cursor while any menu is open, game input otherwise. */
	void RefreshMenuInputMode();

	TSharedPtr<class SCSPauseMenu> PauseMenu;
	bool bPauseOpen = false;
	TSharedPtr<class SCSShopPanel> ShopPanel;
	TSharedPtr<SWidget> ShopHost;
	bool bShopOpen = false;
	bool bScoreboardHeld = false;

	// --- Stage 4 self-tests (CSPlayerControllerLootTests.cpp) ---
	UFUNCTION(Exec) void CSTestGrab();
	UFUNCTION(Exec) void CSTestKill();
	UFUNCTION(Exec) void CSTestWatchLeave();
	UFUNCTION(Exec) void CSTestDoubleDrop();
	void TestKillStep();
	void TestKillVerifyLoot();
	/** Walks to the pickup, presses E, and calls AfterPress one second later. */
	void TestWalkUpAndPress(class ACSWorldPickup* Pickup, TFunction<void()> AfterPress = nullptr);
	FTimerHandle TestKillTimer;
	FTimerHandle Stage4ArmTimers[4];
	FTimerHandle TestStepTimer;
	TWeakObjectPtr<class ACSCharacter> TestVictim;
	int32 TestVictimId = 0;
	int32 TestVictimItemsBefore = 0;
	int32 TestVictimRespawnsAtDeath = 0;
	int32 TestShotsFired = 0;
	/** Times the kill test walked up again because a wall was in the way. */
	int32 TestKillApproaches = 0;
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
	/** The pawn being shot at: re-aimed at right before the trigger, as it may be walking. */
	TWeakObjectPtr<class ACSCharacter> TestShootTarget;
	float TestShootVictimHpBefore = -1.f;
	virtual void PostSeamlessTravel() override;
	void TestInputRelease();
	void TestFireCheck();
	int32 TestRoundsBefore = -1;
	int32 TestMoneyBefore = 0;

	FTimerHandle TestInputTimer;
	FVector TestStartLocation = FVector::ZeroVector;
	FRotator TestStartRotation = FRotator::ZeroRotator;
};
