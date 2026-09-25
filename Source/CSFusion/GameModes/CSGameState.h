// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Room-wide match state.
//
// Fusion networks the GameState implicitly with MasterClient ownership: only
// the current Master Client's writes propagate, and ownership re-targets
// automatically on master migration. That makes it the correct home for
// authoritative match data - a modified client physically cannot write it.
//
// Fusion also patches its own network time into AGameStateBase, so
// GetServerWorldTimeSeconds() is room-synchronised without extra work.
//
// Derived from AGameState (not AGameStateBase) to pair with ACSGameMode:
// AGameMode replicates MatchState through it, and Stage 4 disconnect loot
// needs AGameMode::InactivePlayerArray, which AGameModeBase does not have.

#pragma once

#include "CoreMinimal.h"
#include "Core/CSCoreTypes.h"
#include "GameFramework/GameState.h"
#include "CSGameState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCSMatchPhaseChanged, ECSMatchPhase, NewPhase);

UCLASS()
class CSFUSION_API ACSGameState : public AGameState
{
	GENERATED_BODY()

public:
	ACSGameState();

	virtual void BeginPlay() override;

	// --- Authoritative writes (Master Client only) -------------------------

	/** Advance the match phase. No-op on non-authority peers. */
	void SetMatchPhase(ECSMatchPhase NewPhase);

	/** Set the network time at which the current phase ends. Authority only. */
	void SetPhaseEndTime(double NetworkTimeSeconds);

	/** Add to a team score. Authority only. */
	void AddTeamScore(ECSTeam Team, int32 Delta);

	// --- Reads (every peer) ------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "CS|Match")
	ECSMatchPhase GetMatchPhase() const { return MatchPhase; }

	/** Seconds left in the current phase, clamped at 0. */
	UFUNCTION(BlueprintPure, Category = "CS|Match")
	float GetPhaseTimeRemaining() const;

	UFUNCTION(BlueprintPure, Category = "CS|Match")
	int32 GetTeamScore(ECSTeam Team) const;

	UPROPERTY(BlueprintAssignable, Category = "CS|Match")
	FCSMatchPhaseChanged OnMatchPhaseChanged;

	// --- Bots (Stage 7) -----------------------------------------------------
	// Kept here, in replicated match state, rather than on the creating
	// client: after a master migration the new Master Client reads them and
	// keeps the same number of bots at the same difficulty.

	/** Authority: configure bots once per match. */
	void SetBotSettings(int32 Count, ECSBotDifficulty Difficulty);

	/** -1 until the authority has configured the match. */
	int32 GetBotCount() const { return BotCount; }
	ECSBotDifficulty GetBotDifficulty() const { return BotDifficulty; }

	// --- Game mode (v1.1) ---------------------------------------------------
	// Replicated for the same reason as the bots: a new Master Client after a
	// migration continues the same match - mode, round, scores, streaks.

	/** Authority: set the mode once per match (host's choice or -mode=). */
	void ConfigureMode(ECSGameModeType Mode);
	bool IsModeConfigured() const { return bModeConfigured; }
	ECSGameModeType GetGameMode() const { return GameModeType; }
	const struct FCSModeRules& GetRules() const;

	int32 GetRoundNumber() const { return RoundNumber; }
	/** Rounds modes: network time the buy window closes. 0 = closed. */
	double GetBuyEndNetworkTime() const { return BuyEndNetworkTime; }
	float GetBuyTimeRemaining() const;
	ECSTeam GetWinnerTeam() const { return WinnerTeam; }
	int32 GetWinnerPlayerId() const { return WinnerPlayerId; }
	int32 GetLossStreak(ECSTeam Team) const { return Team == ECSTeam::Alpha ? LossStreakAlpha : LossStreakBravo; }

	void SetRoundNumber(int32 Round);
	void SetBuyEndNetworkTime(double Time);
	void SetWinner(ECSTeam Team, int32 PlayerId);
	void SetLossStreak(ECSTeam Team, int32 Streak);
	void ResetTeamScores();

	// --- v2.0 match record (functions/match) -----------------------------------
	// The id the backend gave this match when it began. Replicated so every
	// signed-in player can ask for their own participation ticket, and so a new
	// Master Client after a migration can still report the match.

	const FString& GetBackendMatchId() const { return BackendMatchId; }
	/** Authority only. */
	void SetBackendMatchId(const FString& MatchId);

protected:
	/**
	 * Fusion discovers replicated properties by scanning for Replicated /
	 * ReplicatedUsing, exactly like stock UE. GetLifetimeReplicatedProps is
	 * not required (and is not consulted) while Fusion drives replication - it
	 * is declared here anyway so the same classes keep working if the project
	 * is later switched to a native dedicated server.
	 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_MatchPhase, BlueprintReadOnly, Category = "CS|Match")
	ECSMatchPhase MatchPhase = ECSMatchPhase::WaitingForPlayers;

	/** Network time (not local time) at which the current phase expires. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "CS|Match")
	double PhaseEndNetworkTime = 0.0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "CS|Match")
	int32 ScoreAlpha = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "CS|Match")
	int32 ScoreBravo = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "CS|Bots")
	int32 BotCount = -1;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "CS|Bots")
	ECSBotDifficulty BotDifficulty = ECSBotDifficulty::Normal;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "CS|Mode")
	ECSGameModeType GameModeType = ECSGameModeType::Deathmatch;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "CS|Mode")
	bool bModeConfigured = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "CS|Mode")
	int32 RoundNumber = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "CS|Mode")
	double BuyEndNetworkTime = 0.0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "CS|Mode")
	ECSTeam WinnerTeam = ECSTeam::None;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "CS|Mode")
	int32 WinnerPlayerId = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "CS|Mode")
	int32 LossStreakAlpha = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "CS|Mode")
	int32 LossStreakBravo = 0;

	/** v2.0: backend match id (a UUID); empty when the match is not recorded. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "CS|Mode")
	FString BackendMatchId;

	UFUNCTION()
	void OnRep_MatchPhase();
};
