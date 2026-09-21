// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v1.1 self-tests (offline, this peer is the authority):
//
//   -cstesttour                      screenshots from every few spawn points plus
//                                    an overview, for looking at a map
//   -mode=dm -cstestmodes            spawn protection (ghost look, no damage),
//                                    the shop (open, buy a rifle and a grenade),
//                                    protection and shop ending on movement,
//                                    a grenade throw and its blast
//   -mode=5v5 -roundtime=40 -cstestcomp
//                                    bots fill 5 vs 5, no protection, 15 s buy
//                                    window, friendly fire off, a round decided
//                                    and paid out, the next round starting

#include "Player/CSPlayerController.h"

#include "AIController.h"
#include "BrainComponent.h"
#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/CSLog.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameModes/CSGameState.h"
#include "Inventory/CSPlayerInventory.h"
#include "Items/CSItemDefinition.h"
#include "Items/CSItemSettings.h"
#include "Items/CSShopSettings.h"
#include "Core/CSModeSettings.h"
#include "Weapons/CSWeaponDefinition.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"
#include "UI/SCSShopPanel.h"
#include "Weapons/CSGrenade.h"

namespace
{
	int32 ShopIndexOf(FName ItemId)
	{
		const TArray<FCSShopEntry>& Entries = UCSShopSettings::Get()->Entries;
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			if (Entries[i].ItemId == ItemId)
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	int32 CountItem(const UObject* Context, int32 PlayerId, FName ItemId)
	{
		const ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(Context, PlayerId);
		const int32 Index = UCSItemSettings::Get()->FindItemIndex(ItemId);
		return (Inventory && Index != INDEX_NONE) ? Inventory->CountItem(Index) : 0;
	}

	int32 SlotOf(const UObject* Context, int32 PlayerId, FName ItemId)
	{
		const ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(Context, PlayerId);
		const int32 Index = UCSItemSettings::Get()->FindItemIndex(ItemId);
		if (!Inventory || Index == INDEX_NONE)
		{
			return INDEX_NONE;
		}
		const TArray<FCSInventorySlot>& Slots = Inventory->GetSlots();
		for (int32 i = 0; i < Slots.Num(); ++i)
		{
			if (Slots[i].ItemIndex == Index && !Slots[i].IsEmpty())
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	int32 CountGrenades(const UWorld* World)
	{
		int32 N = 0;
		for (TActorIterator<ACSGrenade> It(const_cast<UWorld*>(World)); It; ++It)
		{
			++N;
		}
		return N;
	}
}

// ---------------------------------------------------------------------------
// Map tour
// ---------------------------------------------------------------------------

void ACSPlayerController::CSTestTour()
{
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	if (!Self)
	{
		GetWorldTimerManager().SetTimer(TestModesTimer, this, &ACSPlayerController::CSTestTour, 1.f, false);
		return;
	}

	TArray<AActor*> Starts;
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		Starts.Add(*It);
	}
	Starts.Sort([](const AActor& A, const AActor& B) { return A.GetName() < B.GetName(); });
	TourPoints.Reset();
	for (int32 i = 0; i < Starts.Num(); i += 4)
	{
		TourPoints.Add(Starts[i]->GetActorTransform());
	}
	TourIndex = -1;
	const FString Map = GetWorld()->GetMapName();
	UE_LOG(LogCS, Log, TEXT("TOUR: %d spawn points, %d stops on %s"), Starts.Num(), TourPoints.Num(), *Map);

	GetWorldTimerManager().SetTimer(TestModesTimer, [this, Map]()
	{
		ACSCharacter* Pawn = Cast<ACSCharacter>(GetPawn());
		if (!Pawn)
		{
			return;
		}
		if (TourIndex >= 0)
		{
			TestScreenshot(FString::Printf(TEXT("tour_%s_%02d"), *Map.Replace(TEXT("UEDPIE_0_"), TEXT("")), TourIndex));
		}
		++TourIndex;
		if (TourIndex < TourPoints.Num())
		{
			const FTransform& T = TourPoints[TourIndex];
			Pawn->SetActorLocation(T.GetLocation() + FVector(0.f, 0.f, 20.f), false, nullptr, ETeleportType::TeleportPhysics);
			SetControlRotation(FRotator(-6.f, T.Rotator().Yaw, 0.f));
			return;
		}
		if (TourIndex == TourPoints.Num())
		{
			// Overview from high above the centre, looking down.
			if (UCharacterMovementComponent* Move = Pawn->GetCharacterMovement())
			{
				Move->SetMovementMode(MOVE_Flying);
			}
			Pawn->SetActorLocation(FVector(-3400.f, -3000.f, 3400.f), false, nullptr, ETeleportType::TeleportPhysics);
			SetControlRotation(FRotator(-42.f, 40.f, 0.f));
			return;
		}
		GetWorldTimerManager().ClearTimer(TestModesTimer);
		UE_LOG(LogCS, Log, TEXT("TOUR RESULT: %d screenshots -> TOUR DONE"), TourIndex);
	}, 2.5f, true);
}

// ---------------------------------------------------------------------------
// Deathmatch: protection, shop, grenade
// ---------------------------------------------------------------------------

void ACSPlayerController::CSTestModes()
{
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	const ACSGameState* GS = GetWorld()->GetGameState<ACSGameState>();
	if (!Self || !Director || !GS || GS->GetMatchPhase() != ECSMatchPhase::InProgress)
	{
		GetWorldTimerManager().SetTimer(TestModesTimer, this, &ACSPlayerController::CSTestModes, 1.f, false);
		return;
	}
	const int32 Me = Self->GetOwningPlayerId();
	UE_LOG(LogCS, Log, TEXT("MODES TEST: mode %s, money $%d"), *UCSModeSettings::ModeTag(GS->GetGameMode()), Director->GetMoney(Me));

	// A fresh start: money, inventory and a full protection window (and whatever
	// happened before - a stray key press in the window - undone).
	CloseMenus();
	Director->ResetForNewMatch();
	ModesStep = 1;
	GetWorldTimerManager().SetTimer(TestModesTimer, [this]()
	{
		ACSCharacter* Pawn = Cast<ACSCharacter>(GetPawn());
		ACSMatchDirector* D = ACSMatchDirector::Get(this);
		if (!Pawn || !D)
		{
			return;
		}
		const int32 Id = Pawn->GetOwningPlayerId();
		++ModesStep;

		switch (ModesStep)
		{
		case 1:
			// Waiting for the respawn (3 s).
			if (!D->IsPlayerAlive(Id))
			{
				--ModesStep;
			}
			return;

		case 2:
		{
			// Protected: no damage taken, ghost material on the body, shop open.
			const float Before = D->GetHealth(Id);
			const float Taken = D->ApplyDamage(Id, CSBots::FirstBotId + 77, 50.f, ECSHitZone::Torso);
			UMaterialInterface* BodyMat = Pawn->GetMesh() ? Pawn->GetMesh()->GetMaterial(0) : nullptr;
			const bool bGhost = BodyMat && BodyMat->GetBaseMaterial() && BodyMat->GetBaseMaterial()->GetName().Contains(TEXT("SpawnGhost"));
			const bool bOk = D->IsProtected(Id) && Taken == 0.f && D->GetHealth(Id) == Before && D->CanBuy(Id) && bGhost;
			UE_LOG(LogCS, Log, TEXT("MODES TEST RESULT: spawn protection %.1f s left, damage taken %.0f, ghost look %s, shop %s -> %s"),
				D->GetProtectionRemaining(Id), Taken, bGhost ? TEXT("yes") : TEXT("NO"), D->CanBuy(Id) ? TEXT("open") : TEXT("CLOSED"),
				bOk ? TEXT("PROTECTION OK") : TEXT("PROTECTION BROKEN"));
			ToggleShopScreen();
			ModesMoneyBefore = D->GetMoney(Id);
			return;
		}

		case 3:
			TestScreenshot(TEXT("modes_shop"));
			if (ShopPanel.IsValid())
			{
				ShopPanel->Buy(ShopIndexOf(TEXT("smg")));
				ShopPanel->Buy(ShopIndexOf(TEXT("grenade")));
			}
			return;

		case 4:
		{
			const int32 Spent = ModesMoneyBefore - D->GetMoney(Id);
			const bool bRifle = CountItem(this, Id, TEXT("smg")) == 1;
			const bool bGrenade = CountItem(this, Id, TEXT("grenade")) == 1;
			const bool bOk = bShopOpen && bRifle && bGrenade && Spent == 1550;
			UE_LOG(LogCS, Log, TEXT("MODES TEST RESULT: shop %s, bought SMG %s grenade %s, spent $%d -> %s"),
				bShopOpen ? TEXT("open") : TEXT("NOT OPEN"), bRifle ? TEXT("yes") : TEXT("no"), bGrenade ? TEXT("yes") : TEXT("no"), Spent,
				bOk ? TEXT("SHOP OK") : TEXT("SHOP BROKEN"));
			TestScreenshot(TEXT("modes_bought"));
			return;
		}

		case 5:
			ToggleShopScreen();
			// Walk: protection and shop must end.
			TestWalkDir = FRotationMatrix(FRotator(0.f, GetControlRotation().Yaw, 0.f)).GetUnitAxis(EAxis::X);
			return;

		case 6:
		{
			TestWalkDir = FVector::ZeroVector;
			const bool bOk = !D->IsProtected(Id) && !D->CanBuy(Id);
			UE_LOG(LogCS, Log, TEXT("MODES TEST RESULT: after walking -> protected %s, shop %s -> %s"),
				D->IsProtected(Id) ? TEXT("STILL") : TEXT("no"), D->CanBuy(Id) ? TEXT("STILL OPEN") : TEXT("closed"),
				bOk ? TEXT("MOVE ENDS PROTECTION OK") : TEXT("MOVE ENDS PROTECTION BROKEN"));
			ToggleShopScreen(); // must refuse now
			UE_LOG(LogCS, Log, TEXT("MODES TEST RESULT: B after protection -> shop %s -> %s"), bShopOpen ? TEXT("OPENED") : TEXT("refused"),
				bShopOpen ? TEXT("SHOP CLOSING BROKEN") : TEXT("SHOP CLOSING OK"));
			CloseMenus();
			Pawn->RequestSlot(SlotOf(this, Id, TEXT("grenade")));
			return;
		}

		case 7:
			TestScreenshot(TEXT("modes_grenade_hand"));
			ModesGrenadesBefore = CountGrenades(GetWorld());
			SetControlRotation(FRotator(12.f, GetControlRotation().Yaw, 0.f));
			Pawn->RequestThrowGrenade();
			return;

		case 8:
		{
			const int32 Flying = CountGrenades(GetWorld());
			TestScreenshot(TEXT("modes_grenade"));
			UE_LOG(LogCS, Log, TEXT("MODES TEST: grenades in the air %d, left in inventory %d"), Flying, CountItem(this, Id, TEXT("grenade")));
			ModesGrenadeThrown = Flying > ModesGrenadesBefore && CountItem(this, Id, TEXT("grenade")) == 0;
			return;
		}

		case 9:
		case 10:
		case 11:
			return; // fuse

		case 12:
		{
			// The blast has happened: the grenade removes itself shortly after exploding.
			const int32 Left = CountGrenades(GetWorld());
			const FCSLoadoutView Loadout = D->GetLoadout(Id);
			const bool bOk = ModesGrenadeThrown && Left == 0 && !Loadout.bGrenade;
			UE_LOG(LogCS, Log, TEXT("MODES TEST RESULT: grenade thrown %s, exploded %s, hand back to %s -> %s"),
				ModesGrenadeThrown ? TEXT("yes") : TEXT("NO"), Left == 0 ? TEXT("yes") : TEXT("NO"),
				Loadout.Weapon ? *Loadout.Weapon->DisplayName.ToString() : TEXT("nothing"), bOk ? TEXT("GRENADE OK") : TEXT("GRENADE BROKEN"));
			GetWorldTimerManager().ClearTimer(TestModesTimer);
			UE_LOG(LogCS, Log, TEXT("MODES TEST: done"));
			return;
		}
		default:
			return;
		}
	}, 0.8f, true);
}

// ---------------------------------------------------------------------------
// 5 vs 5: teams, buy window, rounds
// ---------------------------------------------------------------------------

void ACSPlayerController::CSTestComp()
{
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	const ACSGameState* GS = GetWorld()->GetGameState<ACSGameState>();
	if (!Self || !Director || !GS || GS->GetMatchPhase() != ECSMatchPhase::InProgress || GS->GetRoundNumber() < 1)
	{
		GetWorldTimerManager().SetTimer(TestModesTimer, this, &ACSPlayerController::CSTestComp, 0.5f, false);
		return;
	}
	const int32 Me = Self->GetOwningPlayerId();
	const bool bBuyOpen = Director->CanBuy(Me);
	const bool bUnprotected = !Director->IsProtected(Me);
	UE_LOG(LogCS, Log, TEXT("COMP TEST RESULT: round %d start -> buy window %.1f s (%s), protection %s -> %s"),
		GS->GetRoundNumber(), GS->GetBuyTimeRemaining(), bBuyOpen ? TEXT("open") : TEXT("CLOSED"), bUnprotected ? TEXT("none") : TEXT("ACTIVE"),
		bBuyOpen && bUnprotected ? TEXT("BUY WINDOW OK") : TEXT("BUY WINDOW BROKEN"));
	CompRoundAtStart = GS->GetRoundNumber();
	CompElapsed = 0.f;
	CompChecked = 0;

	GetWorldTimerManager().SetTimer(TestModesTimer, [this]()
	{
		ACSCharacter* Pawn = Cast<ACSCharacter>(GetPawn());
		ACSMatchDirector* D = ACSMatchDirector::Get(this);
		const ACSGameState* State = GetWorld()->GetGameState<ACSGameState>();
		if (!Pawn || !D || !State)
		{
			return;
		}
		CompElapsed += 0.5f;
		const int32 Id = Pawn->GetOwningPlayerId();

		// Teams, once the bots are in: 5 vs 5, and no friendly fire.
		if (CompChecked == 0 && D->CountMembers(ECSTeam::Alpha) + D->CountMembers(ECSTeam::Bravo) >= 10)
		{
			CompChecked = 1;
			const int32 A = D->CountMembers(ECSTeam::Alpha);
			const int32 B = D->CountMembers(ECSTeam::Bravo);
			int32 Mate = 0;
			for (const FCSPlayerCombatRecord& R : D->GetAllRecords())
			{
				if (R.PlayerId != Id && R.bAlive && D->AreTeammates(Id, R.PlayerId))
				{
					Mate = R.PlayerId;
					break;
				}
			}
			const float FriendlyDamage = Mate ? D->ApplyDamage(Mate, Id, 40.f, ECSHitZone::Torso) : -1.f;
			const bool bOk = A == 5 && B == 5 && FriendlyDamage == 0.f;
			UE_LOG(LogCS, Log, TEXT("COMP TEST RESULT: teams Alpha %d / Bravo %d, damage to teammate %d: %.0f -> %s"),
				A, B, Mate, FriendlyDamage, bOk ? TEXT("TEAMS OK") : TEXT("TEAMS BROKEN"));
		}

		// Buy window closes after 15 s and stays closed.
		if (CompChecked == 1 && State->GetBuyTimeRemaining() <= 0.f && State->GetWinnerTeam() == ECSTeam::None && State->GetWinnerPlayerId() != -1)
		{
			CompChecked = 2;
			const bool bOk = !D->CanBuy(Id);
			UE_LOG(LogCS, Log, TEXT("COMP TEST RESULT: after buy time -> shop %s -> %s"), D->CanBuy(Id) ? TEXT("STILL OPEN") : TEXT("closed"),
				bOk ? TEXT("BUY CLOSES OK") : TEXT("BUY CLOSES BROKEN"));
			CompMoneyBefore = D->GetMoney(Id);
		}

		// A round gets decided and the next one begins.
		if (State->GetRoundNumber() > CompRoundAtStart)
		{
			GetWorldTimerManager().ClearTimer(TestModesTimer);
			const int32 Scores = State->GetTeamScore(ECSTeam::Alpha) + State->GetTeamScore(ECSTeam::Bravo);
			const int32 Earned = D->GetMoney(Id) - CompMoneyBefore;
			const bool bEveryoneBack = D->CountAlive(ECSTeam::Alpha) == D->CountMembers(ECSTeam::Alpha)
				&& D->CountAlive(ECSTeam::Bravo) == D->CountMembers(ECSTeam::Bravo);
			const bool bOk = Scores <= 1 && Earned >= 1400 && bEveryoneBack && D->CanBuy(Id);
			UE_LOG(LogCS, Log, TEXT("COMP TEST RESULT: round %d -> score %d : %d, round money +$%d, everyone back %s, shop %s -> %s"),
				State->GetRoundNumber(), State->GetTeamScore(ECSTeam::Alpha), State->GetTeamScore(ECSTeam::Bravo), Earned,
				bEveryoneBack ? TEXT("yes") : TEXT("NO"), D->CanBuy(Id) ? TEXT("open") : TEXT("CLOSED"),
				bOk ? TEXT("ROUNDS OK") : TEXT("ROUNDS BROKEN"));
			TestScreenshot(TEXT("comp_round2"));
			return;
		}
		if (CompElapsed > 180.f)
		{
			GetWorldTimerManager().ClearTimer(TestModesTimer);
			UE_LOG(LogCS, Warning, TEXT("COMP TEST RESULT: no second round after 180 s -> ROUNDS BROKEN"));
		}
	}, 0.5f, true);
}

// ---------------------------------------------------------------------------
// Poses (third person): crouch, jump, ragdoll, protection look (-cstestposes -bots=1)
// ---------------------------------------------------------------------------

void ACSPlayerController::CSTestPoses()
{
	ACSCharacter* Self = Cast<ACSCharacter>(GetPawn());
	ACSCharacter* Bot = nullptr;
	for (TActorIterator<ACSCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsBot() && It->IsAliveAuthoritative())
		{
			Bot = *It;
			break;
		}
	}
	if (!Self || !Bot)
	{
		GetWorldTimerManager().SetTimer(TestModesTimer, this, &ACSPlayerController::CSTestPoses, 1.f, false);
		return;
	}
	PoseBot = Bot;
	ModesStep = 0;

	GetWorldTimerManager().SetTimer(TestModesTimer, [this]()
	{
		ACSCharacter* Me = Cast<ACSCharacter>(GetPawn());
		ACSCharacter* B = PoseBot.Get();
		ACSMatchDirector* D = ACSMatchDirector::Get(this);
		if (!Me || !B || !D)
		{
			return;
		}
		AAIController* AI = Cast<AAIController>(B->GetController());
		++ModesStep;

		// Keep the bot still and in front of the camera: 4 m ahead, facing us, side-on a little.
		auto Stage = [this, Me, B, AI]()
		{
			if (AI && AI->GetBrainComponent())
			{
				AI->GetBrainComponent()->StopLogic(TEXT("pose test"));
				AI->StopMovement();
				AI->ClearFocus(EAIFocusPriority::Gameplay);
			}
			const FVector Forward = FRotator(0.f, GetControlRotation().Yaw, 0.f).Vector();
			const FVector Spot = Me->GetActorLocation() + Forward * 420.f;
			B->SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
			const FRotator Facing(0.f, (-Forward).Rotation().Yaw + 35.f, 0.f);
			B->SetActorRotation(Facing);
			if (AI)
			{
				AI->SetControlRotation(Facing);
			}
			SetControlRotation(FRotator(-8.f, GetControlRotation().Yaw, 0.f));
		};

		switch (ModesStep)
		{
		case 1: Stage(); return;
		case 2: TestScreenshot(TEXT("pose_stand")); B->Crouch(); return;
		case 3: return;
		case 4: TestScreenshot(TEXT("pose_crouch")); B->UnCrouch(); return;
		case 5: B->Jump(); return;
		case 6: TestScreenshot(TEXT("pose_jump")); return;
		case 7:
			Stage();
			D->ApplyDamage(B->GetOwningPlayerId(), Me->GetOwningPlayerId(), 1000.f, ECSHitZone::Torso);
			return;
		case 8: return;
		case 9:
			TestScreenshot(TEXT("pose_ragdoll"));
			UE_LOG(LogCS, Log, TEXT("POSES TEST RESULT: bot body simulating physics after death -> %s"),
				B->GetMesh() && B->GetMesh()->IsSimulatingPhysics() ? TEXT("RAGDOLL OK") : TEXT("RAGDOLL BROKEN"));
			return;
		case 10: case 11: case 12: case 13:
			return; // respawn (3 s)
		case 14:
		{
			// Freshly respawned and protected: look at it where it stands.
			if (AI && AI->GetBrainComponent())
			{
				AI->GetBrainComponent()->StopLogic(TEXT("pose test"));
				AI->StopMovement();
			}
			const FVector Spot = B->GetActorLocation();
			const FVector Back = B->GetActorForwardVector();
			Me->SetActorLocation(Spot + Back * 380.f + FVector(0.f, 0.f, 20.f), false, nullptr, ETeleportType::TeleportPhysics);
			SetControlRotation(FRotator(-6.f, (-Back).Rotation().Yaw, 0.f));
			return;
		}
		case 15:
		{
			TestScreenshot(TEXT("pose_protected"));
			const bool bBack = !B->GetMesh()->IsSimulatingPhysics();
			UE_LOG(LogCS, Log, TEXT("POSES TEST RESULT: respawned body back on its capsule %s, protected %s -> %s"),
				bBack ? TEXT("yes") : TEXT("NO"), D->IsProtected(B->GetOwningPlayerId()) ? TEXT("yes") : TEXT("no"),
				bBack ? TEXT("RESPAWN POSE OK") : TEXT("RESPAWN POSE BROKEN"));
			GetWorldTimerManager().ClearTimer(TestModesTimer);
			return;
		}
		default:
			return;
		}
	}, 0.7f, true);
}
