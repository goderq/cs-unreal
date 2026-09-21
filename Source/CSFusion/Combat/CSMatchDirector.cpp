// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Combat/CSMatchDirector.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CSMatchDirector.fusion)

#include "Core/CSAuthority.h"
#include "Core/CSCombatSettings.h"
#include "Core/CSLog.h"
#include "EngineUtils.h"
#include "Inventory/CSPlayerInventory.h"
#include "Items/CSItemDefinition.h"
#include "Items/CSItemSettings.h"
#include "Net/UnrealNetwork.h"
#include "Weapons/CSWeaponDefinition.h"

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

	Records.Add(Record);

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

	UE_LOG(LogCSAuth, Log, TEXT("Registered player %d (hp %.0f, %d rounds)."),
		PlayerId, Record.Health, Record.StarterRoundsInMag);

	OnRecordsChanged.Broadcast(PlayerId);
}

void ACSMatchDirector::RemovePlayer(int32 PlayerId, ECSDeathReason Reason)
{
	CS_AUTHORITY_ONLY(this);

	const int32 Index = FindRecordIndex(PlayerId);
	if (Index == INDEX_NONE)
	{
		return;
	}

	// Stage 4 hook: drop this player's inventory as world loot BEFORE the
	// record goes away, and make that drop idempotent so a duplicated
	// disconnect callback cannot spawn the loot twice. The starter pistol is
	// never part of it.
	UE_LOG(LogCSAuth, Log, TEXT("Removing player %d (%s)."),
		PlayerId, *UEnum::GetValueAsString(Reason));

	Records.RemoveAt(Index);
	LastKnownAlive.Remove(PlayerId);

	if (ACSPlayerInventory* Inventory = ACSPlayerInventory::Find(this, PlayerId))
	{
		Inventory->Destroy();
	}

	OnRecordsChanged.Broadcast(PlayerId);
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

	UE_LOG(LogCSCombat, Verbose,
		TEXT("Player %d hit by %d for %.1f (%s), health %.0f -> %.0f"),
		VictimId, InstigatorId, Applied, *UEnum::GetValueAsString(Zone), Before, Victim->Health);

	if (Victim->Health <= 0.f)
	{
		Victim->bAlive = false;
		Victim->Deaths += 1;
		Victim->RespawnAtNetworkTime =
			UCSAuthority::GetNetworkTimeSeconds(this) + Settings->RespawnDelaySeconds;

		if (InstigatorId != VictimId)
		{
			if (FCSPlayerCombatRecord* Killer = FindRecordMutable(InstigatorId))
			{
				Killer->Kills += 1;
			}
		}

		UE_LOG(LogCSCombat, Log, TEXT("Player %d killed by %d (%s)."),
			VictimId, InstigatorId, *UEnum::GetValueAsString(Zone));

		OnPlayerKilled.Broadcast(VictimId, InstigatorId, Zone);
	}

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

	const UCSCombatSettings* Settings = UCSCombatSettings::Get();
	const UCSWeaponDefinition* Starter = Settings->StarterWeapon.LoadSynchronous();

	Record->Health = Settings->MaxHealth;
	Record->Armor = 0.f;
	Record->bAlive = true;
	// The starter pistol is restored in full on every respawn, by design: it is
	// not an inventory item and can never be lost.
	Record->StarterRoundsInMag = Starter ? Starter->MagazineSize : 12;
	Record->LastFireNetworkTime = 0.0;
	Record->ReloadCompleteNetworkTime = 0.0;
	Record->ReloadSlot = INDEX_NONE;
	Record->RespawnAtNetworkTime = 0.0;
	Record->RespawnPointIndex = SpawnPointIndex;
	Record->RespawnCounter += 1;

	UE_LOG(LogCSCombat, Log, TEXT("Player %d respawned at point %d (counter %d)."),
		PlayerId, SpawnPointIndex, Record->RespawnCounter);

	OnRecordsChanged.Broadcast(PlayerId);
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void ACSMatchDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickAuthority();
}

void ACSMatchDirector::TickAuthority()
{
	CS_AUTHORITY_ONLY(this);

	const double Now = UCSAuthority::GetNetworkTimeSeconds(this);

	for (FCSPlayerCombatRecord& Record : Records)
	{
		// Complete reloads.
		if (Record.ReloadCompleteNetworkTime > 0.0 && Now >= Record.ReloadCompleteNetworkTime)
		{
			CompleteReload(Record);
			OnRecordsChanged.Broadcast(Record.PlayerId);
		}

		// Respawn is driven here rather than by the dead client, so refusing to
		// die gains a cheater nothing.
		if (!Record.bAlive && Record.RespawnAtNetworkTime > 0.0 && Now >= Record.RespawnAtNetworkTime)
		{
			// Spread respawns across the available starts deterministically.
			const int32 SpawnIndex = FMath::Abs(Record.PlayerId + Record.RespawnCounter);
			RespawnPlayer(Record.PlayerId, SpawnIndex);
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
