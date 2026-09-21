// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Behavior Tree nodes for bots. Tasks and the service are instanced per bot
// (bCreateNodeInstance), so they keep their per-bot state in members.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/Decorators/BTDecorator_Blackboard.h"
#include "CSBTNodes.generated.h"

class ACSBotController;
class ACSCharacter;

/** "Key is set / not set" condition that aborts itself and lower branches when the key changes. */
UCLASS()
class CSFUSION_API UCSBTDecorator_KeySet : public UBTDecorator_Blackboard
{
	GENERATED_BODY()

public:
	void Configure(FName KeyName, bool bMustBeSet);
};

/** Fight the TargetActor: face it, strafe, shoot in bursts with human-like error. */
UCLASS()
class CSFUSION_API UCSBTTask_Engage : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UCSBTTask_Engage();

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

private:
	void TryFire(ACSBotController* Controller, ACSCharacter* Bot, ACSCharacter* Target, double Now);
	void UpdateMovement(ACSBotController* Controller, ACSCharacter* Bot, ACSCharacter* Target, double Now);

	double StartTime = 0.0;
	double NextFireTime = 0.0;
	double NextMoveTime = 0.0;
	int32 ShotsInBurst = 0;
	bool bReloadRequested = false;
};

/** Move to the Actor or Vector stored in a blackboard key. */
UCLASS()
class CSFUSION_API UCSBTTask_MoveToKey : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UCSBTTask_MoveToKey();

	void Configure(FName InKey, float InAcceptanceRadius, bool bInClearKeyOnArrival, float InTimeout);

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

private:
	bool GetGoal(UBehaviorTreeComponent& OwnerComp, FVector& OutLocation, AActor*& OutActor) const;

	UPROPERTY()
	FName Key;

	UPROPERTY()
	float AcceptanceRadius = 100.f;

	UPROPERTY()
	bool bClearKeyOnArrival = false;

	UPROPERTY()
	float Timeout = 15.f;

	double StartTime = 0.0;
};

/** Pick a random reachable point to wander to (map-wide, biased away from the bot). */
UCLASS()
class CSFUSION_API UCSBTTask_FindPatrolPoint : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UCSBTTask_FindPatrolPoint();

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};

/** Take the LootTarget pickup (the authority re-validates reach and space). */
UCLASS()
class CSFUSION_API UCSBTTask_PickupLoot : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UCSBTTask_PickupLoot();

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};

/** Wait a random time in a range. */
UCLASS()
class CSFUSION_API UCSBTTask_Pause : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UCSBTTask_Pause();

	void Configure(float InMin, float InMax) { MinSeconds = InMin; MaxSeconds = InMax; }

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

private:
	UPROPERTY()
	float MinSeconds = 0.5f;

	UPROPERTY()
	float MaxSeconds = 1.5f;

	double EndTime = 0.0;
};

/**
 * The bot's awareness and housekeeping, ticking on the tree root:
 * picks and validates targets, equips the best weapon, reloads, heals,
 * chooses loot worth walking to.
 */
UCLASS()
class CSFUSION_API UCSBTService_BotBrain : public UBTService
{
	GENERATED_BODY()

public:
	UCSBTService_BotBrain();

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

private:
	void UpdateTarget(ACSBotController* Controller, ACSCharacter* Bot, class UBlackboardComponent* BB);
	void ManageInventory(ACSBotController* Controller, ACSCharacter* Bot, bool bInCombat);
	void ChooseLoot(ACSBotController* Controller, ACSCharacter* Bot, class UBlackboardComponent* BB);

	double LastInventoryAction = 0.0;
};
