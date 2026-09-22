// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Combat/CSMatchDirector.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CSMatchDirector.fusion)

#include "Characters/CSCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Core/CSAuthority.h"
#include "Core/CSCombatSettings.h"
#include "Core/CSLog.h"
#include "Core/CSModeSettings.h"
#include "EngineUtils.h"
#include "GameModes/CSGameMode.h"
#include "GameModes/CSGameState.h"
#include "Inventory/CSPlayerInventory.h"
#include "Items/CSItemDefinition.h"
#include "Items/CSItemSettings.h"
#include "Pickups/CSWorldPickup.h"
#include "Net/UnrealNetwork.h"
#include "Weapons/CSWeaponDefinition.h"
#include "Weapons/CSGrenade.h"

namespace
{
	ACSGameState* ModeState(const UObject* Context)
	{
		const UWorld* World = Context ? Context->GetWorld() : nullptr;
		return World ? World->GetGameState<ACSGameState>() : nullptr;
	}

	/** Rules of the running mode. Before the GameState exists: Deathmatch. */
	const FCSModeRules& ModeRules(const UObject* Context)
	{
		const ACSGameState* GS = ModeState(Context);
		return GS ? GS->GetRules() : UCSModeSettings::Rules(ECSGameModeType::Deathmatch);
	}
}

ACSMatchDirector::ACSMatchDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f; // Respawn and reload timers only.

	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);

	// MasterClient ownership is what makes this store unforgeable: only the
	// elected authority may write it, and ownership follows master migration
	// automatically.
	FusionActor = CreateDefaultSubobject<UFusionActorComponent>(TEXT("FusionActor"));
	FusionActor->Ownership = EFusionObjectOwnerFlags::MasterClient;
}

void ACSMatchDirector::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogCSAuth, Log, TEXT("MatchDirector ready. Local peer is authority: %s"),
		UCSAuthority::IsGameAuthority(this) ? TEXT("YES") : TEXT("no"));
}

void ACSMatchDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACSMatchDirector, Records);
}

ACSMatchDirector* ACSMatchDirector::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<ACSMatchDirector> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Reads
// ---------------------------------------------------------------------------

int32 ACSMatchDirector::FindRecordIndex(int32 PlayerId) const
{
	if (PlayerId == 0)
	{
		return INDEX_NONE;
	}

	for (int32 Index = 0; Index < Records.Num(); ++Index)
	{
		if (Records[Index].PlayerId == PlayerId)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

FCSPlayerCombatRecord* ACSMatchDirector::FindRecordMutable(int32 PlayerId)
{
	const int32 Index = FindRecordIndex(PlayerId);
	return Index == INDEX_NONE ? nullptr : &Records[Index];
}

bool ACSMatchDirector::GetRecord(int32 PlayerId, FCSPlayerCombatRecord& OutRecord) const
{
	const int32 Index = FindRecordIndex(PlayerId);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	OutRecord = Records[Index];
	return true;
}

bool ACSMatchDirector::IsPlayerAlive(int32 PlayerId) const
{
	const int32 Index = FindRecordIndex(PlayerId);
	return Index != INDEX_NONE && Records[Index].bAlive;
}

float ACSMatchDirector::GetHealth(int32 PlayerId) const
{
	const int32 Index = FindRecordIndex(PlayerId);
	return Index == INDEX_NONE ? 0.f : Records[Index].Health;
}

float ACSMatchDirector::GetArmor(int32 PlayerId) const
{
	const int32 Index = FindRecordIndex(PlayerId);
	return Index == INDEX_NONE ? 0.f : Records[Index].Armor;
}

int32 ACSMatchDirector::GetStarterRoundsInMag(int32 PlayerId) const
{
	const int32 Index = FindRecordIndex(PlayerId);
	return Index == INDEX_NONE ? 0 : Records[Index].StarterRoundsInMag;
}

// ---------------------------------------------------------------------------
// Authority-only writes
// ---------------------------------------------------------------------------

void ACSMatchDirector::EnsurePlayer(int32 PlayerId)
{
	CS_AUTHORITY_ONLY(this);

	if (PlayerId == 0)
	{
		return;
	}

	const UCSCombatSettings* Settings = UCSCombatSettings::Get();
	const UCSWeaponDefinition* Starter = Settings->StarterWeapon.LoadSynchronous();
	const int32 StartingRounds = Starter ? Starter->MagazineSize : 12;

	if (FCSPlayerCombatRecord* Existing = FindRecordMutable(PlayerId))
	{
		// Already tracked; leave the live state alone.
		return;
	}

	// A departed player must not be resurrected by their pawn lingering for a
	// frame after the leave notification. Photon player numbers are never
	// reused within a room, so refusing them is safe.
	if (HandledDepartures.Contains(PlayerId))
	{
		return;
	}

	if (Records.Num() >= Settings->MaxTrackedPlayers)
	{
		UE_LOG(LogCSAuth, Error,
			TEXT("MatchDirector is full (%d records); cannot track player %d. ")
			TEXT("Raise MaxTrackedPlayers and the FusionArraySize on Records together."),
			Records.Num(), PlayerId);
		return;
	}

	FCSPlayerCombatRecord Record;
	Record.PlayerId = PlayerId;
	Record.Health = Settings->MaxHealth;
	Record.Armor = 0.f;
	Record.bAlive = true;
	Record.StarterRoundsInMag = StartingRounds;
	Record.LastFireNetworkTime = 0.0;
	Record.RespawnCounter = 1;

	// v1.1: team (smaller side, bots included) and starting money.
	const FCSModeRules& Rules = ModeRules(this);
	if (Rules.bTeams)
	{
		Record.Team = static_cast<uint8>(CountMembers(ECSTeam::Alpha) <= CountMembers(ECSTeam::Bravo) ? ECSTeam::Alpha : ECSTeam::Bravo);
	}
	Record.Money = Rules.StartMoney;

	Records.Add(Record);

	// The spawn point is chosen now that the team is known. The owning client
	// moves itself there on first sight of the record (ACSCharacter::SyncWithDirector).
	{
		FCSPlayerCombatRecord& Added = Records.Last();
		Added.RespawnPointIndex = PickSpawnPoint(PlayerId);

		// 5 vs 5: somebody arriving after the buy time sits the round out.
		const ACSGameState* GS = ModeState(this);
		if (Rules.bRounds && GS && GS->GetMatchPhase() == ECSMatchPhase::InProgress && GS->GetBuyTimeRemaining() <= 0.f)
		{
			Added.bAlive = false;
			Added.Health = 0.f;
		}
		else
		{
			BeginProtection(Added);
		}
	}

	// Every player gets a Master-Client-owned inventory, created empty: per
	// the design a player starts with the starter pistol and nothing else.
	if (!ACSPlayerInventory::Find(this, PlayerId))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ACSPlayerInventory* Inventory = GetWorld()->SpawnActor<ACSPlayerInventory>(
				ACSPlayerInventory::StaticClass(), FTransform::Identity, Params))
		{
			Inventory->InitializeFor(PlayerId);
		}
	}

	UE_LOG(LogCSAuth, Log, TEXT("Registered player %d (hp %.0f, %d rounds, team %d, $%d)."),
		PlayerId, Record.Health, Record.StarterRoundsInMag, Record.Team, Record.Money);

	OnRecordsChanged.Broadcast(PlayerId);
}

void ACSMatchDirector::RemovePlayer(int32 PlayerId, ECSDeathReason Reason)
{
	CS_AUTHORITY_ONLY(this);

	// Double-drop guard. Both the Fusion leave notification and the
	// missing-pawn sweep can report the same departure; only the first counts.
	if (HandledDepartures.Contains(PlayerId))
	{
		UE_LOG(LogCSAuth, Log, TEXT("Departure of player %d already handled - ignoring repeat (%s)."),
			PlayerId, *UEnum::GetValueAsString(Reason));
		return;
	}

	CheatGuard.Forget(PlayerId);

	const int32 Index = FindRecordIndex(PlayerId);
	if (Index == INDEX_NONE)
	{
		return;
	}
	HandledDepartures.Add(PlayerId);

	// Loot first, while the inventory still exists. The position is the last
	// one the authority saw: the pawn is PlayerAttached and may already be gone.
	FVector Where = FVector::ZeroVector;
	GetLastKnownLocation(PlayerId, Where);
	const int32 Dropped = DropInventoryAsLoot(PlayerId, Where, Reason);

	UE_LOG(LogCSAuth, Log, TEXT("Player %d left (%s): dropped %d pickups at %s."),
		PlayerId, *UEnum::GetValueAsString(Reason), Dropped, *Where.ToCompactString());

	Records.RemoveAt(Index);
	ProtectionSpawn.Remove(PlayerId);
	ProtectionArrived.Remove(PlayerId);
	LastKnownAlive.Remove(PlayerId);
	LastKnownLocation.Remove(PlayerId);
	HeartbeatSeen.Remove(PlayerId);
	PawnMissingSince.Remove(PlayerId);

	if (ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(this, PlayerId))
	{
		Inventory->Destroy();
	}

	OnRecordsChanged.Broadcast(PlayerId);
}

ACSCharacter* ACSMatchDirector::FindPawnForPlayer(const UObject* WorldContextObject, int32 PlayerId)
{
	if (PlayerId == 0)
	{
		return nullptr;
	}
	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ACSCharacter> It(World); It; ++It)
	{
		if (!It->IsActorBeingDestroyed() && It->GetOwningPlayerId() == PlayerId)
		{
			return *It;
		}
	}
	return nullptr;
}

bool ACSMatchDirector::GetLastKnownLocation(int32 PlayerId, FVector& OutLocation) const
{
	if (const FVector* Found = LastKnownLocation.Find(PlayerId))
	{
		OutLocation = *Found;
		return true;
	}
	if (const ACSCharacter* Pawn = FindPawnForPlayer(this, PlayerId))
	{
		OutLocation = Pawn->GetActorLocation();
		return true;
	}
	return false;
}

int32 ACSMatchDirector::DropInventoryAsLoot(int32 PlayerId, const FVector& Where, ECSDeathReason Reason)
{
	CS_AUTHORITY_ONLY_RET(this, 0);

	ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(this, PlayerId);
	if (!Inventory)
	{
		return 0;
	}

	// TakeAll empties the inventory in the same step. That is what makes a
	// repeated call harmless: the second one finds nothing left to drop.
	const TArray<FCSInventorySlot> Contents = Inventory->TakeAll();
	if (Contents.Num() == 0)
	{
		return 0;
	}

	ACSWorldPickup::MakeRoomForDrops(this, Contents.Num());

	UWorld* World = GetWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CSLootScatter), false);
	// Ignore every pawn so loot lands on the floor, not on a body.
	for (TActorIterator<ACSCharacter> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}

	int32 Spawned = 0;
	for (int32 i = 0; i < Contents.Num(); ++i)
	{
		const FCSInventorySlot& Item = Contents[i];

		// Deterministic ring: golden-angle spacing, growing radius, so items
		// never stack on one spot however many there are.
		const float Angle = i * 2.39996f;
		const float Radius = FMath::Min(55.f + 28.f * i, 220.f);
		const FVector Flat = Where + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);

		// Settle onto whatever is below.
		FVector Target = Flat;
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Flat + FVector(0.f, 0.f, 80.f),
				Flat - FVector(0.f, 0.f, 400.f), ECC_WorldStatic, Params))
		{
			Target = Hit.ImpactPoint + FVector(0.f, 0.f, 25.f);
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ACSWorldPickup* Pickup = World->SpawnActor<ACSWorldPickup>(
				ACSWorldPickup::StaticClass(), Target, FRotator(0.f, FMath::RandRange(0.f, 360.f), 0.f), SpawnParams))
		{
			// Weapons keep the rounds they had; stacks keep their count.
			Pickup->InitializeItem(Item.ItemIndex, Item.Count, Item.AmmoInMag, /*bDropped*/ true);
			Pickup->SetDropOrigin(Where + FVector(0.f, 0.f, 40.f));
			++Spawned;
		}
	}

	UE_LOG(LogCSInventory, Log, TEXT("Player %d inventory -> %d loot pickups (%s)."),
		PlayerId, Spawned, *UEnum::GetValueAsString(Reason));
	return Spawned;
}

FCSLoadoutView ACSMatchDirector::GetLoadout(int32 PlayerId) const
{
	FCSLoadoutView View;

	const int32 Index = FindRecordIndex(PlayerId);
	const FCSPlayerCombatRecord* Record = Index != INDEX_NONE ? &Records[Index] : nullptr;
	if (Record)
	{
		const double Now = UCSAuthority::GetNetworkTimeSeconds(this);
		View.bReloading = Record->ReloadCompleteNetworkTime > 0.0 && Now < Record->ReloadCompleteNetworkTime;
	}

	// An equipped inventory weapon takes precedence ...
	if (const ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(this, PlayerId))
	{
		const int32 Slot = Inventory->GetEquippedSlot();
		// v1.1: a grenade in hand. Its weapon asset only drives the model and hands.
		if (const UCSItemDefinition* Held = Inventory->GetItemInSlot(Slot); Held && Held->ItemType == ECSItemType::Grenade)
		{
			FCSInventorySlot SlotData;
			Inventory->GetSlot(Slot, SlotData);
			View.bGrenade = true;
			View.Slot = Slot;
			View.Weapon = Held->Weapon.LoadSynchronous();
			View.RoundsInMag = SlotData.Count;
			View.Reserve = 0;
			View.bReloading = false;
			return View;
		}
		if (const UCSWeaponDefinition* Weapon = Inventory->GetEquippedWeapon())
		{
			FCSInventorySlot SlotData;
			Inventory->GetSlot(Slot, SlotData);

			View.Weapon = Weapon;
			View.Slot = Slot;
			View.RoundsInMag = SlotData.AmmoInMag;

			const UCSItemSettings* ItemSettings = UCSItemSettings::Get();
			const UCSItemDefinition* Item = ItemSettings->GetItem(SlotData.ItemIndex);
			const int32 AmmoIndex = Item ? ItemSettings->FindItemIndex(Item->AmmoItemId) : INDEX_NONE;
			View.Reserve = AmmoIndex != INDEX_NONE ? Inventory->CountItem(AmmoIndex) : 0;
			return View;
		}
	}

	// ... otherwise the starter pistol, which is always there.
	View.Weapon = UCSCombatSettings::Get()->StarterWeapon.LoadSynchronous();
	View.Slot = INDEX_NONE;
	View.RoundsInMag = Record ? Record->StarterRoundsInMag : 0;
	View.Reserve = -1;
	return View;
}

ECSFireRejection ACSMatchDirector::ValidateFire(int32 PlayerId, const FCSLoadoutView& Loadout,
	const FVector& ClaimedOrigin, const FVector& ClaimedDirection,
	const FVector& AuthoritativePawnLocation) const
{
	const UCSWeaponDefinition* Weapon = Loadout.Weapon;
	if (!Weapon)
	{
		return ECSFireRejection::NoWeapon;
	}
	// Grenades are thrown (TryThrowGrenade), never fired.
	if (Loadout.bGrenade)
	{
		return ECSFireRejection::NoWeapon;
	}

	const int32 Index = FindRecordIndex(PlayerId);
	if (Index == INDEX_NONE)
	{
		return ECSFireRejection::NoRecord;
	}

	const FCSPlayerCombatRecord& Record = Records[Index];

	if (!Record.bAlive)
	{
		return ECSFireRejection::ShooterDead;
	}

	// The scoreboard is final once the round is over: no shooting until the
	// next warmup.
	const ACSGameState* GS = GetWorld() ? GetWorld()->GetGameState<ACSGameState>() : nullptr;
	if (GS && GS->GetMatchPhase() == ECSMatchPhase::PostMatch)
	{
		return ECSFireRejection::MatchOver;
	}

	// A malformed direction would let a client aim with a zero or denormalised
	// vector and confuse the trace.
	if (!ClaimedDirection.IsNormalized() && !ClaimedDirection.GetSafeNormal().IsNormalized())
	{
		return ECSFireRejection::BadDirection;
	}

	const UCSCombatSettings* Settings = UCSCombatSettings::Get();
	const double Now = UCSAuthority::GetNetworkTimeSeconds(this);

	if (Record.ReloadCompleteNetworkTime > 0.0 && Now < Record.ReloadCompleteNetworkTime)
	{
		return ECSFireRejection::Reloading;
	}

	// The magazine comes from the loadout the AUTHORITY resolved - the starter
	// record or the inventory slot - never from anything the client claims.
	if (Loadout.RoundsInMag <= 0)
	{
		return ECSFireRejection::OutOfAmmo;
	}

	// Fire-rate check against Fusion's room clock, with a tolerance for
	// ordinary jitter. Anything faster is a rate hack.
	const double MinInterval = Weapon->GetFireInterval() * (1.0 - Settings->FireRateTolerance);
	if (Record.LastFireNetworkTime > 0.0 && (Now - Record.LastFireNetworkTime) < MinInterval)
	{
		return ECSFireRejection::FireRate;
	}

	// The muzzle position the client reports must be near where the authority
	// believes that pawn is. This stops teleport-shooting; it is intentionally
	// loose because it also has to absorb latency and interpolation.
	if (FVector::DistSquared(ClaimedOrigin, AuthoritativePawnLocation) >
		FMath::Square(Settings->MaxFireOriginDeviation))
	{
		return ECSFireRejection::OriginTooFar;
	}

	return ECSFireRejection::Accepted;
}

void ACSMatchDirector::CommitFire(int32 PlayerId)
{
	CS_AUTHORITY_ONLY(this);

	FCSPlayerCombatRecord* Record = FindRecordMutable(PlayerId);
	if (!Record)
	{
		return;
	}

	const FCSLoadoutView Loadout = GetLoadout(PlayerId);
	if (Loadout.IsStarter())
	{
		Record->StarterRoundsInMag = FMath::Max(0, Record->StarterRoundsInMag - 1);
	}
	else if (ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(this, PlayerId))
	{
		Inventory->SetSlotAmmo(Loadout.Slot, Loadout.RoundsInMag - 1);
	}

	Record->LastFireNetworkTime = UCSAuthority::GetNetworkTimeSeconds(this);
	OnRecordsChanged.Broadcast(PlayerId);

	// Shooting from under spawn protection ends it.
	CancelProtection(PlayerId, TEXT("fired"));
}

bool ACSMatchDirector::BeginReload(int32 PlayerId)
{
	CS_AUTHORITY_ONLY_RET(this, false);

	FCSPlayerCombatRecord* Record = FindRecordMutable(PlayerId);
	if (!Record || !Record->bAlive)
	{
		return false;
	}

	const FCSLoadoutView Loadout = GetLoadout(PlayerId);
	if (!Loadout.Weapon || Loadout.bReloading)
	{
		return false;
	}
	if (Loadout.RoundsInMag >= Loadout.Weapon->MagazineSize)
	{
		return false; // already full
	}
	if (Loadout.Reserve == 0)
	{
		return false; // nothing to reload from
	}

	Record->ReloadCompleteNetworkTime = UCSAuthority::GetNetworkTimeSeconds(this) + Loadout.Weapon->ReloadSeconds;
	Record->ReloadSlot = Loadout.Slot;
	OnRecordsChanged.Broadcast(PlayerId);
	return true;
}

void ACSMatchDirector::CancelReload(int32 PlayerId)
{
	CS_AUTHORITY_ONLY(this);

	if (FCSPlayerCombatRecord* Record = FindRecordMutable(PlayerId))
	{
		if (Record->ReloadCompleteNetworkTime > 0.0)
		{
			Record->ReloadCompleteNetworkTime = 0.0;
			Record->ReloadSlot = INDEX_NONE;
			OnRecordsChanged.Broadcast(PlayerId);
		}
	}
}

void ACSMatchDirector::CompleteReload(FCSPlayerCombatRecord& Record)
{
	const int32 Slot = Record.ReloadSlot;
	Record.ReloadCompleteNetworkTime = 0.0;
	Record.ReloadSlot = INDEX_NONE;

	if (Slot == INDEX_NONE)
	{
		const UCSWeaponDefinition* Starter = UCSCombatSettings::Get()->StarterWeapon.LoadSynchronous();
		Record.StarterRoundsInMag = Starter ? Starter->MagazineSize : 12;
		return;
	}

	// Inventory weapon: move rounds from the matching ammo stack into the
	// magazine. Re-resolve everything - the weapon may have been dropped or
	// swapped while the reload ran.
	ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(this, Record.PlayerId);
	const UCSItemDefinition* Item = Inventory ? Inventory->GetItemInSlot(Slot) : nullptr;
	const UCSWeaponDefinition* Weapon = (Item && Item->IsWeapon()) ? Item->Weapon.LoadSynchronous() : nullptr;
	if (!Weapon)
	{
		return;
	}

	FCSInventorySlot SlotData;
	Inventory->GetSlot(Slot, SlotData);

	const int32 AmmoIndex = UCSItemSettings::Get()->FindItemIndex(Item->AmmoItemId);
	const int32 Needed = Weapon->MagazineSize - SlotData.AmmoInMag;
	const int32 Taken = (AmmoIndex != INDEX_NONE && Needed > 0) ? Inventory->ConsumeItem(AmmoIndex, Needed) : 0;

	// ConsumeItem can compact stacks; the weapon slot itself is untouched.
	Inventory->SetSlotAmmo(Slot, SlotData.AmmoInMag + Taken);
}

float ACSMatchDirector::Heal(int32 PlayerId, float Amount)
{
	CS_AUTHORITY_ONLY_RET(this, 0.f);

	FCSPlayerCombatRecord* Record = FindRecordMutable(PlayerId);
	if (!Record || !Record->bAlive || Amount <= 0.f)
	{
		return 0.f;
	}

	const float Before = Record->Health;
	Record->Health = FMath::Min(UCSCombatSettings::Get()->MaxHealth, Record->Health + Amount);
	OnRecordsChanged.Broadcast(PlayerId);
	return Record->Health - Before;
}

float ACSMatchDirector::AddArmor(int32 PlayerId, float Amount)
{
	CS_AUTHORITY_ONLY_RET(this, 0.f);

	FCSPlayerCombatRecord* Record = FindRecordMutable(PlayerId);
	if (!Record || !Record->bAlive || Amount <= 0.f)
	{
		return 0.f;
	}

	const float Before = Record->Armor;
	Record->Armor = FMath::Min(UCSCombatSettings::Get()->MaxArmor, Record->Armor + Amount);
	OnRecordsChanged.Broadcast(PlayerId);
	return Record->Armor - Before;
}

float ACSMatchDirector::ApplyDamage(int32 VictimId, int32 InstigatorId, float Damage, ECSHitZone Zone)
{
	CS_AUTHORITY_ONLY_RET(this, 0.f);

	if (Damage <= 0.f)
	{
		return 0.f;
	}

	FCSPlayerCombatRecord* Victim = FindRecordMutable(VictimId);
	if (!Victim || !Victim->bAlive)
	{
		return 0.f;
	}

	const UCSCombatSettings* Settings = UCSCombatSettings::Get();
	const FCSModeRules& Rules = ModeRules(this);
	const double Now = UCSAuthority::GetNetworkTimeSeconds(this);

	// v1.1: spawn protection, and no friendly fire in team modes.
	if (Victim->ProtectedUntil > Now)
	{
		return 0.f;
	}
	if (InstigatorId != VictimId && AreTeammates(VictimId, InstigatorId))
	{
		return 0.f;
	}

	// Armor eats a share of the incoming damage while it lasts.
	float ToHealth = Damage;
	if (Victim->Armor > 0.f)
	{
		const float ToArmor = FMath::Min(Victim->Armor, Damage * Settings->ArmorAbsorptionRatio);
		Victim->Armor = FMath::Max(0.f, Victim->Armor - ToArmor);
		ToHealth = Damage - ToArmor;
	}

	const float Before = Victim->Health;
	Victim->Health = FMath::Max(0.f, Victim->Health - ToHealth);
	const float Applied = Before - Victim->Health;
	if (InstigatorId != VictimId)
	{
		if (FCSPlayerCombatRecord* Dealer = FindRecordMutable(InstigatorId))
		{
			Dealer->DamageDealt += FMath::RoundToInt(Applied);
		}
	}
	const bool bKilledNow = Victim->Health <= 0.f;

	UE_LOG(LogCSCombat, Verbose,
		TEXT("Player %d hit by %d for %.1f (%s), health %.0f -> %.0f"),
		VictimId, InstigatorId, Applied, *UEnum::GetValueAsString(Zone), Before, Victim->Health);

	if (Victim->Health <= 0.f)
	{
		Victim->bAlive = false;
		Victim->Deaths += 1;
		// 5 vs 5 has no respawn: the round decides when everyone comes back.
		Victim->RespawnAtNetworkTime = Rules.bRespawn ? Now + Rules.RespawnDelay : 0.0;
		Victim->ProtectedUntil = 0.0;

		if (InstigatorId != VictimId)
		{
			if (FCSPlayerCombatRecord* Killer = FindRecordMutable(InstigatorId))
			{
				Killer->Kills += 1;
				Killer->Headshots += (Zone == ECSHitZone::Head) ? 1 : 0;
				const int32 Reward = Rules.KillReward + (Zone == ECSHitZone::Head ? Rules.HeadshotBonus : 0);
				Killer->Money = FMath::Clamp(Killer->Money + Reward, 0, Rules.MaxMoney);

				// Team Deathmatch: every kill scores for the team.
				ACSGameState* GS = ModeState(this);
				if (GS && Rules.bTeams && !Rules.bRounds && GS->GetMatchPhase() == ECSMatchPhase::InProgress)
				{
					GS->AddTeamScore(Killer->GetTeam(), 1);
				}
				OnRecordsChanged.Broadcast(InstigatorId);
			}
		}

		UE_LOG(LogCSCombat, Log, TEXT("Player %d killed by %d (%s)."),
			VictimId, InstigatorId, *UEnum::GetValueAsString(Zone));

		// Everything in the inventory falls where they died. The starter
		// pistol stays with the player - it is not in the inventory.
		FVector DeathSpot = FVector::ZeroVector;
		if (const ACSCharacter* VictimPawn = FindPawnForPlayer(this, VictimId))
		{
			DeathSpot = VictimPawn->GetActorLocation();
		}
		else
		{
			GetLastKnownLocation(VictimId, DeathSpot);
		}
		DropInventoryAsLoot(VictimId, DeathSpot,
			InstigatorId == VictimId ? ECSDeathReason::Suicide : ECSDeathReason::Killed);

		OnPlayerKilled.Broadcast(VictimId, InstigatorId, Zone);
	}

	QueueCombatEvent(VictimId, InstigatorId, Applied, bKilledNow, Zone);

	OnRecordsChanged.Broadcast(VictimId);
	return Applied;
}

void ACSMatchDirector::RespawnPlayer(int32 PlayerId, int32 SpawnPointIndex)
{
	CS_AUTHORITY_ONLY(this);

	FCSPlayerCombatRecord* Record = FindRecordMutable(PlayerId);
	if (!Record || Record->bAlive)
	{
		return;
	}

	ResetLife(*Record, SpawnPointIndex);
	Record->Armor = 0.f;

	UE_LOG(LogCSCombat, Log, TEXT("Player %d respawned at point %d (counter %d)."),
		PlayerId, SpawnPointIndex, Record->RespawnCounter);

	OnRecordsChanged.Broadcast(PlayerId);
}

void ACSMatchDirector::ResetLife(FCSPlayerCombatRecord& Record, int32 SpawnPointIndex)
{
	const UCSCombatSettings* Settings = UCSCombatSettings::Get();
	const UCSWeaponDefinition* Starter = Settings->StarterWeapon.LoadSynchronous();

	Record.Health = Settings->MaxHealth;
	Record.bAlive = true;
	// The starter pistol is restored in full on every respawn, by design: it is
	// not an inventory item and can never be lost.
	Record.StarterRoundsInMag = Starter ? Starter->MagazineSize : 12;
	Record.LastFireNetworkTime = 0.0;
	Record.ReloadCompleteNetworkTime = 0.0;
	Record.ReloadSlot = INDEX_NONE;
	Record.RespawnAtNetworkTime = 0.0;
	Record.RespawnPointIndex = SpawnPointIndex;
	Record.RespawnCounter += 1;

	BeginProtection(Record);
}

// ---------------------------------------------------------------------------
// v1.1 game modes: teams, money, spawn protection, shop, rounds
// ---------------------------------------------------------------------------

ECSTeam ACSMatchDirector::GetTeam(int32 PlayerId) const
{
	const int32 Index = FindRecordIndex(PlayerId);
	return Index == INDEX_NONE ? ECSTeam::None : Records[Index].GetTeam();
}

int32 ACSMatchDirector::GetMoney(int32 PlayerId) const
{
	const int32 Index = FindRecordIndex(PlayerId);
	return Index == INDEX_NONE ? 0 : Records[Index].Money;
}

bool ACSMatchDirector::IsProtected(int32 PlayerId) const
{
	return GetProtectionRemaining(PlayerId) > 0.f;
}

float ACSMatchDirector::GetProtectionRemaining(int32 PlayerId) const
{
	const int32 Index = FindRecordIndex(PlayerId);
	if (Index == INDEX_NONE || !Records[Index].bAlive || Records[Index].ProtectedUntil <= 0.0)
	{
		return 0.f;
	}
	return static_cast<float>(FMath::Max(0.0, Records[Index].ProtectedUntil - UCSAuthority::GetNetworkTimeSeconds(this)));
}

bool ACSMatchDirector::CanBuy(int32 PlayerId) const
{
	return GetBuyTimeRemaining(PlayerId) > 0.f;
}

float ACSMatchDirector::GetBuyTimeRemaining(int32 PlayerId) const
{
	if (!IsPlayerAlive(PlayerId))
	{
		return 0.f;
	}
	const ACSGameState* GS = ModeState(this);
	if (GS && GS->GetMatchPhase() == ECSMatchPhase::PostMatch)
	{
		return 0.f;
	}
	// 5 vs 5: the first seconds of every round. Deathmatch modes: exactly as
	// long as the spawn protection lasts.
	if (ModeRules(this).bRounds)
	{
		return GS ? GS->GetBuyTimeRemaining() : 0.f;
	}
	return GetProtectionRemaining(PlayerId);
}

bool ACSMatchDirector::AreTeammates(int32 A, int32 B) const
{
	if (A == B || !ModeRules(this).bTeams)
	{
		return false;
	}
	const ECSTeam TeamA = GetTeam(A);
	return TeamA != ECSTeam::None && TeamA == GetTeam(B);
}

int32 ACSMatchDirector::CountAlive(ECSTeam Team) const
{
	int32 Count = 0;
	for (const FCSPlayerCombatRecord& Record : Records)
	{
		Count += (Record.GetTeam() == Team && Record.bAlive) ? 1 : 0;
	}
	return Count;
}

int32 ACSMatchDirector::CountMembers(ECSTeam Team) const
{
	int32 Count = 0;
	for (const FCSPlayerCombatRecord& Record : Records)
	{
		Count += Record.GetTeam() == Team ? 1 : 0;
	}
	return Count;
}

void ACSMatchDirector::AddMoney(int32 PlayerId, int32 Delta)
{
	CS_AUTHORITY_ONLY(this);
	if (FCSPlayerCombatRecord* Record = FindRecordMutable(PlayerId))
	{
		Record->Money = FMath::Clamp(Record->Money + Delta, 0, ModeRules(this).MaxMoney);
		OnRecordsChanged.Broadcast(PlayerId);
	}
}

void ACSMatchDirector::BeginProtection(FCSPlayerCombatRecord& Record)
{
	ProtectionArrived.Remove(Record.PlayerId);

	// -nospawnprotection: the v1.0 regression suites shoot players the moment they appear.
	static const bool bDisabled = FParse::Param(FCommandLine::Get(), TEXT("nospawnprotection"));
	const float Seconds = bDisabled ? 0.f : ModeRules(this).ProtectionSeconds;
	if (Seconds <= 0.f)
	{
		Record.ProtectedUntil = 0.0;
		ProtectionSpawn.Remove(Record.PlayerId);
		return;
	}

	Record.ProtectedUntil = UCSAuthority::GetNetworkTimeSeconds(this) + Seconds;

	// Where the pawn is going to appear. Movement is measured from there once
	// it has arrived: the owning client teleports itself, a moment later.
	FVector Spawn = FVector::ZeroVector;
	const ACSGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACSGameMode>() : nullptr;
	if (const AActor* Start = GameMode ? GameMode->GetPlayerStartByIndex(Record.RespawnPointIndex) : nullptr)
	{
		Spawn = Start->GetActorLocation();
	}
	ProtectionSpawn.Add(Record.PlayerId, Spawn);
}

void ACSMatchDirector::WatchProtection(FCSPlayerCombatRecord& Record, const ACSCharacter* Pawn)
{
	if (Record.ProtectedUntil <= 0.0)
	{
		return;
	}

	const int32 PlayerId = Record.PlayerId;
	if (!Record.bAlive || UCSAuthority::GetNetworkTimeSeconds(this) >= Record.ProtectedUntil)
	{
		CancelProtection(PlayerId, TEXT("expired"));
		return;
	}
	if (!Pawn)
	{
		return;
	}

	const FVector Location = Pawn->GetActorLocation();
	FVector& Anchor = ProtectionSpawn.FindOrAdd(PlayerId, Location);
	if (!ProtectionArrived.Contains(PlayerId))
	{
		// Still where it died, or in the middle of the teleport.
		if (FVector::Dist2D(Location, Anchor) < 150.f)
		{
			ProtectionArrived.Add(PlayerId);
			Anchor = Location;
		}
		return;
	}

	// Walking, jumping or crouching ends protection - and with it the shop.
	const FVector AnchorCopy = Anchor;
	if (FVector::Dist2D(Location, AnchorCopy) > 60.f)
	{
		CancelProtection(PlayerId, TEXT("moved"));
	}
	else if (Location.Z > AnchorCopy.Z + 20.f)
	{
		CancelProtection(PlayerId, TEXT("jumped"));
	}
	else if (Pawn->GetStance() == ECSStanceState::Crouching)
	{
		CancelProtection(PlayerId, TEXT("crouched"));
	}
}

void ACSMatchDirector::CancelProtection(int32 PlayerId, const TCHAR* Why)
{
	CS_AUTHORITY_ONLY(this);

	FCSPlayerCombatRecord* Record = FindRecordMutable(PlayerId);
	ProtectionSpawn.Remove(PlayerId);
	ProtectionArrived.Remove(PlayerId);
	if (!Record || Record->ProtectedUntil <= 0.0)
	{
		return;
	}
	Record->ProtectedUntil = 0.0;
	UE_LOG(LogCSCombat, Log, TEXT("Player %d spawn protection ended (%s)."), PlayerId, Why);
	OnRecordsChanged.Broadcast(PlayerId);
}

int32 ACSMatchDirector::PickSpawnPoint(int32 PlayerId) const
{
	const ACSGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACSGameMode>() : nullptr;
	if (!GameMode || GameMode->GetNumPlayerStarts() == 0)
	{
		return FMath::Abs(PlayerId);
	}

	const FCSModeRules& Rules = ModeRules(this);
	const ECSTeam Team = Rules.bTeams ? GetTeam(PlayerId) : ECSTeam::None;

	TArray<int32> Candidates;
	GameMode->GetSpawnIndicesForTeam(Team, Candidates);
	if (Candidates.Num() == 0)
	{
		return FMath::Abs(PlayerId);
	}

	// Rounds: a fixed pad per team member, so a whole team spawns side by side
	// without two players on one start.
	if (Rules.bRounds)
	{
		int32 Rank = 0;
		for (const FCSPlayerCombatRecord& Other : Records)
		{
			Rank += (Other.PlayerId != PlayerId && Other.GetTeam() == Team && Other.PlayerId < PlayerId) ? 1 : 0;
		}
		return Candidates[Rank % Candidates.Num()];
	}

	// Respawn modes: as far from living enemies as possible, never onto
	// somebody. A little randomness among the best few keeps it unpredictable.
	TArray<TPair<float, int32>> Scored;
	for (const int32 Index : Candidates)
	{
		const AActor* Start = GameMode->GetPlayerStartByIndex(Index);
		if (!Start)
		{
			continue;
		}
		const FVector Pad = Start->GetActorLocation();
		float NearestEnemy = 1.0e7f;
		bool bOccupied = false;
		for (const FCSPlayerCombatRecord& Other : Records)
		{
			FVector Where;
			if (Other.PlayerId == PlayerId || !Other.bAlive || !GetLastKnownLocation(Other.PlayerId, Where))
			{
				continue;
			}
			const float Distance = FVector::Dist(Pad, Where);
			bOccupied |= Distance < 120.f;
			if (!AreTeammates(PlayerId, Other.PlayerId))
			{
				NearestEnemy = FMath::Min(NearestEnemy, Distance);
			}
		}
		Scored.Add(TPair<float, int32>(bOccupied ? -1.f : NearestEnemy, Index));
	}
	if (Scored.Num() == 0)
	{
		return Candidates[0];
	}
	Scored.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key > B.Key; });
	const int32 Pool = FMath::Min(3, Scored.Num());
	return Scored[FMath::RandRange(0, Pool - 1)].Value;
}

ECSBuyResult ACSMatchDirector::TryBuy(int32 PlayerId, int32 ShopIndex)
{
	CS_AUTHORITY_ONLY_RET(this, ECSBuyResult::Invalid);

	const FCSShopEntry* Entry = UCSShopSettings::Get()->GetEntry(ShopIndex);
	const UCSItemSettings* ItemSettings = UCSItemSettings::Get();
	const int32 ItemIndex = Entry ? ItemSettings->FindItemIndex(Entry->ItemId) : INDEX_NONE;
	const UCSItemDefinition* Item = ItemSettings->GetItem(ItemIndex);
	FCSPlayerCombatRecord* Record = FindRecordMutable(PlayerId);
	ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(this, PlayerId);
	if (!Entry || !Item || !Record || !Inventory)
	{
		return ECSBuyResult::Invalid;
	}
	if (!Record->bAlive)
	{
		return ECSBuyResult::Dead;
	}
	if (!CanBuy(PlayerId))
	{
		return ECSBuyResult::ShopClosed;
	}
	if (Record->Money < Entry->Price)
	{
		return ECSBuyResult::NotEnoughMoney;
	}

	switch (Item->ItemType)
	{
	case ECSItemType::Weapon:
	{
		const UCSWeaponDefinition* Weapon = Item->Weapon.LoadSynchronous();
		if (!Weapon)
		{
			return ECSBuyResult::Invalid;
		}
		if (Inventory->CountItem(ItemIndex) > 0)
		{
			return ECSBuyResult::AlreadyOwned;
		}
		if (!Inventory->CanAccept(ItemIndex, 1) || Inventory->AddItem(ItemIndex, 1, Weapon->MagazineSize) > 0)
		{
			return ECSBuyResult::InventoryFull;
		}
		// Spare magazines, as far as they fit.
		const int32 AmmoIndex = ItemSettings->FindItemIndex(Item->AmmoItemId);
		if (AmmoIndex != INDEX_NONE && Entry->Bundle > 0)
		{
			Inventory->AddItem(AmmoIndex, Entry->Bundle * Weapon->MagazineSize, 0);
		}
		// Straight into the hands.
		const TArray<FCSInventorySlot>& Slots = Inventory->GetSlots();
		for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
		{
			if (Slots[Slot].ItemIndex == ItemIndex && !Slots[Slot].IsEmpty())
			{
				CancelReload(PlayerId);
				Inventory->SetEquippedSlot(Slot);
				break;
			}
		}
		break;
	}

	case ECSItemType::Armor:
		// Worn at once rather than carried.
		if (AddArmor(PlayerId, Item->ArmorAmount) <= 0.f)
		{
			return ECSBuyResult::AlreadyOwned;
		}
		break;

	default:
	{
		// Medkits, ammo: into the inventory.
		const int32 Count = FMath::Max(1, Entry->Bundle) * FMath::Max(1, Item->DefaultPickupCount);
		if (!Inventory->CanAccept(ItemIndex, Count))
		{
			return ECSBuyResult::InventoryFull;
		}
		Inventory->AddItem(ItemIndex, Count, 0);
		break;
	}
	}

	// Re-find: AddArmor and friends do not reallocate, but stay safe.
	if (FCSPlayerCombatRecord* Buyer = FindRecordMutable(PlayerId))
	{
		Buyer->Money -= Entry->Price;
		UE_LOG(LogCSInventory, Log, TEXT("Player %d bought %s for $%d ($%d left)."),
			PlayerId, *Entry->ItemId.ToString(), Entry->Price, Buyer->Money);
	}
	OnRecordsChanged.Broadcast(PlayerId);
	return ECSBuyResult::Ok;
}

void ACSMatchDirector::StartNewRound()
{
	CS_AUTHORITY_ONLY(this);

	// Survivors keep their weapons and armor, the fallen come back with the
	// starter pistol; everybody returns to their team's spawn.
	for (FCSPlayerCombatRecord& Record : Records)
	{
		const float KeptArmor = Record.bAlive ? Record.Armor : 0.f;
		ResetLife(Record, PickSpawnPoint(Record.PlayerId));
		Record.Armor = KeptArmor;
		OnRecordsChanged.Broadcast(Record.PlayerId);
	}
	UE_LOG(LogCSCombat, Log, TEXT("New round: %d players back at their spawns."), Records.Num());
}

void ACSMatchDirector::ResetForNewMatch()
{
	CS_AUTHORITY_ONLY(this);

	const FCSModeRules& Rules = ModeRules(this);

	// Teams: anyone registered before the mode was known gets one now, and a
	// lopsided split (players left during the last match) is evened out.
	for (FCSPlayerCombatRecord& Record : Records)
	{
		if (!Rules.bTeams)
		{
			Record.Team = 0;
		}
		else if (Record.GetTeam() == ECSTeam::None)
		{
			Record.Team = static_cast<uint8>(CountMembers(ECSTeam::Alpha) <= CountMembers(ECSTeam::Bravo) ? ECSTeam::Alpha : ECSTeam::Bravo);
		}
	}
	if (Rules.bTeams)
	{
		// Move bots first, from the back, so humans keep their side.
		for (int32 i = Records.Num() - 1; i >= 0; --i)
		{
			const int32 Alpha = CountMembers(ECSTeam::Alpha);
			const int32 Bravo = CountMembers(ECSTeam::Bravo);
			if (FMath::Abs(Alpha - Bravo) <= 1)
			{
				break;
			}
			const ECSTeam Bigger = Alpha > Bravo ? ECSTeam::Alpha : ECSTeam::Bravo;
			if (CSBots::IsBotId(Records[i].PlayerId) && Records[i].GetTeam() == Bigger)
			{
				Records[i].Team = static_cast<uint8>(Bigger == ECSTeam::Alpha ? ECSTeam::Bravo : ECSTeam::Alpha);
			}
		}
	}

	for (FCSPlayerCombatRecord& Record : Records)
	{
		Record.Kills = 0;
		Record.Deaths = 0;
		Record.Headshots = 0;
		Record.DamageDealt = 0;
		Record.Money = Rules.StartMoney;
		Record.Armor = 0.f;
		if (ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(this, Record.PlayerId))
		{
			Inventory->TakeAll();
		}
		ResetLife(Record, PickSpawnPoint(Record.PlayerId));
		OnRecordsChanged.Broadcast(Record.PlayerId);
	}
	UE_LOG(LogCSCombat, Log, TEXT("Match reset: scores, money and inventories back to the start."));
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void ACSMatchDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickAuthority();
	FlushCombatEvents();
}

void ACSMatchDirector::TickAuthority()
{
	CS_AUTHORITY_ONLY(this);

	const double Now = UCSAuthority::GetNetworkTimeSeconds(this);
	const bool bInSession = UCSAuthority::IsSessionActive(this);

	// Track positions and notice vanished pawns. Collected first and removed
	// after the loop, because RemovePlayer edits Records.
	TArray<int32> Vanished;
	for (const FCSPlayerCombatRecord& Record : Records)
	{
		if (CSBots::IsBotId(Record.PlayerId))
		{
			// Bots are not room members and have no client to lose; the bot
			// manager owns their lifetime. Only their position is tracked.
			if (const ACSCharacter* BotPawn = FindPawnForPlayer(this, Record.PlayerId))
			{
				LastKnownLocation.Add(Record.PlayerId, BotPawn->GetActorLocation());
			}
			continue;
		}

		if (const ACSCharacter* Pawn = FindPawnForPlayer(this, Record.PlayerId))
		{
			LastKnownLocation.Add(Record.PlayerId, Pawn->GetActorLocation());
			PawnMissingSince.Remove(Record.PlayerId);

			// Movement sanity: the owning client simulates this pawn, so its
			// position is only a claim. See FCSCheatGuard.
			if (Record.bAlive)
			{
				const UCharacterMovementComponent* Move = Pawn->GetCharacterMovement();
				CheatGuard.ObservePosition(Record.PlayerId, Pawn->GetActorLocation(), Now,
					Move && Move->IsFalling(), Record.RespawnCounter);
			}

			// Unexpected-disconnect detector that does not wait on the Photon
			// server's own timeout: the client bumps a counter every second on
			// its (player-owned, hence replicated) pawn. Frozen for too long
			// means the process or its connection is gone.
			TPair<int32, double>& Seen = HeartbeatSeen.FindOrAdd(Record.PlayerId, TPair<int32, double>(Pawn->GetHeartbeat(), Now));
			if (Seen.Key != Pawn->GetHeartbeat())
			{
				Seen = TPair<int32, double>(Pawn->GetHeartbeat(), Now);
			}
			else if (bInSession && Now - Seen.Value > HeartbeatTimeoutSeconds)
			{
				UE_LOG(LogCSAuth, Log, TEXT("Player %d: no heartbeat for %.1fs - treating as disconnected."),
					Record.PlayerId, Now - Seen.Value);
				Vanished.Add(Record.PlayerId);
			}
		}
		else if (bInSession)
		{
			// Safety net for the leave notification. If the master migrates
			// while a player is leaving, the new master can receive the notice
			// before it has become the authority and skip it. A pawn that stays
			// gone for this long means the player is gone. RemovePlayer is
			// idempotent, so the normal path firing too is harmless.
			const double& Since = PawnMissingSince.FindOrAdd(Record.PlayerId, Now);
			if (Now - Since > 8.0)
			{
				Vanished.Add(Record.PlayerId);
			}
		}
	}
	// Primary departure detector: the room membership the Photon server itself
	// reports. A tracked player that the server has marked inactive (lost
	// connection) or no longer lists at all is gone - however their pawn and
	// PlayerState happen to be doing on this peer.
	TArray<int32> Active;
	TArray<int32> Inactive;
	if (bInSession && UCSAuthority::GetRoomPlayers(this, Active, Inactive))
	{
		for (const FCSPlayerCombatRecord& Record : Records)
		{
			if (!CSBots::IsBotId(Record.PlayerId) && !Active.Contains(Record.PlayerId) && !Vanished.Contains(Record.PlayerId))
			{
				UE_LOG(LogCSAuth, Log, TEXT("Room membership: player %d is %s."),
					Record.PlayerId, Inactive.Contains(Record.PlayerId) ? TEXT("INACTIVE (connection lost)") : TEXT("no longer in the room"));
				Vanished.Add(Record.PlayerId);
			}
		}
	}

	for (const int32 PlayerId : Vanished)
	{
		RemovePlayer(PlayerId, ECSDeathReason::Disconnected);
	}

	const bool bRespawnMode = ModeRules(this).bRespawn;
	for (int32 i = 0; i < Records.Num(); ++i)
	{
		FCSPlayerCombatRecord& Record = Records[i];

		// Complete reloads.
		if (Record.ReloadCompleteNetworkTime > 0.0 && Now >= Record.ReloadCompleteNetworkTime)
		{
			CompleteReload(Record);
			OnRecordsChanged.Broadcast(Record.PlayerId);
		}

		WatchProtection(Record, FindPawnForPlayer(this, Record.PlayerId));

		// Respawn is driven here rather than by the dead client, so refusing to
		// die gains a cheater nothing. 5 vs 5 has none: rounds revive everyone.
		if (bRespawnMode && !Record.bAlive && Record.RespawnAtNetworkTime > 0.0 && Now >= Record.RespawnAtNetworkTime)
		{
			RespawnPlayer(Record.PlayerId, PickSpawnPoint(Record.PlayerId));
		}
	}
}

void ACSMatchDirector::OnRep_Records()
{
	// Runs on non-authority peers. Raise local death events so every client can
	// react to a kill it did not compute, and so remote pawns are shown dead
	// regardless of what the dying client's own process chooses to do.
	for (const FCSPlayerCombatRecord& Record : Records)
	{
		const bool* Previous = LastKnownAlive.Find(Record.PlayerId);
		if (Previous && *Previous && !Record.bAlive)
		{
			OnPlayerKilled.Broadcast(Record.PlayerId, 0, ECSHitZone::None);
		}
		LastKnownAlive.Add(Record.PlayerId, Record.bAlive);

		OnRecordsChanged.Broadcast(Record.PlayerId);
	}
}

// ---------------------------------------------------------------------------
// Cosmetic combat events (kill feed, hit marker, damage direction)
// ---------------------------------------------------------------------------

void ACSMatchDirector::QueueCombatEvent(int32 VictimId, int32 InstigatorId, float Damage, bool bKilled, ECSHitZone Zone)
{
	// Merge pellets of one blast: same victim and shooter within this frame.
	for (FCSCombatEvent& Pending : PendingCombatEvents)
	{
		if (Pending.VictimId == VictimId && Pending.InstigatorId == InstigatorId)
		{
			Pending.Damage += Damage;
			Pending.bKilled |= bKilled;
			if (Zone == ECSHitZone::Head)
			{
				Pending.Zone = Zone;
			}
			return;
		}
	}

	FCSCombatEvent& Event = PendingCombatEvents.AddDefaulted_GetRef();
	Event.VictimId = VictimId;
	Event.InstigatorId = InstigatorId;
	Event.Damage = Damage;
	Event.bKilled = bKilled;
	Event.Zone = Zone;

	if (!CombatWeaponOverride.IsEmpty())
	{
		Event.WeaponName = CombatWeaponOverride;
	}
	else
	{
		const FCSLoadoutView Loadout = GetLoadout(InstigatorId);
		Event.WeaponName = Loadout.Weapon ? Loadout.Weapon->DisplayName.ToString() : FString();
	}

	if (const ACSCharacter* Shooter = FindPawnForPlayer(this, InstigatorId))
	{
		Event.FromLocation = Shooter->GetActorLocation();
	}
	else
	{
		GetLastKnownLocation(InstigatorId, Event.FromLocation);
	}
}

void ACSMatchDirector::FlushCombatEvents()
{
	if (PendingCombatEvents.Num() == 0)
	{
		return;
	}

	TArray<FCSCombatEvent> Events = MoveTemp(PendingCombatEvents);
	PendingCombatEvents.Reset();

	const bool bInSession = UCSAuthority::IsSessionActive(this);
	for (FCSCombatEvent& Event : Events)
	{
		const int32 Zone = static_cast<int32>(Event.Zone);
		// TargetAllClients also dispatches to the sender, so the authority's own
		// HUD is fed through the same receive handler as everyone else's.
		if (bInSession)
		{
			RpcCombatEvent(Event.VictimId, Event.InstigatorId, Event.Damage, Event.bKilled, Zone, Event.WeaponName, Event.FromLocation);
		}
		else
		{
			RpcCombatEvent_Receive(Event.VictimId, Event.InstigatorId, Event.Damage, Event.bKilled, Zone, Event.WeaponName, Event.FromLocation);
		}
	}
}

void ACSMatchDirector::RpcCombatEvent_Receive(int32 VictimId, int32 InstigatorId, float Damage, bool bKilled, int32 Zone, FString& WeaponName, FVector FromLocation)
{
	FCSCombatEvent Event;
	Event.VictimId = VictimId;
	Event.InstigatorId = InstigatorId;
	Event.Damage = Damage;
	Event.bKilled = bKilled;
	Event.Zone = static_cast<ECSHitZone>(FMath::Clamp(Zone, 0, 255));
	Event.WeaponName = WeaponName;
	Event.FromLocation = FromLocation;

	UE_LOG(LogCSCombat, Log, TEXT("Combat event: %d -> %d, %.0f dmg%s%s (%s)"),
		InstigatorId, VictimId, Damage, bKilled ? TEXT(", KILL") : TEXT(""),
		Event.Zone == ECSHitZone::Head ? TEXT(", head") : TEXT(""), *WeaponName);

	OnCombatEvent.Broadcast(Event);
}

// ---------------------------------------------------------------------------
// Anti-cheat (Stage 8)
// ---------------------------------------------------------------------------

bool ACSMatchDirector::GuardRequest(int32 PlayerId, ECSRequestKind Kind)
{
	if (!UCSAuthority::IsGameAuthority(this))
	{
		return true;
	}
	const bool bAllowed = CheatGuard.AllowRequest(PlayerId, Kind, UCSAuthority::GetNetworkTimeSeconds(this));
	if (!bAllowed && UCSCombatSettings::Get()->bLogRejections)
	{
		UE_LOG(LogCSAuth, Verbose, TEXT("Request %d from player %d dropped by the cheat guard."), static_cast<int32>(Kind), PlayerId);
	}
	return bAllowed;
}

void ACSMatchDirector::ResetScores()
{
	CS_AUTHORITY_ONLY(this);

	for (FCSPlayerCombatRecord& Record : Records)
	{
		if (Record.PlayerId != 0 && (Record.Kills != 0 || Record.Deaths != 0))
		{
			Record.Kills = 0;
			Record.Deaths = 0;
			Record.Headshots = 0;
			Record.DamageDealt = 0;
			OnRecordsChanged.Broadcast(Record.PlayerId);
		}
	}
	UE_LOG(LogCSAuth, Log, TEXT("Scores reset for the new round."));
}

// ---------------------------------------------------------------------------
// v1.1 grenades
// ---------------------------------------------------------------------------

bool ACSMatchDirector::TryThrowGrenade(int32 PlayerId, const FVector& Origin, const FVector& Direction,
	const FVector& PawnLocation, const FVector& PawnVelocity)
{
	CS_AUTHORITY_ONLY_RET(this, false);

	const UCSCombatSettings* Settings = UCSCombatSettings::Get();
	FCSPlayerCombatRecord* Record = FindRecordMutable(PlayerId);
	ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(this, PlayerId);
	if (!Record || !Record->bAlive || !Inventory)
	{
		return false;
	}
	const ACSGameState* GS = ModeState(this);
	if (GS && GS->GetMatchPhase() == ECSMatchPhase::PostMatch)
	{
		return false;
	}

	const FCSLoadoutView Loadout = GetLoadout(PlayerId);
	if (!Loadout.bGrenade || Loadout.RoundsInMag <= 0)
	{
		return false;
	}

	// Same plausibility rules as a shot: sane direction, thrown from near the pawn.
	const FVector Dir = Direction.GetSafeNormal();
	if (Dir.IsNearlyZero() || FVector::DistSquared(Origin, PawnLocation) > FMath::Square(Settings->MaxFireOriginDeviation))
	{
		return false;
	}
	const double Now = UCSAuthority::GetNetworkTimeSeconds(this);
	if (const double* Last = LastThrowTime.Find(PlayerId); Last && Now - *Last < Settings->GrenadeThrowInterval * 0.85)
	{
		return false;
	}
	LastThrowTime.Add(PlayerId, Now);

	// Consume it; an empty hand goes back to the pistol.
	const int32 Slot = Loadout.Slot;
	Inventory->RemoveFromSlot(Slot, 1);
	FCSInventorySlot Left;
	if (!Inventory->GetSlot(Slot, Left) || Left.IsEmpty())
	{
		Inventory->SetEquippedSlot(INDEX_NONE);
	}
	CancelProtection(PlayerId, TEXT("threw a grenade"));

	// Along the view with a little lift, plus some of the thrower's own motion.
	const FVector Velocity = Dir * Settings->GrenadeThrowSpeed + FVector(0.f, 0.f, 160.f) + PawnVelocity * 0.5f;
	const FVector Start = Origin + Dir * 30.f - FVector(0.f, 0.f, 8.f);
	const int32 Serial = NextGrenadeSerial++ + PlayerId * 100000;

	UE_LOG(LogCSCombat, Log, TEXT("Player %d threw grenade %d."), PlayerId, Serial);
	if (UCSAuthority::IsSessionActive(this))
	{
		RpcGrenadeThrown(Serial, PlayerId, Start, Velocity);
	}
	else
	{
		RpcGrenadeThrown_Receive(Serial, PlayerId, Start, Velocity);
	}
	OnRecordsChanged.Broadcast(PlayerId);
	return true;
}

void ACSMatchDirector::RpcGrenadeThrown_Receive(int32 Serial, int32 ThrowerId, FVector Origin, FVector Velocity)
{
	UWorld* World = GetWorld();
	if (!World || ACSGrenade::FindBySerial(this, Serial))
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	if (ACSGrenade* Grenade = World->SpawnActor<ACSGrenade>(ACSGrenade::StaticClass(), Origin, Velocity.Rotation(), Params))
	{
		Grenade->Launch(Serial, ThrowerId, Velocity, UCSCombatSettings::Get()->GrenadeFuseSeconds);
	}
	// Everybody else sees the throwing motion (the thrower already played it).
	if (ACSCharacter* Thrower = FindPawnForPlayer(this, ThrowerId))
	{
		if (!Thrower->IsLocallyControlled())
		{
			Thrower->PlayThrowPresentation(/*bFromRelease*/ true);
		}
	}
}

void ACSMatchDirector::ExplodeGrenade(ACSGrenade* Grenade)
{
	CS_AUTHORITY_ONLY(this);
	if (!Grenade)
	{
		return;
	}

	const UCSCombatSettings* Settings = UCSCombatSettings::Get();
	const FVector Center = Grenade->GetActorLocation() + FVector(0.f, 0.f, 10.f);
	const int32 ThrowerId = Grenade->GetThrowerId();
	const int32 Serial = Grenade->GetSerial();

	// Walls stop the blast: only players with a clear line to the centre are hit.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CSGrenadeBlast), false, Grenade);
	for (TActorIterator<ACSCharacter> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}

	TArray<TPair<int32, float>> Hits;
	for (const FCSPlayerCombatRecord& Record : Records)
	{
		const ACSCharacter* Pawn = Record.bAlive ? FindPawnForPlayer(this, Record.PlayerId) : nullptr;
		if (!Pawn)
		{
			continue;
		}
		// Closest of feet, chest and head decides.
		float Best = TNumericLimits<float>::Max();
		for (const float Z : { -60.f, 10.f, 60.f })
		{
			const FVector Point = Pawn->GetActorLocation() + FVector(0.f, 0.f, Z);
			const float Distance = FVector::Dist(Center, Point);
			if (Distance < Settings->GrenadeRadius && Distance < Best
				&& !GetWorld()->LineTraceTestByChannel(Center, Point, ECC_WorldStatic, Params))
			{
				Best = Distance;
			}
		}
		if (Best < Settings->GrenadeRadius)
		{
			const float Falloff = FMath::Pow(1.f - Best / Settings->GrenadeRadius, 1.3f);
			Hits.Add(TPair<int32, float>(Record.PlayerId, FMath::Max(1.f, Settings->GrenadeMaxDamage * Falloff)));
		}
	}

	CombatWeaponOverride = TEXT("HE Grenade");
	for (const TPair<int32, float>& Hit : Hits)
	{
		ApplyDamage(Hit.Key, ThrowerId, Hit.Value, ECSHitZone::Torso);
	}
	FlushCombatEvents();
	CombatWeaponOverride.Reset();

	// Right away here, so the fuse check does not fire again while the RPC
	// travels back to this peer (the receive handler then finds it done).
	Grenade->Explode(Center);
	UE_LOG(LogCSCombat, Log, TEXT("Grenade %d by %d exploded, %d player(s) hit."), Serial, ThrowerId, Hits.Num());
	if (UCSAuthority::IsSessionActive(this))
	{
		RpcGrenadeExploded(Serial, Center);
	}
	else
	{
		RpcGrenadeExploded_Receive(Serial, Center);
	}
}

void ACSMatchDirector::RpcGrenadeExploded_Receive(int32 Serial, FVector Location)
{
	if (const ACSGrenade* Done = ACSGrenade::FindBySerial(this, Serial, /*bIncludeExploded*/ true); Done && !ACSGrenade::FindBySerial(this, Serial))
	{
		return; // already went off here (the authority's own copy)
	}
	if (ACSGrenade* Grenade = ACSGrenade::FindBySerial(this, Serial))
	{
		Grenade->Explode(Location);
		return;
	}
	// Never saw it fly (joined mid-air): still show the blast.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	if (ACSGrenade* Grenade = GetWorld()->SpawnActor<ACSGrenade>(ACSGrenade::StaticClass(), Location, FRotator::ZeroRotator, Params))
	{
		Grenade->Explode(Location);
	}
}

// ---------------------------------------------------------------------------
// v1.2 accounts
// ---------------------------------------------------------------------------

void ACSMatchDirector::NoteIdentity(int32 PlayerId, const FString& ProfileId)
{
	CS_AUTHORITY_ONLY(this);
	if (PlayerId == 0 || ProfileId.IsEmpty())
	{
		return;
	}
	ProfileIds.Add(PlayerId, ProfileId);
}

FString ACSMatchDirector::GetProfileIdFor(int32 PlayerId) const
{
	const FString* Found = ProfileIds.Find(PlayerId);
	return Found ? *Found : FString();
}
