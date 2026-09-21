// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "AI/CSBotController.h"

#include "AI/CSBotBehavior.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
#include "Core/CSLog.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"

ACSBotController::ACSBotController()
{
	bWantsPlayerState = false;
	PrimaryActorTick.bCanEverTick = true;

	Perception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception"));

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = Tuning.SightRadius;
	SightConfig->LoseSightRadius = Tuning.SightRadius + 500.f;
	SightConfig->PeripheralVisionAngleDegrees = 75.f;
	SightConfig->SetMaxAge(4.f);
	// Every other character is an enemy in free-for-all: detect all
	// affiliations, the brain filters to alive ACSCharacters.
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
	HearingConfig->HearingRange = 3000.f;
	HearingConfig->SetMaxAge(3.f);
	HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
	HearingConfig->DetectionByAffiliation.bDetectFriendlies = true;

	Perception->ConfigureSense(*SightConfig);
	Perception->ConfigureSense(*HearingConfig);
	Perception->SetDominantSense(UAISense_Sight::StaticClass());
	SetPerceptionComponent(*Perception);
}

ACSCharacter* ACSBotController::GetBot() const
{
	return Cast<ACSCharacter>(GetPawn());
}

void ACSBotController::SetDifficulty(ECSBotDifficulty InDifficulty)
{
	Difficulty = InDifficulty;
	Tuning = FCSBotTuning::For(InDifficulty);
	if (SightConfig && Perception)
	{
		SightConfig->SightRadius = Tuning.SightRadius;
		SightConfig->LoseSightRadius = Tuning.SightRadius + 500.f;
		Perception->ConfigureSense(*SightConfig);
	}
}

void ACSBotController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	Perception->OnTargetPerceptionUpdated.AddUniqueDynamic(this, &ACSBotController::HandleTargetPerceptionUpdated);

	if (!BehaviorTree)
	{
		UBlackboardData* BuiltBlackboard = nullptr;
		BehaviorTree = CSBotBehavior::BuildTree(this, BuiltBlackboard);
		BlackboardData = BuiltBlackboard;
	}
	if (BehaviorTree)
	{
		RunBehaviorTree(BehaviorTree);
	}

	if (ACSMatchDirector* Director = ACSMatchDirector::Get(this))
	{
		BoundDirector = Director;
		CombatEventHandle = Director->OnCombatEvent.AddUObject(this, &ACSBotController::HandleCombatEvent);
	}

	UE_LOG(LogCSAI, Log, TEXT("Bot controller possessed %s (bot %d, %s)."), *GetNameSafe(InPawn),
		GetBot() ? GetBot()->GetOwningPlayerId() : 0, *UEnum::GetValueAsString(Difficulty));
}

void ACSBotController::OnUnPossess()
{
	if (ACSMatchDirector* Director = BoundDirector.Get())
	{
		Director->OnCombatEvent.Remove(CombatEventHandle);
	}
	BoundDirector.Reset();
	Super::OnUnPossess();
}

void ACSBotController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ACSMatchDirector* Director = BoundDirector.Get())
	{
		Director->OnCombatEvent.Remove(CombatEventHandle);
	}
	Super::EndPlay(EndPlayReason);
}

bool ACSBotController::CanSee(const AActor* Target) const
{
	const ACSCharacter* Enemy = Cast<ACSCharacter>(Target);
	if (!Enemy || Enemy == GetPawn() || !Enemy->IsAliveAuthoritative() || !Perception)
	{
		return false;
	}
	TArray<AActor*> Seen;
	Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Seen);
	return Seen.Contains(Target);
}

ACSCharacter* ACSBotController::FindBestVisibleEnemy() const
{
	const APawn* Self = GetPawn();
	if (!Self || !Perception)
	{
		return nullptr;
	}
	TArray<AActor*> Seen;
	Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Seen);

	ACSCharacter* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (AActor* Actor : Seen)
	{
		ACSCharacter* Enemy = Cast<ACSCharacter>(Actor);
		if (!Enemy || Enemy == Self || !Enemy->IsAliveAuthoritative() || Enemy->IsHidden())
		{
			continue;
		}
		const float Dist = FVector::DistSquared(Enemy->GetActorLocation(), Self->GetActorLocation());
		if (Dist < BestDist)
		{
			BestDist = Dist;
			Best = Enemy;
		}
	}
	return Best;
}

void ACSBotController::HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB || !Stimulus.WasSuccessfullySensed())
	{
		return;
	}

	// A gunshot heard while not fighting: go and look.
	if (Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>() && !BB->GetValueAsObject(CSBotBehavior::KeyTarget))
	{
		BB->SetValueAsVector(CSBotBehavior::KeyLastKnownLocation, Stimulus.StimulusLocation);
	}
}

void ACSBotController::HandleCombatEvent(const FCSCombatEvent& Event)
{
	ACSCharacter* Bot = GetBot();
	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!Bot || !BB || Event.VictimId != Bot->GetOwningPlayerId() || Event.bKilled)
	{
		return;
	}

	// Shot: turn on the attacker if we can see them, otherwise go where the
	// shots came from.
	ACSCharacter* Attacker = ACSMatchDirector::FindPawnForPlayer(this, Event.InstigatorId);
	if (Attacker && Attacker != Bot && CanSee(Attacker))
	{
		if (BB->GetValueAsObject(CSBotBehavior::KeyTarget) != Attacker)
		{
			BB->SetValueAsObject(CSBotBehavior::KeyTarget, Attacker);
			NoteTargetAcquired();
		}
	}
	else if (!BB->GetValueAsObject(CSBotBehavior::KeyTarget))
	{
		BB->SetValueAsVector(CSBotBehavior::KeyLastKnownLocation, Event.FromLocation);
	}
}

bool ACSBotController::IsPickupIgnored(const AActor* Pickup) const
{
	const double* Until = IgnoredPickups.Find(Pickup);
	return Until && GetWorld() && GetWorld()->GetTimeSeconds() < *Until;
}

void ACSBotController::IgnorePickup(const AActor* Pickup, float Seconds)
{
	if (GetWorld())
	{
		IgnoredPickups.Add(Pickup, GetWorld()->GetTimeSeconds() + Seconds);
	}
}

FString ACSBotController::DescribeState() const
{
	TArray<AActor*> Seen;
	if (Perception)
	{
		Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Seen);
	}
	FString Ids;
	for (const AActor* Actor : Seen)
	{
		if (const ACSCharacter* SeenCharacter = Cast<ACSCharacter>(Actor))
		{
			Ids += FString::Printf(TEXT("%d "), SeenCharacter->GetOwningPlayerId());
		}
	}
	const UBlackboardComponent* BB = GetBlackboardComponent();
	const ACSCharacter* Target = BB ? Cast<ACSCharacter>(BB->GetValueAsObject(CSBotBehavior::KeyTarget)) : nullptr;
	return FString::Printf(TEXT("sees %d [%s], target %d"), Seen.Num(), *Ids.TrimEnd(), Target ? Target->GetOwningPlayerId() : 0);
}
