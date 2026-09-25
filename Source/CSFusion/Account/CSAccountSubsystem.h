// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// The player's account: sign in with Epic (EOS), then exchange that for a
// short-lived login token for the CS-Fusion backend (Supabase).
//
// Sign-in (v2.0, docs/AUDIT.md B2-B4):
//   * Every launch first tries the saved Epic session silently: EOS persistent
//     auth, no window (CheckingSession). The refresh token is kept by the EOS
//     SDK in the system credential store; this game never sees or stores it.
//   * Only when that fails does the player get the [SIGN IN WITH EPIC] button,
//     which opens the Epic account portal (overlay or browser). The game never
//     sees a password and has no login form of its own.
//   * The backend token (eos-login, 2 h) is renewed silently before it runs
//     out, with the Epic token the SDK keeps fresh. A request answered 401 is
//     retried once after a renewal.
//   * Sign out forgets the Epic session too (EOS Logout deletes the persistent
//     auth), so "switch account" really asks for another account.
//   * No connection: the player can keep playing offline practice, without
//     stats, and retry later.
//
// Nothing here is authoritative for gameplay or for administration: the role
// only decides which pages the menu shows. Every admin action is checked again
// by the backend against the database (functions/admin).

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "OnlineSubsystemTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CSAccountSubsystem.generated.h"

class FJsonObject;
class IHttpRequest;
class IHttpResponse;

UENUM(BlueprintType)
enum class ECSAccountState : uint8
{
	/** Waiting for the player to press [SIGN IN WITH EPIC]. */
	SignedOut,
	/** No Config/Backend.ini: the build cannot sign anybody in. */
	NotConfigured,
	/** The Epic window is open, or the backend is being asked. */
	SigningIn,
	Ready,
	/** Something went wrong (network, Epic, the server); retry or play offline. */
	Failed,
	/** v2.0: trying the saved Epic session, silently. */
	CheckingSession,
	/** v2.0: the player chose to play offline (practice only, no stats). */
	Offline
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

	/** v2.0: offline and bot matches are counted here, not in the leaderboard. */
	UPROPERTY(BlueprintReadOnly, Category = "CS|Account") int32 PracticeMatches = 0;
	UPROPERTY(BlueprintReadOnly, Category = "CS|Account") int32 PracticeKills = 0;
	UPROPERTY(BlueprintReadOnly, Category = "CS|Account") int32 PracticeDeaths = 0;

	float KD() const { return Deaths > 0 ? static_cast<float>(Kills) / Deaths : static_cast<float>(Kills); }
};

DECLARE_MULTICAST_DELEGATE(FCSAccountChanged);

UCLASS()
class CSFUSION_API UCSAccountSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Code, parsed body ({ "result": ... } or { "error": ... }; may be null). */
	using FResponseHandler = TFunction<void(int32 Code, const TSharedPtr<FJsonObject>& Json)>;

	static UCSAccountSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Silent sign-in with the saved Epic session; no window. Falls back to SignedOut. */
	void TrySilentSignIn();
	/** Interactive: the Epic account portal (overlay or browser). */
	void SignIn();
	/** Signs out of the game and of Epic (the saved Epic session is deleted). */
	void SignOut();
	/** Sign out, then sign in again with the account portal. */
	void SwitchAccount();
	/** Keep playing without an account: practice only, nothing is recorded. */
	void PlayOffline();

	ECSAccountState GetState() const { return State; }
	bool IsReady() const { return State == ECSAccountState::Ready; }
	bool IsOffline() const { return State == ECSAccountState::Offline; }

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

	/** player | moderator | admin | superadmin, as the backend said at sign-in. */
	const FString& GetRole() const { return Role; }
	/** Moderator or above: the ADMIN page is shown. The backend checks every action anyway. */
	bool IsStaff() const { return IsReady() && !Role.IsEmpty() && Role != TEXT("player"); }
	bool IsAdmin() const { return IsReady() && (Role == TEXT("admin") || Role == TEXT("superadmin")); }

	/**
	 * One call to the admin Edge Function. Params gets "action" added. The
	 * backend re-checks the role against the database on every call.
	 */
	void AdminCall(const FString& Action, const TSharedRef<FJsonObject>& Params,
		TFunction<void(bool bOk, int32 Code, const TSharedPtr<FJsonObject>& Response)> OnDone);

	/** Top players by kills (ranked matches only), for the menu. */
	void FetchLeaderboard(TFunction<void(bool bOk, const TArray<TSharedPtr<FJsonObject>>& Rows)> OnDone);

	// --- v2.0 match records (functions/match) -------------------------------------

	/** Host: registers the match; the server stamps the start. Returns its id and the host's ticket. */
	void MatchStart(const FString& Mode, const FString& Map, const FString& Room, bool bOffline,
		TFunction<void(bool bOk, const FString& MatchId, const FString& Ticket)> OnDone);
	/** Any signed-in player: this player's own participation ticket for the match. */
	void MatchTicket(const FString& MatchId, TFunction<void(bool bOk, const FString& Ticket)> OnDone);
	/** Host: the result, per ticket. The server checks and counts it. */
	void MatchReport(const FString& MatchId, const TSharedRef<FJsonObject>& Report);

	/** Fires whenever the state, the nickname or the stats change. */
	FCSAccountChanged OnAccountChanged;

	/**
	 * Self-tests only (-cstestmenu=backendprobe; compiled out of Shipping):
	 * POST to a backend function with this login, skipping every client-side
	 * gate - exactly what a modified client could do. The server must refuse
	 * whatever the account is not allowed.
	 */
	void DebugCallFunction(const FString& Function, const TSharedRef<FJsonObject>& Body, FResponseHandler OnDone);

private:
	void BeginEpicLogin(const TCHAR* CredentialType);
	void HandleEpicLogin(int32 LocalUserNum, bool bWasSuccessful, const class FUniqueNetId& UserId, const FString& Error);
	void HandleEpicLogout(int32 LocalUserNum, bool bWasSuccessful);
	void HandleLoginStatusChanged(int32 LocalUserNum, ELoginStatus::Type OldStatus, ELoginStatus::Type NewStatus, const class FUniqueNetId& UserId);
	/** Reads the Epic token out of the identity interface (several possible names). */
	FString ReadEpicToken() const;
	/** eos-login. bRenewal keeps the state (Ready) and only swaps the token. */
	void ExchangeTokenForSession(const FString& EpicToken, bool bRenewal);
	void ApplySession(const TSharedPtr<FJsonObject>& Json);
	void ClearSession();
	void Fail(const FString& Error);
	void SetState(ECSAccountState NewState);

	/** Renews the backend token (silently); OnDone(bOk) when finished. Calls queue up. */
	void RenewSession(TFunction<void(bool bOk)> OnDone);
	void FinishRenewal(bool bOk);
	bool TickSession(float DeltaTime);

	/** Request with the Supabase headers already set (anon key, and our token when signed in). */
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> MakeRequest(const FString& Url, const FString& Verb, bool bAuthenticated) const;
	/** POST with our login token; a 401 is retried once after renewing the session. */
	void PostAuthenticated(const FString& Url, const FString& Body, FResponseHandler OnDone, bool bIsRetry = false);

	ECSAccountState State = ECSAccountState::SignedOut;
	FString Nickname;
	FString ProfileId;
	FString Role;
	FString EpicDisplayName;
	FString AccessToken;
	FString LastError;
	FCSAccountStats Stats;
	FDelegateHandle LoginHandle;
	FDelegateHandle LogoutHandle;
	FDelegateHandle LoginStatusHandle;
	FTSTicker::FDelegateHandle TickerHandle;

	/** The attempt in flight is the silent one (persistent auth): failing is not an error. */
	bool bSilentAttempt = false;
	/** Sign in with the account portal once the running logout finishes. */
	bool bSignInAfterLogout = false;

	/** FPlatformTime::Seconds() when the backend token expires / should be renewed. */
	double TokenExpiresAt = 0.0;
	double TokenRenewAt = 0.0;
	bool bRenewing = false;
	TArray<TFunction<void(bool)>> RenewWaiters;
};
