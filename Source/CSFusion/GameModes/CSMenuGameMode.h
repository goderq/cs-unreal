// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Game mode of Lvl_MainMenu: no pawn, no match, just ACSMenuPlayerController
// showing the main menu. Deliberately not derived from ACSGameMode - the menu
// must never spawn a match director, pickups or a character.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CSMenuGameMode.generated.h"

UCLASS()
class CSFUSION_API ACSMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACSMenuGameMode();

	/** No pawn in the menu: skip RestartPlayer entirely. */
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
};
