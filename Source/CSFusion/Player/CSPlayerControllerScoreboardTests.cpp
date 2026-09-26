// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v2.0 phase 6 scoreboard self-test, enabled with -cstestscoreboard
// (Scripts/run_tests.ps1 suite "scoreboard": offline Deathmatch, two bots, no
// spawn protection). On the authority:
//
//   1. this player puts 50 damage on bot A, bot B finishes A: this player
//      gets an assist, B the kill, and the score is 2 a kill + 1 an assist;
//   2. Tab held: the scoreboard is drawn with a row per player
//      (Saved/CSTest/scoreboard.png).

#include "Player/CSPlayerController.h"

#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
#include "Core/CSCoreTypes.h"
#include "Core/CSLog.h"
#include "TimerManager.h"
#include "UI/CSHUD.h"

namespace
{
	const FCSPlayerCombatRecord* RecordOf(const ACSMatchDirector* Director, int32 PlayerId)
	{
		return Director ? Director->GetAllRecords().FindByPredicate([PlayerId](const FCSPlayerCombatRecord& R) { return R.PlayerId == PlayerId; }) : nullptr;
	}
}

void ACSPlayerController::CSTestScoreboard()
{
	CS_SELF_TEST_ONLY();
#if !UE_BUILD_SHIPPING
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	const ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	const int32 Me = Self ? Self->GetOwningPlayerId() : 0;
	TArray<int32> Bots;
	if (Director)
	{
		for (const FCSPlayerCombatRecord& R : Director->GetAllRecords())
		{
			if (CSBots::IsBotId(R.PlayerId) && R.bAlive)
			{
				Bots.Add(R.PlayerId);
			}
		}
	}
	const bool bReady = Director && Me != 0 && Bots.Num() >= 2 && Director->IsPlayerAlive(Me);
	if (TestRetryUntil(bReady, TestScoreboardTimer, &ACSPlayerController::CSTestScoreboard, TEXT("two living bots")))
	{
		return;
	}
	if (!bReady)
	{
		UE_LOG(LogCS, Log, TEXT("SCOREBOARD TEST RESULT: no two bots to test with -> SCOREBOARD MISSING"));
		UE_LOG(LogCS, Log, TEXT("SCOREBOARD TEST: done."));
		return;
	}

	const int32 Victim = Bots[0];
	const int32 Killer = Bots[1];
	const FCSPlayerCombatRecord* MineBefore = RecordOf(Director, Me);
	const FCSPlayerCombatRecord* KillerBefore = RecordOf(Director, Killer);
	const int32 AssistsBefore = MineBefore ? MineBefore->Assists : -1;
	const int32 KillsBefore = KillerBefore ? KillerBefore->Kills : -1;

	Director->ApplyDamage(Victim, Me, 50.f, ECSHitZone::Torso);
	Director->ApplyDamage(Victim, Killer, 500.f, ECSHitZone::Torso);

	const FCSPlayerCombatRecord* Mine = RecordOf(Director, Me);
	const FCSPlayerCombatRecord* KillerAfter = RecordOf(Director, Killer);
	const int32 Assists = Mine ? Mine->Assists : -1;
	const int32 Kills = KillerAfter ? KillerAfter->Kills : -1;
	const bool bScore = Mine && Mine->GetScore() == Mine->Kills * 2 + Mine->Assists;
	UE_LOG(LogCS, Log, TEXT("SCOREBOARD TEST RESULT: 50 damage then someone else's kill -> my assists %d -> %d, killer's kills %d -> %d, my score %d -> %s"),
		AssistsBefore, Assists, KillsBefore, Kills, Mine ? Mine->GetScore() : -1,
		(Assists == AssistsBefore + 1 && Kills == KillsBefore + 1 && bScore) ? TEXT("ASSISTS OK") : TEXT("ASSISTS BROKEN"));

	// Tab: the table with its new columns.
	SetScoreboardHeld(true);
	GetWorldTimerManager().SetTimer(TestScoreboardTimer, [this]()
	{
		TestScreenshot(TEXT("scoreboard"));
		FTimerHandle Check;
		GetWorldTimerManager().SetTimer(Check, [this]()
		{
			const ACSHUD* Hud = Cast<ACSHUD>(GetHUD());
			const ACSMatchDirector* D = ACSMatchDirector::Get(this);
			int32 Players = 0;
			if (D)
			{
				for (const FCSPlayerCombatRecord& R : D->GetAllRecords())
				{
					Players += R.PlayerId != 0 ? 1 : 0;
				}
			}
			const bool bDrawn = Hud && Hud->WasScoreboardDrawn();
			const int32 Rows = Hud ? Hud->GetScoreboardRowCount() : 0;
			SetScoreboardHeld(false);
			UE_LOG(LogCS, Log, TEXT("SCOREBOARD TEST RESULT: Tab held -> drawn %s, %d row(s) for %d player(s) -> %s"),
				bDrawn ? TEXT("yes") : TEXT("no"), Rows, Players, (bDrawn && Rows == Players) ? TEXT("SCOREBOARD OK") : TEXT("SCOREBOARD BROKEN"));
			UE_LOG(LogCS, Log, TEXT("SCOREBOARD TEST: done."));
		}, 0.5f, false);
	}, 0.6f, false);
#endif
}
