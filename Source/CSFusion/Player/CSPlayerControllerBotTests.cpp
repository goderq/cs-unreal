// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Stage 7 bot self-test (-cstestbots): watches the bots for a minute and
// reports whether they spawned, moved around the map, fought (hits and
// kills), picked up loot and respawned. Run with -bots=N.

#include "Player/CSPlayerController.h"

#include "AI/CSBotController.h"
#include "AI/CSBotManager.h"
#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
#include "Core/CSLog.h"
#include "EngineUtils.h"
#include "Inventory/CSPlayerInventory.h"
#include "TimerManager.h"
#include "Weapons/CSWeaponDefinition.h"

void ACSPlayerController::CSTestBots()
{
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	if (!Director)
	{
		UE_LOG(LogCS, Warning, TEXT("BOT TEST: no director."));
		return;
	}

	BotTestStart.Reset();
	BotTestMoved.Reset();
	BotTestMaxItems.Reset();
	BotTestHits = 0;
	BotTestKills = 0;
	BotTestElapsed = 0.f;

	for (TActorIterator<ACSCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsBot())
		{
			BotTestStart.Add(It->GetOwningPlayerId(), It->GetActorLocation());
		}
	}
	UE_LOG(LogCS, Log, TEXT("BOT TEST: watching %d bot(s)."), BotTestStart.Num());

	BotTestEventHandle = Director->OnCombatEvent.AddLambda([this](const FCSCombatEvent& Event)
	{
		if (CSBots::IsBotId(Event.InstigatorId))
		{
			++BotTestHits;
			BotTestKills += Event.bKilled ? 1 : 0;
		}
	});

	GetWorldTimerManager().SetTimer(TestBotsTimer, [this]()
	{
		BotTestElapsed += 5.f;
		const ACSMatchDirector* D = ACSMatchDirector::Get(this);
		for (TActorIterator<ACSCharacter> It(GetWorld()); It; ++It)
		{
			if (!It->IsBot())
			{
				continue;
			}
			const int32 Id = It->GetOwningPlayerId();
			const FVector* Start = BotTestStart.Find(Id);
			if (!Start)
			{
				BotTestStart.Add(Id, It->GetActorLocation());
				continue;
			}
			BotTestMoved.FindOrAdd(Id) = FMath::Max(BotTestMoved.FindRef(Id), FVector::Dist2D(*Start, It->GetActorLocation()));

			int32 Items = 0;
			if (const ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(this, Id))
			{
				for (const FCSInventorySlot& Slot : Inventory->GetSlots())
				{
					Items += Slot.IsEmpty() ? 0 : 1;
				}
			}
			BotTestMaxItems.FindOrAdd(Id) = FMath::Max(BotTestMaxItems.FindRef(Id), Items);

			FCSPlayerCombatRecord Record;
			const bool bHasRecord = D && D->GetRecord(Id, Record);
			const ACSBotController* Controller = Cast<ACSBotController>(It->GetController());
			UE_LOG(LogCS, Log, TEXT("BOT TEST t=%.0f: %s hp %.0f %s, K %d D %d, items %d, moved %.0f cm, weapon %s, %s"),
				BotTestElapsed, *ACSBotManager::GetBotName(Id), bHasRecord ? Record.Health : -1.f,
				(bHasRecord && Record.bAlive) ? TEXT("alive") : TEXT("dead"),
				bHasRecord ? Record.Kills : 0, bHasRecord ? Record.Deaths : 0, Items, BotTestMoved.FindRef(Id),
				(D && D->GetLoadout(Id).Weapon) ? *D->GetLoadout(Id).Weapon->DisplayName.ToString() : TEXT("-"),
				Controller ? *Controller->DescribeState() : TEXT("NO CONTROLLER"));
		}

		if (BotTestElapsed >= 70.f)
		{
			GetWorldTimerManager().ClearTimer(TestBotsTimer);
			if (ACSMatchDirector* Dir = ACSMatchDirector::Get(this))
			{
				Dir->OnCombatEvent.Remove(BotTestEventHandle);
			}

			int32 Moved = 0;
			int32 Looted = 0;
			int32 Deaths = 0;
			for (const TPair<int32, float>& Pair : BotTestMoved)
			{
				Moved += Pair.Value > 800.f ? 1 : 0;
			}
			for (const TPair<int32, int32>& Pair : BotTestMaxItems)
			{
				Looted += Pair.Value > 0 ? 1 : 0;
			}
			for (TActorIterator<ACSCharacter> It(GetWorld()); It; ++It)
			{
				FCSPlayerCombatRecord Record;
				if (It->IsBot() && D && D->GetRecord(It->GetOwningPlayerId(), Record))
				{
					Deaths += Record.Deaths;
				}
			}
			const int32 Bots = BotTestStart.Num();
			const bool bOk = Bots > 0 && Moved == Bots && BotTestHits > 0 && BotTestKills > 0 && Looted > 0;
			UE_LOG(LogCS, Log, TEXT("BOT TEST RESULT: %d bot(s), %d roamed > 8 m, %d hit(s), %d kill(s), %d looted, %d death(s)/respawn(s) -> %s"),
				Bots, Moved, BotTestHits, BotTestKills, Looted, Deaths, bOk ? TEXT("BOTS OK") : TEXT("BOTS BROKEN"));
			TestScreenshot(TEXT("bots_end"));
		}
	}, 5.f, true);
}
