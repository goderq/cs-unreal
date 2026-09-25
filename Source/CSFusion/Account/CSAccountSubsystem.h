// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// The player's account: sign in with Epic (EOS), then exchange that for a
// short-lived Supabase token that the game uses for the profile and stats.
//
// Flow:
//   1. EOS login (Epic Account Services) through the OnlineSubsystemEOS
//      identity interface - the Epic overlay or browser does the actual login,
//      the game never sees a password.
//   2. The EOS access token goes to the eos-login Edge Function, which asks
//      Epic whether it is real, creates the profile on first sign-in and
//      returns { token, profile, stats }.
//   3. That token is what every later request carries. It expires in hours,
//      and row level security decides what it may touch - the game holds no
//      service key, so a modified client cannot write other people's rows.
//
// Nothing here is authoritative for gameplay: the account only decides who
// you are, not what happens in the match.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CSAccountSubsystem.generated.h"

class IHttpRequest;

UENUM(BlueprintType)
enum class ECSAccountState : uint8
{
	/** Nothing tried yet. */
	SignedOut,
	/** No Config/Backend.ini: the build cannot sign anybody in. */
	NotConfigured,
	SigningIn,
	Ready,
	Failed
};

USTRUCT(BlueprintType)
struct CSFUSION_API FCSAccountStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CS|Account") int32 Matches = 0;
	UPROPERTY(BlueprintReadOnly, Category = "CS|Account") int32 Wins = 0;
	UPROPERTY(BlueprintReadOnly, Category = "CS|Account") int32 Kills = 0;
	UPROPERTY(BlueprintReadOnly, Category = "CS|Account") int32 Deaths = 0;
	UPROPERTY(BlueprintReadOnly, Category = "CS|Account") int32 Headshots = 0;
	UPROPERTY(BlueprintReadOnly, Category = "CS|Account") int32 PlaytimeSeconds = 0;

	float KD() const { return Deaths > 0 ? static_cast<float>(Kills) / Deaths : static_cast<float>(Kills); }
};

/** One participant of a finished match, as reported by the authority. */
struct FCSMatchReportPlayer
{
	FString ProfileId;
	int32 Team = 0;
	int32 Kills = 0;
	int32 Deaths = 0;
	int32 Headshots = 0;
	int32 Damage = 0;
	int32 Money = 0;
	bool bWon = false;
};

DECLARE_MULTICAST_DELEGATE(FCSAccountChanged);

UCLASS()
class CSFUSION_API UCSAccountSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UCSAccountSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Epic login, then the backend handshake. Safe to call again while it runs. */
	void SignIn();
	void SignOut();

	ECSAccountState GetState() const { return State; }
	bool IsReady() const { return State == ECSAccountState::Ready; }

	/** Signing in is required to play unless -noaccount is on the command line. */
	bool IsSignInRequired() const;

	/**
	 * One line for the log: did the EOS plugin come up with our credentials,
	 * and is the account backend configured. Touching it starts the EOS
	 * platform, so it is a real check of the Epic ids - not just of the ini.
	 */
	static FString DescribeBackend();

	const FString& GetNickname() const { return Nickname; }
	const FString& GetProfileId() const { return ProfileId; }
	const FString& GetEpicDisplayName() const { return EpicDisplayName; }
	const FString& GetLastError() const { return LastError; }
	const FCSAccountStats& GetStats() const { return Stats; }

	/** v2.0: the profile has admin rights (decides whether the admin panel shows). */
	bool IsAdmin() const { return IsReady() && bIsAdmin; }

	/**
	 * v2.0 admin panel: one call to the admin Edge Function. Params gets
	 * "action" added. OnDone(bOk, HttpCode, ResponseJson) - the server
	 * re-checks is_admin, so a modified client gains nothing from this.
	 */
	void AdminCall(const FString& Action, const TSharedRef<FJsonObject>& Params,
		TFunction<void(bool bOk, int32 Code, const TSharedPtr<FJsonObject>& Response)> OnDone);

	/** Top players by kills, for the menu. */
	void FetchLeaderboard(TFunction<void(bool bOk, const TArray<TSharedPtr<FJsonObject>>& Rows)> OnDone);

	/**
	 * Authority only: hand a finished match to the backend. Players without an
	 * account (and bots) are simply left out.
	 */
	void ReportMatch(const FString& Mode, const FString& Map, const FString& Room,
		const FDateTime& StartedAtUtc, int32 WinnerTeam, int32 Rounds,
		const TArray<FCSMatchReportPlayer>& Players);

	/** Fires whenever the state, the nickname or the stats change. */
	FCSAccountChanged OnAccountChanged;

private:
	void BeginEpicLogin();
	void HandleEpicLogin(int32 LocalUserNum, bool bWasSuccessful, const class FUniqueNetId& UserId, const FString& Error);
	/** Reads the Epic token out of the identity interface (several possible names). */
	FString ReadEpicToken() const;
	void ExchangeTokenForSession(const FString& EpicToken);
	void Fail(const FString& Error);
	void SetState(ECSAccountState NewState);

	/** Request with the Supabase headers already set (anon key, and our token when signed in). */
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> MakeRequest(const FString& Url, const FString& Verb, bool bAuthenticated) const;

	ECSAccountState State = ECSAccountState::SignedOut;
	FString Nickname;
	FString ProfileId;
	FString EpicDisplayName;
	FString AccessToken;
	FString LastError;
	FCSAccountStats Stats;
	bool bIsAdmin = false;
	FDelegateHandle LoginHandle;
	double TokenExpiresAt = 0.0;
};
