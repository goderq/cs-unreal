// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "GameModes/CSMenuGameMode.h"

#include "GameFramework/HUD.h"
#include "Player/CSMenuPlayerController.h"

ACSMenuGameMode::ACSMenuGameMode()
{
	PlayerControllerClass = ACSMenuPlayerController::StaticClass();
	DefaultPawnClass = nullptr;
	HUDClass = AHUD::StaticClass();
}

void ACSMenuGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// Intentionally empty: the base version would try to spawn a pawn.
}
