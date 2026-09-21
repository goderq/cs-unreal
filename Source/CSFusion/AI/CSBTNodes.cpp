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

	/** Rounds available to reload the weapon in this inventory slot. */
	int32 ReserveFor(const ACSPlayerInventory* Inventory, const UCSItemDefinition* WeaponItem)
	{
		if (!Inventory || !WeaponItem || WeaponItem->AmmoItemId.IsNone())
		{
			return 0;
		}
		const int32 AmmoIndex = UCSItemSettings::Get()->FindItemIndex(WeaponItem->AmmoItemId);
		return AmmoIndex == INDEX_NONE ? 0 : Inventory->CountItem(AmmoIndex);
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

	const FCSLoadoutView Loadout = Director->GetLoadout(Bot->GetOwningPlayerId());
	if (!Loadout.Weapon || Loadout.bReloading || Loadout.bGrenade)
	{
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
	int32 GrenadeSlot = INDEX_NONE;
	const TArray<FCSInventorySlot>& Slots = Inventory->GetSlots();
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const UCSItemDefinition* Item = UCSItemSettings::Get()->GetItem(Slots[i].ItemIndex);
		if (!Slots[i].IsEmpty() && Item && Item->ItemType == ECSItemType::Grenade)
		{
			GrenadeSlot = i;
			break;
		}
	}
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

	// Back to the gun (TryThrowGrenade already switched to the pistol if that was the last one).
	if (Previous != GrenadeSlot && Previous != INDEX_NONE)
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

	// Close in when far, back off when too close, strafe otherwise.
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
	ACSWorldPickup* Pickup = BB ? Cast<ACSWorldPickup>(BB->GetValueAsObject(CSBotBehavior::KeyLootTarget)) : nullptr;
	if (BB)
	{
		BB->ClearValue(CSBotBehavior::KeyLootTarget);
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
	FCSPlayerCombatRecord Record;
	if (!Director || !Inventory || !Director->GetRecord(Id, Record))
	{
		return;
	}
	const UCSItemSettings* Items = UCSItemSettings::Get();
	const FCSLoadoutView Loadout = Director->GetLoadout(Id);

	// Best usable weapon: highest damage per second that has rounds to shoot.
	int32 BestSlot = INDEX_NONE;
	float BestScore = WeaponScore(UCSCombatSettings::Get()->StarterWeapon.LoadSynchronous());
	int32 MedkitSlot = INDEX_NONE;
	int32 ArmorSlot = INDEX_NONE;
	const TArray<FCSInventorySlot>& Slots = Inventory->GetSlots();
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].IsEmpty())
		{
			continue;
		}
		const UCSItemDefinition* Item = Items->GetItem(Slots[i].ItemIndex);
		if (!Item)
		{
			continue;
		}
		if (const UCSWeaponDefinition* Weapon = ItemWeapon(Item))
		{
			const bool bCanShoot = Slots[i].AmmoInMag > 0 || ReserveFor(Inventory, Item) > 0;
			const float Score = WeaponScore(Weapon);
			if (bCanShoot && Score > BestScore)
			{
				BestScore = Score;
				BestSlot = i;
			}
		}
		else if (Item->ItemType == ECSItemType::Medkit)
		{
			MedkitSlot = i;
		}
		else if (Item->ItemType == ECSItemType::Armor)
		{
			ArmorSlot = i;
		}
	}

	if (Inventory->GetEquippedSlot() != BestSlot && !Loadout.bReloading)
	{
		Bot->BotSelectSlot(BestSlot);
		LastInventoryAction = T;
		return;
	}

	if (!bInCombat)
	{
		if (Record.Health < 65.f && MedkitSlot != INDEX_NONE)
		{
			Bot->BotSelectSlot(MedkitSlot);
			LastInventoryAction = T;
			return;
		}
		if (Record.Armor < 50.f && ArmorSlot != INDEX_NONE)
		{
			Bot->BotSelectSlot(ArmorSlot);
			LastInventoryAction = T;
			return;
		}
		// Top up the magazine between fights.
		if (Loadout.Weapon && !Loadout.bReloading && Loadout.RoundsInMag < Loadout.Weapon->MagazineSize / 2
			&& (Loadout.Reserve != 0))
		{
			Bot->BotReload();
			LastInventoryAction = T;
		}
	}
}

void UCSBTService_BotBrain::ChooseLoot(ACSBotController* Controller, ACSCharacter* Bot, UBlackboardComponent* BB)
{
	if (ACSWorldPickup* Current = Cast<ACSWorldPickup>(BB->GetValueAsObject(CSBotBehavior::KeyLootTarget)))
	{
		if (!Current->IsAvailable())
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
	const UCSItemSettings* Items = UCSItemSettings::Get();

	// What the bot carries decides what is worth a detour.
	float CarriedBest = WeaponScore(Director->GetLoadout(Id).Weapon);
	TSet<FName> WantedAmmo;
	bool bHasMedkit = false;
	bool bHasArmor = false;
	bool bHasFreeSlot = false;
	for (const FCSInventorySlot& Slot : Inventory->GetSlots())
	{
		if (Slot.IsEmpty())
		{
			bHasFreeSlot = true;
			continue;
		}
		const UCSItemDefinition* Item = Items->GetItem(Slot.ItemIndex);
		if (!Item)
		{
			continue;
		}
		if (const UCSWeaponDefinition* Weapon = ItemWeapon(Item))
		{
			CarriedBest = FMath::Max(CarriedBest, WeaponScore(Weapon));
			if (ReserveFor(Inventory, Item) < Weapon->MagazineSize * 2)
			{
				WantedAmmo.Add(Item->AmmoItemId);
			}
		}
		bHasMedkit |= Item->ItemType == ECSItemType::Medkit;
		bHasArmor |= Item->ItemType == ECSItemType::Armor;
	}

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
		if (!Item)
		{
			continue;
		}

		bool bWanted = false;
		if (const UCSWeaponDefinition* Weapon = ItemWeapon(Item))
		{
			bWanted = bHasFreeSlot && WeaponScore(Weapon) > CarriedBest * 1.1f;
		}
		else if (Item->ItemType == ECSItemType::Ammo)
		{
			bWanted = WantedAmmo.Contains(Item->ItemId);
		}
		else if (Item->ItemType == ECSItemType::Medkit)
		{
			bWanted = !bHasMedkit && (bHasFreeSlot || Record.Health < 100.f);
		}
		else if (Item->ItemType == ECSItemType::Armor)
		{
			bWanted = !bHasArmor && Record.Armor < 75.f;
		}
		if (!bWanted)
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
