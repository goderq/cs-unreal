// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "AI/CSBTNodes.h"

#include "AI/CSBotBehavior.h"
#include "AI/CSBotController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
#include "Core/CSCombatSettings.h"
#include "Core/CSLog.h"
#include "EngineUtils.h"
#include "Inventory/CSPlayerInventory.h"
#include "Items/CSItemDefinition.h"
#include "Items/CSItemSettings.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Items/CSShopSettings.h"
#include "Pickups/CSAmmoMachine.h"
#include "Pickups/CSWorldPickup.h"
#include "Weapons/CSWeaponDefinition.h"

namespace
{
	ACSBotController* BotController(UBehaviorTreeComponent& OwnerComp)
	{
		return Cast<ACSBotController>(OwnerComp.GetAIOwner());
	}

	double Now(const UObject* Context)
	{
		const UWorld* World = Context ? Context->GetWorld() : nullptr;
		return World ? World->GetTimeSeconds() : 0.0;
	}

	/** Damage per second, the one number bots compare weapons by. */
	float WeaponScore(const UCSWeaponDefinition* Weapon)
	{
		return Weapon ? Weapon->BaseDamage * FMath::Max(1, Weapon->PelletsPerShot) * Weapon->RoundsPerMinute / 60.f : 0.f;
	}

	const UCSWeaponDefinition* ItemWeapon(const UCSItemDefinition* Item)
	{
		return (Item && Item->IsWeapon()) ? Item->Weapon.LoadSynchronous() : nullptr;
	}

	/** Rounds a gun slot can still shoot: loaded plus spare. */
	int32 RoundsLeft(const ACSPlayerInventory* Inventory, int32 Slot)
	{
		FCSInventorySlot Data;
		return (Inventory && Inventory->GetSlot(Slot, Data) && !Data.IsEmpty()) ? Data.AmmoInMag + Data.Reserve : 0;
	}
}

// ---------------------------------------------------------------------------
// Decorator
// ---------------------------------------------------------------------------

void UCSBTDecorator_KeySet::Configure(FName KeyName, bool bMustBeSet)
{
	BlackboardKey.SelectedKeyName = KeyName;
	BlackboardKey.AllowedTypes.Reset();
	// OperationType is what the runtime evaluates; BasicOperation is only the
	// editor-side mirror of it (WITH_EDITORONLY_DATA, absent in game builds).
	OperationType = static_cast<uint8>(bMustBeSet ? EBasicKeyOperation::Set : EBasicKeyOperation::NotSet);
#if WITH_EDITORONLY_DATA
	BasicOperation = bMustBeSet ? EBasicKeyOperation::Set : EBasicKeyOperation::NotSet;
#endif
	NotifyObserver = EBTBlackboardRestart::ResultChange;
	// Both: a higher-priority branch interrupts lower ones as soon as its key
	// becomes set (target spotted while looting), and a branch stops itself as
	// soon as its key is cleared (target lost).
	FlowAbortMode = EBTFlowAbortMode::Both;
	NodeName = FString::Printf(TEXT("%s is %s"), *KeyName.ToString(), bMustBeSet ? TEXT("set") : TEXT("not set"));
}

// ---------------------------------------------------------------------------
// Engage
// ---------------------------------------------------------------------------

UCSBTTask_Engage::UCSBTTask_Engage()
{
	NodeName = TEXT("Engage target");
	bNotifyTick = true;
	bCreateNodeInstance = true;
}

EBTNodeResult::Type UCSBTTask_Engage::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	StartTime = Now(&OwnerComp);
	NextMoveTime = StartTime;
	ShotsInBurst = 0;
	bReloadRequested = false;
	if (ACSBotController* Controller = BotController(OwnerComp))
	{
		bMayThrowGrenade = FMath::FRand() < Controller->GetTuning().GrenadeChance;
		Controller->PickAimPart();
	}
	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type UCSBTTask_Engage::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (ACSBotController* Controller = BotController(OwnerComp))
	{
		Controller->StopMovement();
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
	}
	return EBTNodeResult::Aborted;
}

void UCSBTTask_Engage::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	ACSBotController* Controller = BotController(OwnerComp);
	ACSCharacter* Bot = Controller ? Controller->GetBot() : nullptr;
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!Bot || !BB || !Bot->IsAliveAuthoritative())
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	ACSCharacter* Target = Cast<ACSCharacter>(BB->GetValueAsObject(CSBotBehavior::KeyTarget));
	if (!Target || !Target->IsAliveAuthoritative())
	{
		BB->ClearValue(CSBotBehavior::KeyTarget);
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	if (!Controller->CanSee(Target))
	{
		// Lost sight: remember where and go look.
		BB->SetValueAsVector(CSBotBehavior::KeyLastKnownLocation, Target->GetActorLocation());
		BB->ClearValue(CSBotBehavior::KeyTarget);
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	Controller->SetFocus(Target, EAIFocusPriority::Gameplay);

	const double T = Now(&OwnerComp);
	UpdateMovement(Controller, Bot, Target, T);
	if (T - Controller->GetTargetAcquiredTime() >= Controller->GetTuning().ReactionTime)
	{
		TryFire(Controller, Bot, Target, T);
	}

	// Re-evaluate the tree every few seconds even while fighting.
	if (T - StartTime > 6.0)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
}

void UCSBTTask_Engage::TryFire(ACSBotController* Controller, ACSCharacter* Bot, ACSCharacter* Target, double T)
{
	ACSMatchDirector* Director = ACSMatchDirector::Get(Bot);
	if (!Director || T < NextFireTime)
	{
		return;
	}

	// v1.1: now and then a grenade instead, at a sensible range.
	if (bMayThrowGrenade && TryThrowGrenade(Controller, Bot, Target, Director))
	{
		bMayThrowGrenade = false;
		NextFireTime = T + 0.9;
		return;
	}

	// A flashbang leaves it blind: no shooting until it wears off.
	if (Director->IsBlinded(Bot->GetOwningPlayerId()))
	{
		return;
	}

	const FCSLoadoutView Loadout = Director->GetLoadout(Bot->GetOwningPlayerId());
	if (!Loadout.Weapon || Loadout.bReloading || Loadout.bGrenade)
	{
		return;
	}
	if (Loadout.bKnife)
	{
		// Only a knife left: swing once the target is within reach (the
		// movement closes in, see UpdateMovement). A stab from behind.
		const float Distance = FVector::Dist(Bot->GetActorLocation(), Target->GetActorLocation());
		if (Distance > Loadout.Weapon->MeleeRange + 45.f)
		{
			return;
		}
		FVector Eye;
		FVector Unused;
		Bot->GetAimRay(Eye, Unused);
		const FVector ToTarget = (Target->GetActorLocation() - Eye).GetSafeNormal();
		const bool bBehind = FVector::DotProduct(Target->GetActorForwardVector(), ToTarget) > 0.5f;
		Bot->BotMelee(Eye, ToTarget, /*bHeavy*/ bBehind);
		NextFireTime = T + (bBehind ? Loadout.Weapon->MeleeHeavyInterval : Loadout.Weapon->MeleeInterval) * FMath::FRandRange(1.05f, 1.4f);
		return;
	}
	if (Loadout.RoundsInMag <= 0)
	{
		if (!bReloadRequested)
		{
			bReloadRequested = true;
			Bot->BotReload();
		}
		NextFireTime = T + 0.3;
		return;
	}
	bReloadRequested = false;

	const FCSBotTuning& Tuning = Controller->GetTuning();

	// The view turns smoothly toward the chosen body part (see the controller);
	// the trigger is only pulled once it is actually there.
	if (Controller->GetAimErrorTo(Target) > Tuning.FireConeDegrees)
	{
		return;
	}

	FVector Eye;
	FVector Unused;
	Bot->GetAimRay(Eye, Unused);
	const FVector View = Controller->GetControlRotation().Vector();

	// Human-like error around where it looks, a bit worse while moving.
	const float Error = FMath::DegreesToRadians(Tuning.AimErrorDegrees * (Bot->GetVelocity().Size2D() > 50.f ? 1.35f : 1.f));
	Bot->BotFire(Eye, FMath::VRandCone(View, Error));

	// Pace to the weapon's rate (the authority enforces it anyway), with
	// bursts and pauses; each burst goes for a newly chosen body part.
	const float Interval = Loadout.Weapon->GetFireInterval();
	NextFireTime = T + Interval * FMath::FRandRange(1.05f, 1.3f);
	if (!Loadout.Weapon->bAutomatic || ++ShotsInBurst >= Tuning.BurstShots)
	{
		ShotsInBurst = 0;
		if (Loadout.Weapon->bAutomatic)
		{
			NextFireTime += Tuning.BurstPause * FMath::FRandRange(0.7f, 1.3f);
		}
		Controller->PickAimPart();
	}
}

bool UCSBTTask_Engage::TryThrowGrenade(ACSBotController* Controller, ACSCharacter* Bot, ACSCharacter* Target, ACSMatchDirector* Director)
{
	const int32 Id = Bot->GetOwningPlayerId();
	ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(Bot, Id);
	if (!Inventory)
	{
		return false;
	}
	const float Distance = FVector::Dist2D(Bot->GetActorLocation(), Target->GetActorLocation());
	if (Distance < 700.f || Distance > 1900.f)
	{
		return false;
	}
	// HE first; a flashbang when that is all there is.
	const int32 GrenadeSlot = Inventory->HasItemInSlot(CSLoadout::Frag) ? CSLoadout::Frag
		: (Inventory->HasItemInSlot(CSLoadout::Flash) ? CSLoadout::Flash : INDEX_NONE);
	if (GrenadeSlot == INDEX_NONE)
	{
		return false;
	}

	const int32 Previous = Inventory->GetEquippedSlot();
	Inventory->SetEquippedSlot(GrenadeSlot);

	// Lob: aim at the feet, raised more the farther away the target is.
	FVector Eye;
	FVector Unused;
	Bot->GetAimRay(Eye, Unused);
	FRotator Lob = (Target->GetActorLocation() - FVector(0.f, 0.f, 60.f) - Eye).Rotation();
	Lob.Pitch += FMath::Clamp(6.f + Distance / 130.f, 8.f, 24.f);
	const bool bThrown = Director->TryThrowGrenade(Id, Eye, Lob.Vector(), Eye, Bot->GetVelocity());

	// Back to the gun (TryThrowGrenade already switched away if that was the last one).
	if (Previous != GrenadeSlot)
	{
		Inventory->SetEquippedSlot(Previous);
	}
	if (bThrown)
	{
		Bot->PlayThrowPresentation(/*bFromRelease*/ true);
		UE_LOG(LogCSAI, Log, TEXT("Bot %d threw a grenade at %d (%.0f cm)."), Id, Target->GetOwningPlayerId(), Distance);
	}
	return bThrown;
}

void UCSBTTask_Engage::UpdateMovement(ACSBotController* Controller, ACSCharacter* Bot, ACSCharacter* Target, double T)
{
	if (T < NextMoveTime)
	{
		return;
	}
	NextMoveTime = T + Controller->GetTuning().StrafeInterval * FMath::FRandRange(0.7f, 1.3f);

	const FVector ToTarget = Target->GetActorLocation() - Bot->GetActorLocation();
	const float Distance = ToTarget.Size2D();
	const FVector Forward = ToTarget.GetSafeNormal2D();
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Forward) * (FMath::RandBool() ? 1.f : -1.f);

	// Close in when far, back off when too close, strafe otherwise. With only
	// the knife in hand: straight at the target.
	const ACSMatchDirector* Director = ACSMatchDirector::Get(Bot);
	if (Director && Director->GetLoadout(Bot->GetOwningPlayerId()).bKnife)
	{
		NextMoveTime = T + 0.35;
		Controller->MoveToLocation(Target->GetActorLocation() - Forward * 60.f, 20.f, /*bStopOnOverlap*/ false);
		return;
	}
	FVector Offset = Side * FMath::FRandRange(150.f, 350.f);
	if (Distance > 1800.f)
	{
		Offset += Forward * 400.f;
	}
	else if (Distance < 450.f)
	{
		Offset -= Forward * 250.f;
	}

	FVector Destination = Bot->GetActorLocation() + Offset;
	if (const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Bot->GetWorld()))
	{
		FNavLocation Projected;
		if (Nav->ProjectPointToNavigation(Destination, Projected, FVector(200.f, 200.f, 300.f)))
		{
			Destination = Projected.Location;
		}
	}
	// bCanStrafe keeps the bot facing its focus (the target) while it moves.
	Controller->MoveToLocation(Destination, 30.f, false, true, false, true);
}

// ---------------------------------------------------------------------------
// Move to key
// ---------------------------------------------------------------------------

UCSBTTask_MoveToKey::UCSBTTask_MoveToKey()
{
	NodeName = TEXT("Move to key");
	bNotifyTick = true;
	bCreateNodeInstance = true;
}

void UCSBTTask_MoveToKey::Configure(FName InKey, float InAcceptanceRadius, bool bInClearKeyOnArrival, float InTimeout)
{
	Key = InKey;
	AcceptanceRadius = InAcceptanceRadius;
	bClearKeyOnArrival = bInClearKeyOnArrival;
	Timeout = InTimeout;
	NodeName = FString::Printf(TEXT("Move to %s"), *InKey.ToString());
}

bool UCSBTTask_MoveToKey::GetGoal(UBehaviorTreeComponent& OwnerComp, FVector& OutLocation, AActor*& OutActor) const
{
	const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		return false;
	}
	OutActor = Cast<AActor>(BB->GetValueAsObject(Key));
	if (OutActor)
	{
		OutLocation = OutActor->GetActorLocation();
		return true;
	}
	OutLocation = BB->GetValueAsVector(Key);
	return FAISystem::IsValidLocation(OutLocation);
}

EBTNodeResult::Type UCSBTTask_MoveToKey::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ACSBotController* Controller = BotController(OwnerComp);
	FVector Goal;
	AActor* GoalActor = nullptr;
	if (!Controller || !GetGoal(OwnerComp, Goal, GoalActor))
	{
		return EBTNodeResult::Failed;
	}

	const EPathFollowingRequestResult::Type Result = GoalActor
		? Controller->MoveToActor(GoalActor, AcceptanceRadius, true, true, false)
		: Controller->MoveToLocation(Goal, AcceptanceRadius, true, true, true, false);

	if (Result == EPathFollowingRequestResult::Failed)
	{
		if (bClearKeyOnArrival)
		{
			OwnerComp.GetBlackboardComponent()->ClearValue(Key);
		}
		return EBTNodeResult::Failed;
	}
	if (Result == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		if (bClearKeyOnArrival)
		{
			OwnerComp.GetBlackboardComponent()->ClearValue(Key);
		}
		return EBTNodeResult::Succeeded;
	}
	StartTime = Now(&OwnerComp);
	return EBTNodeResult::InProgress;
}

void UCSBTTask_MoveToKey::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	ACSBotController* Controller = BotController(OwnerComp);
	if (!Controller || !Controller->GetBot() || !Controller->GetBot()->IsAliveAuthoritative())
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	const EPathFollowingStatus::Type Status = Controller->GetMoveStatus();
	const bool bTimedOut = Now(&OwnerComp) - StartTime > Timeout;
	if (Status == EPathFollowingStatus::Idle || bTimedOut)
	{
		FVector Goal;
		AActor* GoalActor = nullptr;
		const bool bHaveGoal = GetGoal(OwnerComp, Goal, GoalActor);
		const bool bArrived = bHaveGoal &&
			FVector::Dist2D(Goal, Controller->GetBot()->GetActorLocation()) <= AcceptanceRadius + 120.f;
		if (bClearKeyOnArrival)
		{
			OwnerComp.GetBlackboardComponent()->ClearValue(Key);
		}
		if (bTimedOut)
		{
			Controller->StopMovement();
		}
		FinishLatentTask(OwnerComp, bArrived ? EBTNodeResult::Succeeded : EBTNodeResult::Failed);
	}
}

EBTNodeResult::Type UCSBTTask_MoveToKey::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (ACSBotController* Controller = BotController(OwnerComp))
	{
		Controller->StopMovement();
	}
	return EBTNodeResult::Aborted;
}

// ---------------------------------------------------------------------------
// Patrol point
// ---------------------------------------------------------------------------

UCSBTTask_FindPatrolPoint::UCSBTTask_FindPatrolPoint()
{
	NodeName = TEXT("Find patrol point");
}

EBTNodeResult::Type UCSBTTask_FindPatrolPoint::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ACSBotController* Controller = BotController(OwnerComp);
	ACSCharacter* Bot = Controller ? Controller->GetBot() : nullptr;
	const UNavigationSystemV1* Nav = Bot ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(Bot->GetWorld()) : nullptr;
	if (!Nav)
	{
		return EBTNodeResult::Failed;
	}

	// Try a few map-wide points and keep one that is not right next to us, so
	// bots actually roam the map instead of shuffling on the spot.
	FNavLocation Best;
	float BestDistance = -1.f;
	for (int32 Attempt = 0; Attempt < 6; ++Attempt)
	{
		FNavLocation Candidate;
		if (Nav->GetRandomReachablePointInRadius(Bot->GetActorLocation(), 3000.f, Candidate))
		{
			const float Distance = FVector::Dist2D(Candidate.Location, Bot->GetActorLocation());
			if (Distance > BestDistance)
			{
				Best = Candidate;
				BestDistance = Distance;
			}
			if (Distance > 900.f)
			{
				break;
			}
		}
	}
	if (BestDistance < 0.f)
	{
		return EBTNodeResult::Failed;
	}
	OwnerComp.GetBlackboardComponent()->SetValueAsVector(CSBotBehavior::KeyPatrolLocation, Best.Location);
	return EBTNodeResult::Succeeded;
}

// ---------------------------------------------------------------------------
// Pickup
// ---------------------------------------------------------------------------

UCSBTTask_PickupLoot::UCSBTTask_PickupLoot()
{
	NodeName = TEXT("Pick up loot");
}

EBTNodeResult::Type UCSBTTask_PickupLoot::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ACSBotController* Controller = BotController(OwnerComp);
	ACSCharacter* Bot = Controller ? Controller->GetBot() : nullptr;
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	UObject* Target = BB ? BB->GetValueAsObject(CSBotBehavior::KeyLootTarget) : nullptr;
	ACSWorldPickup* Pickup = Cast<ACSWorldPickup>(Target);
	if (BB)
	{
		BB->ClearValue(CSBotBehavior::KeyLootTarget);
	}
	// v2.0: an ammo machine is a "loot" target too; buying is its pickup.
	if (ACSAmmoMachine* Machine = Cast<ACSAmmoMachine>(Target); Machine && Bot)
	{
		Bot->BotBuyAmmo(Machine);
		Controller->IgnorePickup(Machine, 30.f);
		return EBTNodeResult::Succeeded;
	}
	if (!Bot || !Pickup || !Pickup->IsAvailable())
	{
		return EBTNodeResult::Failed;
	}
	Bot->BotPickup(Pickup);
	// If the authority refused it (inventory full, contested), do not keep
	// walking back to it; if it was taken it is gone anyway.
	Controller->IgnorePickup(Pickup, 20.f);
	return EBTNodeResult::Succeeded;
}

// ---------------------------------------------------------------------------
// Pause
// ---------------------------------------------------------------------------

UCSBTTask_Pause::UCSBTTask_Pause()
{
	NodeName = TEXT("Pause");
	bNotifyTick = true;
	bCreateNodeInstance = true;
}

EBTNodeResult::Type UCSBTTask_Pause::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	EndTime = Now(&OwnerComp) + FMath::FRandRange(MinSeconds, MaxSeconds);
	return EBTNodeResult::InProgress;
}

void UCSBTTask_Pause::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	if (Now(&OwnerComp) >= EndTime)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
}

// ---------------------------------------------------------------------------
// Brain service
// ---------------------------------------------------------------------------

UCSBTService_BotBrain::UCSBTService_BotBrain()
{
	NodeName = TEXT("Bot brain");
	Interval = 0.25f;
	RandomDeviation = 0.05f;
	bCreateNodeInstance = true;
	bNotifyTick = true;
}

void UCSBTService_BotBrain::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	ACSBotController* Controller = BotController(OwnerComp);
	ACSCharacter* Bot = Controller ? Controller->GetBot() : nullptr;
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!Bot || !BB)
	{
		return;
	}

	if (!Bot->IsAliveAuthoritative())
	{
		// Dead: forget everything; the tree restarts cleanly after respawn.
		BB->ClearValue(CSBotBehavior::KeyTarget);
		BB->ClearValue(CSBotBehavior::KeyLastKnownLocation);
		BB->ClearValue(CSBotBehavior::KeyLootTarget);
		Controller->StopMovement();
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
		return;
	}

	UpdateTarget(Controller, Bot, BB);
	const bool bInCombat = BB->GetValueAsObject(CSBotBehavior::KeyTarget) != nullptr;
	ManageInventory(Controller, Bot, bInCombat);
	if (!bInCombat)
	{
		ChooseLoot(Controller, Bot, BB);
	}
}

void UCSBTService_BotBrain::UpdateTarget(ACSBotController* Controller, ACSCharacter* Bot, UBlackboardComponent* BB)
{
	ACSCharacter* Current = Cast<ACSCharacter>(BB->GetValueAsObject(CSBotBehavior::KeyTarget));
	if (Current && (!Current->IsAliveAuthoritative() || Current->IsHidden()))
	{
		BB->ClearValue(CSBotBehavior::KeyTarget);
		Current = nullptr;
	}
	if (Current)
	{
		return;
	}
	if (ACSCharacter* Enemy = Controller->FindBestVisibleEnemy())
	{
		BB->SetValueAsObject(CSBotBehavior::KeyTarget, Enemy);
		BB->ClearValue(CSBotBehavior::KeyLootTarget);
		Controller->NoteTargetAcquired();
		UE_LOG(LogCSAI, Verbose, TEXT("Bot %d spotted player %d."), Bot->GetOwningPlayerId(), Enemy->GetOwningPlayerId());
	}
}

void UCSBTService_BotBrain::ManageInventory(ACSBotController* Controller, ACSCharacter* Bot, bool bInCombat)
{
	const double T = Now(Bot);
	if (T - LastInventoryAction < 1.0)
	{
		return;
	}

	const int32 Id = Bot->GetOwningPlayerId();
	const ACSMatchDirector* Director = ACSMatchDirector::Get(Bot);
	const ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(Bot, Id);
	if (!Director || !Inventory)
	{
		return;
	}
	const FCSLoadoutView Loadout = Director->GetLoadout(Id);

	// Best gun that can still shoot: the higher damage per second of the
	// primary and the pistol. Nothing left in either means the knife.
	int32 BestSlot = CSLoadout::Knife;
	float BestScore = 0.f;
	for (const int32 Slot : { CSLoadout::Primary, CSLoadout::Pistol })
	{
		const UCSWeaponDefinition* Weapon = ItemWeapon(Inventory->GetItemInSlot(Slot));
		if (Weapon && Weapon->IsFirearm() && RoundsLeft(Inventory, Slot) > 0 && WeaponScore(Weapon) > BestScore)
		{
			BestScore = WeaponScore(Weapon);
			BestSlot = Slot;
		}
	}

	// A grenade in hand is only held for the moment of a throw.
	const bool bHoldingGrenade = CSLoadout::IsGrenadeSlot(Inventory->GetEquippedSlot());
	if ((Inventory->GetEquippedSlot() != BestSlot || bHoldingGrenade) && !Loadout.bReloading)
	{
		Bot->BotSelectSlot(BestSlot);
		LastInventoryAction = T;
		return;
	}

	// Top up the magazine between fights.
	if (!bInCombat && Loadout.IsFirearm() && !Loadout.bReloading
		&& Loadout.RoundsInMag < Loadout.Weapon->MagazineSize / 2 && Loadout.Reserve > 0)
	{
		Bot->BotReload();
		LastInventoryAction = T;
	}
}

void UCSBTService_BotBrain::ChooseLoot(ACSBotController* Controller, ACSCharacter* Bot, UBlackboardComponent* BB)
{
	if (UObject* Current = BB->GetValueAsObject(CSBotBehavior::KeyLootTarget))
	{
		const ACSWorldPickup* Pickup = Cast<ACSWorldPickup>(Current);
		if (Pickup && !Pickup->IsAvailable())
		{
			BB->ClearValue(CSBotBehavior::KeyLootTarget);
		}
		return;
	}

	const int32 Id = Bot->GetOwningPlayerId();
	const ACSMatchDirector* Director = ACSMatchDirector::Get(Bot);
	const ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(Bot, Id);
	FCSPlayerCombatRecord Record;
	if (!Director || !Inventory || !Director->GetRecord(Id, Record))
	{
		return;
	}

	// 1. Running low on rounds with money to spare: the nearest ammo machine.
	bool bLow = false;
	for (const int32 Slot : { CSLoadout::Primary, CSLoadout::Pistol })
	{
		const UCSWeaponDefinition* Weapon = ItemWeapon(Inventory->GetItemInSlot(Slot));
		if (Weapon && Weapon->IsFirearm() && RoundsLeft(Inventory, Slot) < Weapon->MagazineSize * 2)
		{
			bLow = true;
		}
	}
	if (bLow && Record.Money >= UCSShopSettings::Get()->AmmoMachinePrice)
	{
		ACSAmmoMachine* Nearest = nullptr;
		float NearestDistance = 4500.f;
		for (ACSAmmoMachine* Machine : ACSAmmoMachine::GetAllSorted(Bot))
		{
			const float Distance = FVector::Dist(Machine->GetActorLocation(), Bot->GetActorLocation());
			if (Distance < NearestDistance && !Controller->IsPickupIgnored(Machine))
			{
				NearestDistance = Distance;
				Nearest = Machine;
			}
		}
		if (Nearest)
		{
			BB->SetValueAsObject(CSBotBehavior::KeyLootTarget, Nearest);
			return;
		}
	}

	// 2. A better gun on the floor than the one carried in that slot.
	ACSWorldPickup* Best = nullptr;
	float BestDistance = 3000.f;
	for (TActorIterator<ACSWorldPickup> It(Bot->GetWorld()); It; ++It)
	{
		ACSWorldPickup* Pickup = *It;
		if (!Pickup->IsAvailable() || Controller->IsPickupIgnored(Pickup))
		{
			continue;
		}
		const UCSItemDefinition* Item = Pickup->GetItemDefinition();
		const UCSWeaponDefinition* Weapon = ItemWeapon(Item);
		const int32 Slot = ACSPlayerInventory::SlotForItem(Item);
		if (!Weapon || !CSLoadout::IsDroppable(Slot) || Pickup->GetAmmoInMag() + Pickup->GetReserve() <= 0)
		{
			continue;
		}
		const UCSWeaponDefinition* Carried = ItemWeapon(Inventory->GetItemInSlot(Slot));
		const float CarriedScore = (Carried && RoundsLeft(Inventory, Slot) > 0) ? WeaponScore(Carried) : 0.f;
		if (WeaponScore(Weapon) <= CarriedScore * 1.1f)
		{
			continue;
		}

		const float Distance = FVector::Dist(Pickup->GetActorLocation(), Bot->GetActorLocation());
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Pickup;
		}
	}

	if (Best)
	{
		BB->SetValueAsObject(CSBotBehavior::KeyLootTarget, Best);
	}
}
