// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "AI/CSBotController.h"

#include "AI/CSBotBehavior.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
#include "Core/CSLog.h"
#include "Core/CSAuthority.h"
#include "Inventory/CSPlayerInventory.h"
#include "Items/CSItemDefinition.h"
#include "Items/CSItemSettings.h"
#include "Items/CSShopSettings.h"
#include "AISystem.h"
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
	// Blinded by a flashbang: sees nothing until it wears off.
	if (const ACSMatchDirector* Director = ACSMatchDirector::Get(this); Director && GetBot() && Director->IsBlinded(GetBot()->GetOwningPlayerId()))
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
	if (const ACSMatchDirector* Blind = ACSMatchDirector::Get(this); Blind && GetBot() && Blind->IsBlinded(GetBot()->GetOwningPlayerId()))
	{
		return nullptr;
	}
	TArray<AActor*> Seen;
	Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Seen);

	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	ACSCharacter* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (AActor* Actor : Seen)
	{
		ACSCharacter* Enemy = Cast<ACSCharacter>(Actor);
		// v1.1: never teammates; spawn-protected players cannot be hurt, so ignore them.
		if (!Enemy || Enemy == Self || !Enemy->IsAliveAuthoritative() || Enemy->IsHidden() || !IsEnemy(Enemy)
			|| (Director && Director->IsProtected(Enemy->GetOwningPlayerId())))
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
	if (Attacker && Attacker != Bot && IsEnemy(Attacker) && CanSee(Attacker))
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

// ---------------------------------------------------------------------------
// v1.1 aiming, teams, shopping
// ---------------------------------------------------------------------------

bool ACSBotController::IsEnemy(const AActor* Other) const
{
	const ACSCharacter* Bot = GetBot();
	const ACSCharacter* OtherCharacter = Cast<ACSCharacter>(Other);
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	if (!Bot || !OtherCharacter || OtherCharacter == Bot)
	{
		return false;
	}
	return !(Director && Director->AreTeammates(Bot->GetOwningPlayerId(), OtherCharacter->GetOwningPlayerId()));
}

void ACSBotController::PickAimPart()
{
	float Total = 0.f;
	for (const float W : Tuning.PartWeights)
	{
		Total += FMath::Max(0.f, W);
	}
	float Roll = FMath::FRand() * Total;
	for (int32 i = 0; i < static_cast<int32>(ECSBotAimPart::Count); ++i)
	{
		Roll -= FMath::Max(0.f, Tuning.PartWeights[i]);
		if (Roll <= 0.f)
		{
			AimPart = static_cast<ECSBotAimPart>(i);
			return;
		}
	}
	AimPart = ECSBotAimPart::Chest;
}

FVector ACSBotController::GetAimPoint(const AActor* Target) const
{
	if (!Target)
	{
		return FVector::ZeroVector;
	}
	// Offsets from the capsule centre (half height 88): up, right.
	float Up = 32.f;
	float Right = 0.f;
	switch (AimPart)
	{
	case ECSBotAimPart::Head:		Up = 64.f; break;
	case ECSBotAimPart::Chest:		Up = 32.f; break;
	case ECSBotAimPart::Stomach:	Up = 10.f; break;
	case ECSBotAimPart::Pelvis:		Up = -8.f; break;
	case ECSBotAimPart::LeftArm:	Up = 28.f; Right = -24.f; break;
	case ECSBotAimPart::RightArm:	Up = 28.f; Right = 24.f; break;
	case ECSBotAimPart::LeftLeg:	Up = -48.f; Right = -11.f; break;
	case ECSBotAimPart::RightLeg:	Up = -48.f; Right = 11.f; break;
	default: break;
	}
	const ACSCharacter* Enemy = Cast<ACSCharacter>(Target);
	if (Enemy && Enemy->GetStance() == ECSStanceState::Crouching)
	{
		Up = Up * 0.7f - 18.f;
	}
	FVector Point = Target->GetActorLocation() + FVector(0.f, 0.f, Up) + Target->GetActorRightVector() * Right;
	// Better bots lead a moving target a little, to make up for turning lag.
	Point += Target->GetVelocity() * (0.12f * Tuning.LeadFactor);
	return Point;
}

float ACSBotController::GetAimErrorTo(const AActor* Target) const
{
	const APawn* Bot = GetPawn();
	if (!Bot || !Target)
	{
		return 180.f;
	}
	const FVector Want = (GetAimPoint(Target) - Bot->GetPawnViewLocation()).GetSafeNormal();
	const FVector Have = GetControlRotation().Vector();
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Want, Have), -1.f, 1.f)));
}

FVector ACSBotController::GetFocalPointOnActor(const AActor* Actor) const
{
	if (Cast<ACSCharacter>(Actor))
	{
		return GetAimPoint(Actor);
	}
	return Super::GetFocalPointOnActor(Actor);
}

void ACSBotController::UpdateControlRotation(float DeltaTime, bool bUpdatePawn)
{
	APawn* const MyPawn = GetPawn();
	if (!MyPawn || DeltaTime <= 0.f)
	{
		return;
	}

	const FRotator Current = GetControlRotation();
	FRotator Desired = Current;
	const FVector Focal = GetFocalPoint();
	if (FAISystem::IsValidLocation(Focal))
	{
		Desired = (Focal - MyPawn->GetPawnViewLocation()).Rotation();
		// A living hand is never perfectly still.
		Desired.Yaw += Sway.X;
		Desired.Pitch += Sway.Y;
	}
	else if (bSetControlRotationFromPawnOrientation)
	{
		Desired = MyPawn->GetActorRotation();
	}

	// Ease toward the aim (fast when far, gentle when close), capped by the
	// turn rate of the difficulty - no instant snaps.
	const FRotator Delta = (Desired - Current).GetNormalized();
	const float Alpha = 1.f - FMath::Exp(-Tuning.TurnResponsiveness * DeltaTime);
	const float MaxStep = Tuning.TurnSpeed * DeltaTime;
	FRotator NewRotation = Current;
	NewRotation.Yaw += FMath::Clamp(Delta.Yaw * Alpha, -MaxStep, MaxStep);
	NewRotation.Pitch += FMath::Clamp(Delta.Pitch * Alpha, -MaxStep, MaxStep);
	NewRotation.Roll = 0.f;
	NewRotation = NewRotation.GetNormalized();
	NewRotation.Pitch = FMath::Clamp(NewRotation.Pitch, -85.f, 85.f);
	SetControlRotation(NewRotation);

	if (bUpdatePawn && !MyPawn->GetActorRotation().Equals(NewRotation, 1.e-3f))
	{
		MyPawn->FaceRotation(NewRotation, DeltaTime);
	}
}

void ACSBotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const double T = GetWorld()->GetTimeSeconds();
	if (T >= NextSwayChange)
	{
		NextSwayChange = T + FMath::FRandRange(0.5f, 1.3f);
		SwayTarget = FVector2D(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-0.6f, 0.6f)) * Tuning.SwayDegrees;
	}
	Sway = FMath::Vector2DInterpTo(Sway, SwayTarget, DeltaSeconds, 2.5f);

	TryShopping();
}

void ACSBotController::TryShopping()
{
	ACSCharacter* Bot = GetBot();
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	if (!Bot || !Director || !UCSAuthority::IsGameAuthority(this))
	{
		return;
	}
	const int32 Id = Bot->GetOwningPlayerId();
	FCSPlayerCombatRecord Record;
	if (!Director->GetRecord(Id, Record) || !Record.bAlive || Record.RespawnCounter == ShoppedForLife || !Director->CanBuy(Id))
	{
		return;
	}
	ShoppedForLife = Record.RespawnCounter;

	const UCSShopSettings* Shop = UCSShopSettings::Get();
	auto IndexOf = [Shop](const TCHAR* ItemId) -> int32
	{
		for (int32 i = 0; i < Shop->Entries.Num(); ++i)
		{
			if (Shop->Entries[i].ItemId == FName(ItemId))
			{
				return i;
			}
		}
		return INDEX_NONE;
	};
	auto Money = [Director, Id]() { return Director->GetMoney(Id); };
	auto Buy = [Director, Id, &IndexOf](const TCHAR* ItemId)
	{
		const int32 I = IndexOf(ItemId);
		return I != INDEX_NONE && Director->TryBuy(Id, I) == ECSBuyResult::Ok;
	};

	// Already carrying a primary (a survivor in 5 vs 5)? Then only extras.
	const ACSPlayerInventory* Carried = ACSPlayerInventory::Find(this, Id);
	const bool bHasWeapon = Carried && Carried->HasItemInSlot(CSLoadout::Primary);

	if (!bHasWeapon)
	{
		const bool bHard = Difficulty == ECSBotDifficulty::Hard;
		if (bHard && Money() >= 4750 && FMath::FRand() < 0.15f)
		{
			Buy(TEXT("sniper"));
		}
		else if (Money() >= 3100 && FMath::RandBool())
		{
			Buy(TEXT("m4"));
		}
		else if (Money() >= 2700)
		{
			Buy(TEXT("ak47"));
		}
		else if (Money() >= 1250)
		{
			Buy(FMath::RandBool() ? TEXT("smg") : TEXT("shotgun"));
		}
	}
	if (Record.Armor < 50.f && Money() >= 650)
	{
		Buy(TEXT("armor"));
	}
	if (Tuning.GrenadeChance > 0.f && Money() >= 300 && FMath::FRand() < 0.6f)
	{
		Buy(TEXT("grenade"));
	}
	if (Tuning.GrenadeChance > 0.f && Money() >= 200 && FMath::FRand() < 0.35f)
	{
		Buy(TEXT("flashbang"));
	}
	// Whatever was bought, the best gun goes in hand; grenades are thrown on purpose.
	if (ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(this, Id))
	{
		Inventory->SetEquippedSlot(Inventory->GetBestWeaponSlot());
	}
	UE_LOG(LogCSAI, Log, TEXT("Bot %d shopped, $%d left."), Id, Money());
}
