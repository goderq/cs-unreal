// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Local controller of the main menu map. Puts SCSMainMenu on screen with a
// cursor and UI-only input, and removes it when the map goes away (joining a
// room loads the match map, which destroys this controller).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CSMenuPlayerController.generated.h"

class SCSMainMenu;
class SCSLoginScreen;

UCLASS()
class CSFUSION_API ACSMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACSMenuPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PlayerTick(float DeltaTime) override;

protected:
	/**
	 * Menu self-test, enabled with -cstestmenu[=action].
	 *   (no value)          screenshots of every page, then stays in the menu
	 *   =quick              Quick Match through the menu
	 *   =create:NAME        Create Session NAME through the menu
	 *   =browsejoin:NAME    open the Session Browser, wait until NAME is
	 *                       listed, then join it from the list
	 *   =backendprobe       the live backend, as a modified client would call it
	 *   =admin              the ADMIN page with live data, a screenshot of every
	 *                       tab and of the first row of each (staff accounts)
	 */
	void RunMenuTest();
	void MenuTestStep();
	void Screenshot(const FString& Name);
	/** -cstestmenu=backendprobe: the real backend must refuse what this account may not do. */
	void RunBackendProbe();
	/** -cstestmenu=admin: one step of the admin page tour, once a second. */
	void AdminTestStep();

	/** v1.2: the sign-in screen shown before the menu. */
	TSharedPtr<SCSLoginScreen> LoginScreen;
	void ShowLoginScreen();
	void ShowMainMenu();

	/** v2.0: signing out (or a session that could not be renewed) goes back to the sign-in screen. */
	void HandleAccountChanged();
	FDelegateHandle AccountChangedHandle;

	TSharedPtr<SCSMainMenu> Menu;

	UPROPERTY(Transient)
	TObjectPtr<class UAudioComponent> MenuMusic;

	FTimerHandle MenuTestTimer;
	int32 MenuTestStage = 0;
	float MenuTestWaited = 0.f;
	FString MenuTestAction;
};
