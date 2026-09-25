// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "GameModes/CSGameMode.h"

#include "Characters/CSCharacter.h"
#include "Combat/CSMatchDirector.h"
#include "AI/CSBotManager.h"
#include "Core/CSAuthority.h"
#include "Core/CSLog.h"
#include "Core/CSModeSettings.h"
#include "Account/CSAccountSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Multiplayer/CSSessionSubsystem.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "GameModes/CSGameState.h"
#include "Multiplayer/CSGameInstance.h"
#include "Player/CSPlayerController.h"
#include "Player/CSPlayerState.h"
#include "UI/CSHUD.h"

ACSGameMode::ACSGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f; // Match flow does not need frame rate.

	GameStateClass = ACSGameState::StaticClass();
	PlayerStateClass = ACSPlayerState::StaticClass();
	PlayerControllerClass = ACSPlayerController::StaticClass();
	DefaultPawnClass = ACSCharacter::StaticClass();
	HUDClass = ACSHUD::StaticClass();

	bStartPlayersAsSpectators = false;
}

void ACSGameMode::BeginPlay()
{
	Super::BeginPlay();

	CachePlayerStarts();

	// Test hook: -roundtime=N shortens the round so the end-of-round flow
	// (banner, scoreboard, score reset) can be exercised in an automated run.
	float RoundOverride = 0.f;
	if (!UE_BUILD_SHIPPING && FParse::Value(FCommandLine::Get(), TEXT("roundtime="), RoundOverride) && RoundOverride > 0.f)
	{
		RoundSecondsOverride = FMath::Max(10.f, RoundOverride);
	}

	UE_LOG(LogCS, Log,
		TEXT("CSGameMode BeginPlay. Authority: %s. PlayerStarts found: %d"),
		UCSAuthority::IsGameAuthority(this) ? TEXT("YES") : TEXT("no"),
		CachedPlayerStarts.Num());

	// Every peer gets a local bot manager; only the current authority's acts,
	// so bots survive a master migration (Stage 7).
	if (!ACSBotManager::Get(this))
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		GetWorld()->SpawnActor<ACSBotManager>(ACSBotManager::StaticClass(), FTransform::Identity, Params);
	}

	if (UCSAuthority::IsGameAuthority(this))
	{
		ConfigureModeIfNeeded();
		EnsureMatchDirector();
		// v2.0: nothing spawns on the floor - guns are bought, ammo comes from
		// the machines. Pickup markers still in a level are simply ignored.

		if (ACSGameState* GS = GetCSGameState())
		{
			GS->SetMatchPhase(ECSMatchPhase::WaitingForPlayers);
		}
	}
}

void ACSGameMode::EnsureMatchDirector()
{
	CS_AUTHORITY_ONLY(this);

	if (ACSMatchDirector::Get(this))
	{
		return;
	}

	// Spawned rather than map-placed so the map asset stays free of gameplay
	// singletons. Its UFusionActorComponent uses MasterClient ownership, so it
	// survives master migration by re-targeting rather than being destroyed.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Name = TEXT("CSMatchDirector");

	ACSMatchDirector* Director = GetWorld()->SpawnActor<ACSMatchDirector>(
		ACSMatchDirector::StaticClass(), FTransform::Identity, Params);

	UE_LOG(LogCSAuth, Log, TEXT("Authority spawned MatchDirector: %s"), *GetNameSafe(Director));
}

AActor* ACSGameMode::GetPlayerStartByIndex(int32 Index) const
{
	if (CachedPlayerStarts.Num() == 0)
	{
		return nullptr;
	}

	const int32 Wrapped = FMath::Abs(Index) % CachedPlayerStarts.Num();
	return CachedPlayerStarts[Wrapped];
}

void ACSGameMode::GetSpawnIndicesForTeam(ECSTeam Team, TArray<int32>& OutIndices) const
{
	OutIndices.Reset();
	const FName TeamTag = Team == ECSTeam::Alpha ? FName(TEXT("Alpha")) : (Team == ECSTeam::Bravo ? FName(TEXT("Bravo")) : NAME_None);

	TArray<int32> Untagged;
	for (int32 i = 0; i < CachedPlayerStarts.Num(); ++i)
	{
		const APlayerStart* Start = Cast<APlayerStart>(CachedPlayerStarts[i]);
		const FName Tag = Start ? Start->PlayerStartTag : NAME_None;
		if (Team == ECSTeam::None || Tag == TeamTag)
		{
			OutIndices.Add(i);
		}
		else if (Tag.IsNone())
		{
			Untagged.Add(i);
		}
	}
	if (OutIndices.Num() == 0)
	{
		OutIndices = Untagged.Num() > 0 ? Untagged : TArray<int32>();
	}
	if (OutIndices.Num() == 0)
	{
		for (int32 i = 0; i < CachedPlayerStarts.Num(); ++i)
		{
			OutIndices.Add(i);
		}
	}
}

void ACSGameMode::CachePlayerStarts()
{
	CachedPlayerStarts.Reset();

	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		CachedPlayerStarts.Add(*It);
	}

	// Actor iteration order is not guaranteed to match between peers, so sort
	// by a value every peer agrees on. Spawn indices must be stable because
	// each client picks its own start with no negotiation.
	CachedPlayerStarts.Sort([](const AActor& A, const AActor& B)
	{
		return A.GetName() < B.GetName();
	});
}

AActor* ACSGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (CachedPlayerStarts.Num() == 0)
	{
		CachePlayerStarts();
	}

	if (CachedPlayerStarts.Num() == 0)
	{
		UE_LOG(LogCS, Warning, TEXT("No APlayerStart in the level; falling back to engine default."));
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	// Key the start off the Photon player id. Every peer derives the same
	// index for the same player, so two clients spawning simultaneously do not
	// land on the same pad as long as there are enough starts.
	const int32 PlayerId = UCSAuthority::GetLocalPlayerId(this);
	const int32 Index = FMath::Abs(PlayerId) % CachedPlayerStarts.Num();

	UE_LOG(LogCS, Verbose, TEXT("ChoosePlayerStart: PlayerId=%d -> start #%d (%s)"),
		PlayerId, Index, *GetNameSafe(CachedPlayerStarts[Index]));

	return CachedPlayerStarts[Index];
}

void ACSGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// NOT authority-gated on purpose: under Fusion each client spawns its own
	// controller and pawn locally. See the class comment.
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

void ACSGameMode::RestartGame()
{
	// Authority-gated: without this every peer restarts the round and the
	// effects multiply by player count.
	CS_AUTHORITY_ONLY(this);

	UE_LOG(LogCS, Log, TEXT("Authority restarting the round."));
	Super::RestartGame();
}

ACSGameState* ACSGameMode::GetCSGameState() const
{
	return GetWorld() ? GetWorld()->GetGameState<ACSGameState>() : nullptr;
}

void ACSGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateMatchFlow();
}

void ACSGameMode::UpdateMatchFlow()
{
	CS_AUTHORITY_ONLY(this);

	ACSGameState* GS = GetCSGameState();
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	if (!GS || !Director)
	{
		return;
	}

	ConfigureModeIfNeeded();

	const FCSModeRules& Rules = GS->GetRules();
	const double Now = UCSAuthority::GetNetworkTimeSeconds(this);
	const int32 PlayerCount = UCSAuthority::GetRoomPlayerCount(this);

	switch (GS->GetMatchPhase())
	{
	case ECSMatchPhase::WaitingForPlayers:
		if (PlayerCount >= MinPlayersToStart)
		{
			// Phase changes need no announcement RPC: MatchPhase is replicated
			// and every HUD shows its own banner when it sees the change. (The
			// old GameInstance RPC fired before Fusion had registered the
			// GameInstance and was dropped: "Missing Function Descriptor".)
			GS->SetPhaseEndTime(Now + WarmupSeconds);
			GS->SetMatchPhase(ECSMatchPhase::Warmup);
		}
		break;

	case ECSMatchPhase::Warmup:
		if (GS->GetPhaseTimeRemaining() <= 0.f)
		{
			// Fresh match: kills, money and inventories from warmup do not count.
			GS->ResetTeamScores();
			GS->SetWinner(ECSTeam::None, 0);
			GS->SetLossStreak(ECSTeam::Alpha, 0);
			GS->SetLossStreak(ECSTeam::Bravo, 0);
			GS->SetMatchPhase(ECSMatchPhase::InProgress);
			Director->ResetForNewMatch();
			StartBackendMatch();

			if (Rules.bRounds)
			{
				BeginRound(1);
			}
			else
			{
				GS->SetRoundNumber(0);
				GS->SetBuyEndNetworkTime(0.0);
				GS->SetPhaseEndTime(Now + GetTimeLimit(Rules));
			}
		}
		break;

	case ECSMatchPhase::InProgress:
		if (Rules.bRounds)
		{
			UpdateRounds(Rules);
		}
		else
		{
			UpdateScoreLimit(Rules);
		}
		break;

	case ECSMatchPhase::PostMatch:
		if (GS->GetPhaseTimeRemaining() <= 0.f)
		{
			GS->SetMatchPhase(ECSMatchPhase::WaitingForPlayers);
		}
		break;
	}
}

float ACSGameMode::GetTimeLimit(const FCSModeRules& Rules) const
{
	// -roundtime= (tests) overrides the configured limit.
	return RoundSecondsOverride > 0.f ? RoundSecondsOverride : Rules.TimeLimitSeconds;
}

void ACSGameMode::FinishMatch(ECSTeam WinnerTeam, int32 WinnerPlayerId)
{
	ACSGameState* GS = GetCSGameState();
	GS->SetWinner(WinnerTeam, WinnerPlayerId);
	GS->SetBuyEndNetworkTime(0.0);
	GS->SetPhaseEndTime(UCSAuthority::GetNetworkTimeSeconds(this) + PostMatchSeconds);
	GS->SetMatchPhase(ECSMatchPhase::PostMatch);
	UE_LOG(LogCS, Log, TEXT("Match over. Winner: team %d / player %d."), static_cast<int32>(WinnerTeam), WinnerPlayerId);
	ReportMatchToBackend(WinnerTeam, WinnerPlayerId);
}

void ACSGameMode::UpdateScoreLimit(const FCSModeRules& Rules)
{
	ACSGameState* GS = GetCSGameState();
	const ACSMatchDirector* Director = ACSMatchDirector::Get(this);

	if (Rules.bTeams)
	{
		// Team Deathmatch: first team to the kill limit, or the leader at time.
		const int32 Alpha = GS->GetTeamScore(ECSTeam::Alpha);
		const int32 Bravo = GS->GetTeamScore(ECSTeam::Bravo);
		if (Alpha >= Rules.ScoreLimit || Bravo >= Rules.ScoreLimit || GS->GetPhaseTimeRemaining() <= 0.f)
		{
			FinishMatch(Alpha == Bravo ? ECSTeam::None : (Alpha > Bravo ? ECSTeam::Alpha : ECSTeam::Bravo), 0);
		}
		return;
	}

	// Deathmatch: first player to the kill limit, or the top fragger at time.
	int32 BestId = 0;
	int32 BestKills = -1;
	bool bTie = false;
	for (const FCSPlayerCombatRecord& Record : Director->GetAllRecords())
	{
		if (Record.Kills > BestKills)
		{
			BestKills = Record.Kills;
			BestId = Record.PlayerId;
			bTie = false;
		}
		else if (Record.Kills == BestKills)
		{
			bTie = true;
		}
	}
	if (BestKills >= Rules.ScoreLimit || GS->GetPhaseTimeRemaining() <= 0.f)
	{
		FinishMatch(ECSTeam::None, bTie ? 0 : BestId);
	}
}

void ACSGameMode::BeginRound(int32 Round)
{
	ACSGameState* GS = GetCSGameState();
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	const FCSModeRules& Rules = GS->GetRules();
	const double Now = UCSAuthority::GetNetworkTimeSeconds(this);

	GS->SetRoundNumber(Round);
	GS->SetWinner(ECSTeam::None, 0);
	GS->SetBuyEndNetworkTime(Now + Rules.BuySeconds);
	GS->SetPhaseEndTime(Now + GetTimeLimit(Rules));
	if (Round > 1)
	{
		Director->StartNewRound();
	}
	UE_LOG(LogCS, Log, TEXT("Round %d begins (buy time %.0fs)."), Round, Rules.BuySeconds);
}

void ACSGameMode::UpdateRounds(const FCSModeRules& Rules)
{
	ACSGameState* GS = GetCSGameState();
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);

	// A decided round shows its result for RoundEndSeconds. The decision
	// lives in the replicated WinnerTeam, so a master migration in the middle
	// of it neither loses the result nor pays it out twice.
	if (IsRoundDecided())
	{
		if (GS->GetPhaseTimeRemaining() > 0.f)
		{
			return;
		}
		const ECSTeam Winner = GS->GetWinnerTeam();
		if (Winner != ECSTeam::None && GS->GetTeamScore(Winner) >= Rules.ScoreLimit)
		{
			FinishMatch(Winner, 0);
			return;
		}
		BeginRound(GS->GetRoundNumber() + 1);
		return;
	}

	const int32 AlphaMembers = Director->CountMembers(ECSTeam::Alpha);
	const int32 BravoMembers = Director->CountMembers(ECSTeam::Bravo);
	const int32 AlphaAlive = Director->CountAlive(ECSTeam::Alpha);
	const int32 BravoAlive = Director->CountAlive(ECSTeam::Bravo);

	ECSTeam Winner = ECSTeam::None;
	bool bDecided = false;
	if (AlphaMembers > 0 && BravoMembers > 0 && (AlphaAlive == 0 || BravoAlive == 0))
	{
		// Elimination.
		Winner = AlphaAlive > 0 ? ECSTeam::Alpha : (BravoAlive > 0 ? ECSTeam::Bravo : ECSTeam::None);
		bDecided = true;
	}
	else if (GS->GetPhaseTimeRemaining() <= 0.f)
	{
		// Time: more players standing wins; equal is a draw.
		Winner = AlphaAlive == BravoAlive ? ECSTeam::None : (AlphaAlive > BravoAlive ? ECSTeam::Alpha : ECSTeam::Bravo);
		bDecided = true;
	}
	if (!bDecided)
	{
		return;
	}

	FinishRound(Winner, Rules);
}

bool ACSGameMode::IsRoundDecided() const
{
	// WinnerTeam is set for the round-end pause; a draw marks it with the
	// sentinel player id -1.
	const ACSGameState* GS = GetCSGameState();
	return GS && (GS->GetWinnerTeam() != ECSTeam::None || GS->GetWinnerPlayerId() == -1);
}

void ACSGameMode::FinishRound(ECSTeam Winner, const FCSModeRules& Rules)
{
	ACSGameState* GS = GetCSGameState();
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);

	GS->SetWinner(Winner, Winner == ECSTeam::None ? -1 : 0);
	GS->SetBuyEndNetworkTime(0.0);
	GS->SetPhaseEndTime(UCSAuthority::GetNetworkTimeSeconds(this) + Rules.RoundEndSeconds);
	if (Winner != ECSTeam::None)
	{
		GS->AddTeamScore(Winner, 1);
	}

	// CS economy: the winners get the win bonus; the losers a consolation that
	// grows with every round lost in a row.
	for (const ECSTeam Team : { ECSTeam::Alpha, ECSTeam::Bravo })
	{
		int32 Reward = 0;
		if (Team == Winner)
		{
			Reward = Rules.RoundWinReward;
			GS->SetLossStreak(Team, 0);
		}
		else
		{
			const int32 Streak = GS->GetLossStreak(Team);
			Reward = FMath::Min(Rules.RoundLossReward + Streak * Rules.LossStreakBonus, Rules.LossRewardMax);
			GS->SetLossStreak(Team, Streak + 1);
		}
		for (const FCSPlayerCombatRecord& Record : Director->GetAllRecords())
		{
			if (Record.GetTeam() == Team)
			{
				Director->AddMoney(Record.PlayerId, Reward);
			}
		}
	}

	UE_LOG(LogCS, Log, TEXT("Round %d won by team %d. Score %d : %d."), GS->GetRoundNumber(), static_cast<int32>(Winner),
		GS->GetTeamScore(ECSTeam::Alpha), GS->GetTeamScore(ECSTeam::Bravo));
}

void ACSGameMode::AddInactivePlayer(APlayerState* LeavingPlayerState, APlayerController* PC)
{
	// Read the id before Super, which may move or rename the PlayerState.
	const int32 PlayerNumber = LeavingPlayerState ? FCString::Atoi(*LeavingPlayerState->SavedNetworkAddress) : 0;

	Super::AddInactivePlayer(LeavingPlayerState, PC);

	if (PlayerNumber == 0 || !UCSAuthority::IsGameAuthority(this))
	{
		return;
	}

	UE_LOG(LogCSAuth, Log, TEXT("Fusion reports player %d left the room."), PlayerNumber);

	if (ACSMatchDirector* Director = ACSMatchDirector::Get(this))
	{
		// The server - here the Master Client - decides what drops. The leaving
		// client has no say, and RemovePlayer is idempotent, so a repeated
		// notification cannot create a second set of loot.
		Director->RemovePlayer(PlayerNumber, ECSDeathReason::Disconnected);
	}
}

void ACSGameMode::ConfigureModeIfNeeded()
{
	CS_AUTHORITY_ONLY(this);

	// The mode is fixed by whoever created the room; a new master after a
	// migration finds it already configured in the replicated GameState.
	ACSGameState* GS = GetCSGameState();
	if (!GS || GS->IsModeConfigured())
	{
		return;
	}
	ECSGameModeType Mode = ECSGameModeType::Deathmatch;
	if (const UCSSessionSubsystem* Session = GetGameInstance()->GetSubsystem<UCSSessionSubsystem>())
	{
		Mode = Session->GetMatchMode();
	}
	GS->ConfigureMode(Mode);
}

void ACSGameMode::StartBackendMatch()
{
	CS_AUTHORITY_ONLY(this);

	ACSGameState* GS = GetCSGameState();
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	if (!GS || !Director)
	{
		return;
	}
	GS->SetBackendMatchId(FString());
	Director->ClearTickets();

	UCSAccountSubsystem* Account = UCSAccountSubsystem::Get(this);
	if (!Account || !Account->IsReady())
	{
		return; // a guest host: this match is not recorded
	}

	const UCSSessionSubsystem* Session = GetGameInstance()->GetSubsystem<UCSSessionSubsystem>();
	const bool bOffline = !UCSAuthority::IsSessionActive(this);
	const FString Room = (!bOffline && Session) ? Session->GetRoomName() : FString();
	FString MapName = GetWorld()->GetMapName();
	MapName.RemoveFromStart(GetWorld()->StreamingLevelsPrefix);
	MapName.RemoveFromStart(TEXT("Lvl_"));
	const int32 LocalId = UCSAuthority::GetLocalPlayerId(this);

	TWeakObjectPtr<ACSGameMode> WeakThis(this);
	Account->MatchStart(UCSModeSettings::ModeTag(GS->GetGameMode()), MapName, Room, bOffline,
		[WeakThis, LocalId](bool bOk, const FString& MatchId, const FString& Ticket)
		{
			ACSGameMode* Self = WeakThis.Get();
			if (!Self || !bOk || !UCSAuthority::IsGameAuthority(Self))
			{
				return;
			}
			ACSGameState* State = Self->GetCSGameState();
			ACSMatchDirector* D = ACSMatchDirector::Get(Self);
			// The answer came too late (the match already ended): nothing to record.
			if (!State || !D || State->GetMatchPhase() != ECSMatchPhase::InProgress)
			{
				return;
			}
			State->SetBackendMatchId(MatchId);
			D->NoteTicket(LocalId, Ticket);
		});
}

void ACSGameMode::ReportMatchToBackend(ECSTeam WinnerTeam, int32 WinnerPlayerId)
{
	// Authority only, and only once per match: the Master Client is the peer
	// that knows every record. Each signed-in player is identified by the
	// ticket they got from the backend themselves; guests have none and are
	// left out, bots are never listed. The backend checks the rest.
	CS_AUTHORITY_ONLY(this);

	UCSAccountSubsystem* Account = UCSAccountSubsystem::Get(this);
	ACSMatchDirector* Director = ACSMatchDirector::Get(this);
	ACSGameState* GS = GetCSGameState();
	if (!Account || !Account->IsReady() || !Director || !GS || GS->GetBackendMatchId().IsEmpty())
	{
		return;
	}

	const bool bTeams = GS->GetRules().bTeams;
	TArray<TSharedPtr<FJsonValue>> Players;
	int32 Humans = 0;
	for (const FCSPlayerCombatRecord& Record : Director->GetAllRecords())
	{
		if (CSBots::IsBotId(Record.PlayerId))
		{
			continue;
		}
		++Humans;
		const FString Ticket = Director->GetTicketFor(Record.PlayerId);
		if (Ticket.IsEmpty())
		{
			continue; // a player without an account
		}
		const TSharedRef<FJsonObject> Player = MakeShared<FJsonObject>();
		Player->SetStringField(TEXT("ticket"), Ticket);
		Player->SetNumberField(TEXT("team"), bTeams ? static_cast<int32>(Record.Team) : 0);
		Player->SetNumberField(TEXT("kills"), Record.Kills);
		Player->SetNumberField(TEXT("deaths"), Record.Deaths);
		Player->SetNumberField(TEXT("headshots"), Record.Headshots);
		Player->SetNumberField(TEXT("damage"), Record.DamageDealt);
		Player->SetNumberField(TEXT("money"), Record.Money);
		Player->SetBoolField(TEXT("won"), bTeams
			? (WinnerTeam != ECSTeam::None && Record.GetTeam() == WinnerTeam)
			: (WinnerPlayerId != 0 && WinnerPlayerId == Record.PlayerId));
		Players.Add(MakeShared<FJsonValueObject>(Player));
	}
	if (Players.Num() == 0)
	{
		return;
	}

	const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetNumberField(TEXT("winner_team"), bTeams ? static_cast<int32>(WinnerTeam) : 0);
	Report->SetNumberField(TEXT("rounds"), GS->GetRules().bRounds ? GS->GetRoundNumber() : 0);
	Report->SetBoolField(TEXT("bots"), Director->HadBotsThisMatch());
	Report->SetNumberField(TEXT("humans"), Humans);
	Report->SetArrayField(TEXT("players"), Players);
	Account->MatchReport(GS->GetBackendMatchId(), Report);

	UE_LOG(LogCS, Log, TEXT("Match report sent: %d player(s) with tickets, %d human(s), bots %s."),
		Players.Num(), Humans, Director->HadBotsThisMatch() ? TEXT("yes") : TEXT("no"));
	// Reported once; a new match gets a new id.
	GS->SetBackendMatchId(FString());
}
