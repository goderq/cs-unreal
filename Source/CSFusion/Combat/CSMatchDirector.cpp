// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Combat/CSMatchDirector.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CSMatchDirector.fusion)

#include "Core/CSAuthority.h"
#include "Core/CSCombatSettings.h"
#include "Core/CSLog.h"
#include "EngineUtils.h"
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
	OnRecordsChanged.Broadcast(PlayerId);
}

ECSFireRejection ACSMatchDirector::ValidateFire(int32 PlayerId, const UCSWeaponDefinition* Weapon,
	const FVector& ClaimedOrigin, const FVector& ClaimedDirection,
	const FVector& AuthoritativePawnLocation) const
{
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

	if (Record.StarterRoundsInMag <= 0)
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

	if (FCSPlayerCombatRecord* Record = FindRecordMutable(PlayerId))
	{
		Record->StarterRoundsInMag = FMath::Max(0, Record->StarterRoundsInMag - 1);
		Record->LastFireNetworkTime = UCSAuthority::GetNetworkTimeSeconds(this);
		OnRecordsChanged.Broadcast(PlayerId);
	}
}

bool ACSMatchDirector::BeginReload(int32 PlayerId, const UCSWeaponDefinition* Weapon)
{
	CS_AUTHORITY_ONLY_RET(this, false);

	if (!Weapon)
	{
		return false;
	}

	FCSPlayerCombatRecord* Record = FindRecordMutable(PlayerId);
	if (!Record || !Record->bAlive)
	{
		return false;
	}

	if (Record->StarterRoundsInMag >= Weapon->MagazineSize)
	{
		return false;
	}

	const double Now = UCSAuthority::GetNetworkTimeSeconds(this);
	if (Record->ReloadCompleteNetworkTime > 0.0 && Now < Record->ReloadCompleteNetworkTime)
	{
		return false; // already reloading
	}

	Record->ReloadCompleteNetworkTime = Now + Weapon->ReloadSeconds;
	OnRecordsChanged.Broadcast(PlayerId);
	return true;
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
	const UCSCombatSettings* Settings = UCSCombatSettings::Get();
	const UCSWeaponDefinition* Starter = Settings->StarterWeapon.LoadSynchronous();

	for (FCSPlayerCombatRecord& Record : Records)
	{
		// Complete reloads.
		if (Record.ReloadCompleteNetworkTime > 0.0 && Now >= Record.ReloadCompleteNetworkTime)
		{
			Record.ReloadCompleteNetworkTime = 0.0;
			Record.StarterRoundsInMag = Starter ? Starter->MagazineSize : 12;
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
