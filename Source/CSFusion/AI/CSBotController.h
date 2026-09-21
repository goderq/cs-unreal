// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// AI controller of one bot. Exists only on the Master Client (the authority
// that simulates bots); other peers just see the replicated bot pawn.
//
//   Perception   AIPerception with Sight (enemies in view) and Hearing
//                (gunshots, reported by the authority for every shot)
//   Brain        a Behavior Tree built in C++ by CSBotBehavior, running on a
//                Blackboard with TargetActor / LastKnownLocation /
//                LootTarget / PatrolLocation
//   Damage       the director's combat events: a bot that is shot knows
//                where from, and turns on its attacker
//
// Everything the bot does goes through ACSCharacter::Bot* and so through the
// same authority validation as a human player's requests.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "AI/CSBotTuning.h"
#include "Perception/AIPerceptionTypes.h"
#include "CSBotController.generated.h"

class ACSCharacter;
class UAIPerceptionComponent;
class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;
class UBehaviorTree;
class UBlackboardData;
struct FCSCombatEvent;

UCLASS()
class CSFUSION_API ACSBotController : public AAIController
{
	GENERATED_BODY()

public:
	ACSBotController();

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	ACSCharacter* GetBot() const;

	void SetDifficulty(ECSBotDifficulty InDifficulty);
	ECSBotDifficulty GetDifficulty() const { return Difficulty; }
	const FCSBotTuning& GetTuning() const { return Tuning; }

	/** Alive enemy currently in sight. */
	bool CanSee(const AActor* Target) const;

	/** Closest alive enemy in sight, or null. */
	ACSCharacter* FindBestVisibleEnemy() const;

	/** When the current target was first seen (for reaction time). */
	double GetTargetAcquiredTime() const { return TargetAcquiredTime; }
	void NoteTargetAcquired() { TargetAcquiredTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0; }

	/** "sees N [ids], target X" for logs and tests. */
	FString DescribeState() const;

	/** Pickups this bot recently failed to take (full inventory etc.), to skip for a while. */
	bool IsPickupIgnored(const AActor* Pickup) const;
	void IgnorePickup(const AActor* Pickup, float Seconds);

protected:
	UFUNCTION()
	void HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	void HandleCombatEvent(const FCSCombatEvent& Event);

	UPROPERTY(VisibleAnywhere, Category = "CS|AI")
	TObjectPtr<UAIPerceptionComponent> Perception;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

	/** Built in C++ per controller (see CSBotBehavior). */
	UPROPERTY(Transient)
	TObjectPtr<UBehaviorTree> BehaviorTree;

	UPROPERTY(Transient)
	TObjectPtr<UBlackboardData> BlackboardData;

private:
	ECSBotDifficulty Difficulty = ECSBotDifficulty::Normal;
	FCSBotTuning Tuning;
	double TargetAcquiredTime = 0.0;
	TMap<TWeakObjectPtr<const AActor>, double> IgnoredPickups;
	FDelegateHandle CombatEventHandle;
	TWeakObjectPtr<class ACSMatchDirector> BoundDirector;
};
