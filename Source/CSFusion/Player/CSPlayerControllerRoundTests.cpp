// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v1.0 round-flow self-test (-cstestround, use with -roundtime=20 -bots=2):
//   1. hold the scoreboard key (real input) -> scoreboard drawn, release -> hidden;
//   2. at the end of the round -> "MATCH OVER" banner and the scoreboard by itself;
//   3. the next round starts with every player's kills and deaths at zero.

#include "Player/CSPlayerController.h"

#include "Combat/CSMatchDirector.h"
#include "Core/CSLog.h"
#include "Framework/Application/SlateApplication.h"
#include "GameModes/CSGameState.h"
#include "Input/CSInputConfig.h"
#include "TimerManager.h"
#include "UI/CSHUD.h"

namespace
{
	void SendKey(ACSPlayerController* PC, const FKey& Key, EInputEvent Event)
	{
		const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
		FViewport* Viewport = (PC->GetLocalPlayer() && PC->GetLocalPlayer()->ViewportClient)
			? PC->GetLocalPlayer()->ViewportClient->Viewport : nullptr;
		PC->InputKey(FInputKeyEventArgs(Viewport, Device, Key, Event, Event == IE_Released ? 0.f : 1.f, false, FPlatformTime::Cycles64()));
	}
}

void ACSPlayerController::CSTestRound()
{
	CS_SELF_TEST_ONLY();
	ACSHUD* Hud = Cast<ACSHUD>(GetHUD());
	if (!Hud || !GetPawn())
	{
		GetWorldTimerManager().SetTimer(TestRoundTimer, this, &ACSPlayerController::CSTestRound, 1.f, false);
		return;
	}

	// 1. Scoreboard on hold, through Enhanced Input.
	const FKey Key = GetDefault<UCSInputConfig>()->Key_Scoreboard;
	SendKey(this, Key, IE_Pressed);
	GetWorldTimerManager().SetTimer(TestRoundTimer, [this, Key]()
	{
		const ACSHUD* Hud1 = Cast<ACSHUD>(GetHUD());
		const bool bShown = Hud1 && Hud1->WasScoreboardDrawn();
		const int32 Rows = Hud1 ? Hud1->GetScoreboardRowCount() : 0;
		TestScreenshot(TEXT("round_scoreboard_hold"));
		SendKey(this, Key, IE_Released);

		GetWorldTimerManager().SetTimer(TestRoundTimer, [this, bShown, Rows, Key]()
		{
			const ACSHUD* Hud2 = Cast<ACSHUD>(GetHUD());
			const ACSGameState* GS = GetWorld()->GetGameState<ACSGameState>();
			const bool bPost = GS && GS->GetMatchPhase() == ECSMatchPhase::PostMatch;
			const bool bBoardHidden = bPost || (Hud2 && !Hud2->WasScoreboardDrawn());
			UE_LOG(LogCS, Log, TEXT("ROUND TEST RESULT: hold %s -> scoreboard %s with %d row(s), released -> %s -> %s"),
				*Key.GetDisplayName().ToString(), bShown ? TEXT("shown") : TEXT("NOT shown"), Rows,
				bBoardHidden ? TEXT("hidden") : TEXT("still shown"),
				bShown && bBoardHidden && Rows >= 1 ? TEXT("SCOREBOARD OK") : TEXT("SCOREBOARD BROKEN"));

			// 2 + 3. Watch the phase until PostMatch, then until the next round.
			RoundTestSawPostMatch = false;
			RoundTestElapsed = 0.f;
			GetWorldTimerManager().SetTimer(TestRoundTimer, [this]()
			{
				RoundTestElapsed += 0.25f;
				const ACSHUD* Hud3 = Cast<ACSHUD>(GetHUD());
				const ACSGameState* GS3 = GetWorld()->GetGameState<ACSGameState>();
				const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
				if (!Hud3 || !GS3 || !Director)
				{
					return;
				}
				const ECSMatchPhase Phase = GS3->GetMatchPhase();

				if (!RoundTestSawPostMatch && Phase == ECSMatchPhase::PostMatch)
				{
					// Give the HUD a frame to raise the banner.
					if (Hud3->GetBannerTitle() != TEXT("MATCH OVER"))
					{
						return;
					}
					RoundTestSawPostMatch = true;
					int32 TotalKills = 0;
					for (const FCSPlayerCombatRecord& R : Director->GetAllRecords())
					{
						TotalKills += R.PlayerId != 0 ? R.Kills : 0;
					}
					RoundTestKillsAtEnd = TotalKills;
					TestScreenshot(TEXT("round_end"));
					const bool bOk = Hud3->WasScoreboardDrawn() && Hud3->GetScoreboardRowCount() >= 1;
					UE_LOG(LogCS, Log, TEXT("ROUND TEST RESULT: round end -> banner '%s' / '%s', scoreboard %s (%d rows), %d kill(s) this round -> %s"),
						*Hud3->GetBannerTitle(), *Hud3->GetBannerSubtitle(),
						Hud3->WasScoreboardDrawn() ? TEXT("shown") : TEXT("NOT shown"), Hud3->GetScoreboardRowCount(), TotalKills,
						bOk ? TEXT("ROUND END OK") : TEXT("ROUND END BROKEN"));
					return;
				}

				if (RoundTestSawPostMatch && Phase == ECSMatchPhase::InProgress)
				{
					GetWorldTimerManager().ClearTimer(TestRoundTimer);
					int32 Kills = 0;
					int32 Deaths = 0;
					for (const FCSPlayerCombatRecord& R : Director->GetAllRecords())
					{
						Kills += R.PlayerId != 0 ? R.Kills : 0;
						Deaths += R.PlayerId != 0 ? R.Deaths : 0;
					}
					// Checked right at the start of the round; a kill in the
					// first quarter second is possible but unlikely.
					UE_LOG(LogCS, Log, TEXT("ROUND TEST RESULT: next round -> banner '%s', kills %d -> %d, deaths now %d -> %s"),
						*Hud3->GetBannerTitle(), RoundTestKillsAtEnd, Kills, Deaths,
						Kills == 0 && Deaths == 0 ? TEXT("SCORES RESET OK") : TEXT("SCORES RESET BROKEN"));
					return;
				}

				if (RoundTestElapsed > 120.f)
				{
					GetWorldTimerManager().ClearTimer(TestRoundTimer);
					UE_LOG(LogCS, Warning, TEXT("ROUND TEST RESULT: phase stuck at %d -> ROUND FLOW BROKEN"), static_cast<int32>(Phase));
				}
			}, 0.25f, true);
		}, 0.5f, false);
	}, 0.5f, false);
}
