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
	 */
	void RunMenuTest();
	void MenuTestStep();
	void Screenshot(const FString& Name);

	TSharedPtr<SCSMainMenu> Menu;

	FTimerHandle MenuTestTimer;
	int32 MenuTestStage = 0;
	float MenuTestWaited = 0.f;
	FString MenuTestAction;
};
