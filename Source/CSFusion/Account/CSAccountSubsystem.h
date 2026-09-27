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
// v2.4: a second way in - email + password (Supabase Auth; the password goes
// only to Supabase, over HTTPS, and is never stored). Both ways end in the same
// login token and the same profile:
//   * Register: nickname + email + password. The nickname is set once.
//   * Recovery: a 6-digit code by email, typed into the game, then a new password.
//   * The email session is remembered (FCSSessionStore, DPAPI) and signs the
//     player in on the next launch; the last method used is tried first.
//   * One profile per player: a first Epic sign-in whose Epic account has no
//     profile yet asks (ChoosingProfile) whether to create one or to link Epic
//     to the player's email profile. From the profile page, an Epic player can
//     add an email and an email player can add Epic (functions/account-link).
//     An email or Epic account opens at most one profile; nothing is merged.
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
	Offline,
	/** v2.4: signed in to Epic, but that Epic account has no profile yet: create one, or link it to the email profile. */
	ChoosingProfile
};

UENUM(BlueprintType)
enum class ECSSignInMethod : uint8
{
	None,
	Epic,
	Email
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
	/** Result of an email / link operation: bOk, or the message to show. */
	using FAccountResult = TFunction<void(bool bOk, const FString& Message)>;

	static UCSAccountSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Silent sign-in with the saved Epic session; no window. Falls back to SignedOut. */
	void TrySilentSignIn();
	/** Interactive: the Epic account portal (overlay or browser). */
	void SignIn();
	/** Signs out of the game and of Epic (the saved Epic session is deleted). */
	void SignOut();
	/** Sign out; an Epic player then gets the account portal again, an email player the sign-in screen. */
	void SwitchAccount();
	/** Keep playing without an account: practice only, nothing is recorded. */
	void PlayOffline();

	/** v2.4: the launch sign-in - the remembered email session, else the saved Epic session. Silent. */
	void TryAutoSignIn();

	// --- v2.4 email + password (Supabase Auth) -------------------------------------

	/** Sign in with email + password. On failure the state goes back to SignedOut and OnDone gets the message. */
	void SignInWithEmail(const FString& Email, const FString& Password, FAccountResult OnDone);
	/** New account: the nickname is checked first, then the account is created and signed in. */
	void RegisterWithEmail(const FString& Email, const FString& Password, const FString& Nickname, FAccountResult OnDone);
	/** Sends the recovery letter (a 6-digit code with a custom template, else a link; the answer is the same whether the email is known or not). */
	void RequestPasswordReset(const FString& Email, FAccountResult OnDone);
	/** The code - or the whole reset link - from the email + a new password; signs the player in when it works. */
	void ResetPassword(const FString& Email, const FString& Code, const FString& NewPassword, FAccountResult OnDone);

	// --- v2.4 one profile, two ways in ---------------------------------------------

	/** ChoosingProfile: create a new profile for this Epic account. */
	void CreateProfileForEpic();
	/** ChoosingProfile: sign in with the email profile and link this Epic account to it. */
	void LinkEpicToEmailProfile(const FString& Email, const FString& Password, FAccountResult OnDone);
	/** ChoosingProfile: never mind (signs out of Epic). */
	void CancelProfileChoice();
	/** Signed in with Epic: add an email + password (a new email account, or an existing one without a profile). */
	void LinkEmail(const FString& Email, const FString& Password, FAccountResult OnDone);
	/** Signed in with email: add the Epic account (opens the Epic window when there is no saved Epic session). */
	void LinkEpic(FAccountResult OnDone);

	ECSSignInMethod GetSignInMethod() const { return SignInMethod; }
	bool HasEpicLinked() const { return bEpicLinked; }
	bool HasEmailLinked() const { return bEmailLinked; }
	const FString& GetEmailAddress() const { return EmailAddress; }
	/** The email of the remembered session, to pre-fill the form. */
	const FString& GetRememberedEmail() const { return RememberedEmail; }
	/** A one-off message for the profile page (e.g. the result of a link made during sign-in). */
	const FString& GetNotice() const { return Notice; }
	void ClearNotice() { Notice.Reset(); }
	/** Something (a sign-in, a link, a recovery) is waiting for the server. */
	bool IsBusy() const { return bEmailBusy || State == ECSAccountState::SigningIn || State == ECSAccountState::CheckingSession; }

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

	/** Photon custom authentication parameters ("token=..."): the current login token for photon-auth (B5). Empty when not signed in. */
	FString MakePhotonAuthParameters() const;
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
	/** Host: an anti-cheat measure (Kind "suspended" / "removed") against the holder of Ticket, for the security log. */
	void MatchIncident(const FString& MatchId, const FString& Ticket, int32 PlayerNumber, const FString& Kind, const FString& Reason);

	/** Fires whenever the state, the nickname or the stats change. */
	FCSAccountChanged OnAccountChanged;

	/**
	 * Self-tests only (-cstestmenu=backendprobe; compiled out of Shipping):
	 * POST to a backend function with this login, skipping every client-side
	 * gate - exactly what a modified client could do. The server must refuse
	 * whatever the account is not allowed.
	 */
	void DebugCallFunction(const FString& Function, const TSharedRef<FJsonObject>& Body, FResponseHandler OnDone);
	/** Self-tests only (CSAuthSelfTest): the Supabase Auth token of an email sign-in. Empty in Shipping. */
	FString DebugSupabaseAccessToken() const;

private:
	void BeginEpicLogin(const TCHAR* CredentialType);
	void HandleEpicLogin(int32 LocalUserNum, bool bWasSuccessful, const class FUniqueNetId& UserId, const FString& Error);
	void HandleEpicLogout(int32 LocalUserNum, bool bWasSuccessful);
	void HandleLoginStatusChanged(int32 LocalUserNum, ELoginStatus::Type OldStatus, ELoginStatus::Type NewStatus, const class FUniqueNetId& UserId);
	/** Reads the Epic token out of the identity interface (several possible names). */
	FString ReadEpicToken() const;
	/** eos-login. bRenewal keeps the state (Ready) and only swaps the token. bCreate: make a profile for an unknown Epic account. */
	void ExchangeTokenForSession(const FString& EpicToken, bool bRenewal, bool bCreate = true);

	// v2.4 email plumbing
	/** POST/PUT to Supabase Auth (/auth/v1/...). OnDone(Code, Json). */
	void AuthRequest(const FString& Path, const FString& Verb, const TSharedRef<FJsonObject>& Body, const FString& BearerToken, FResponseHandler OnDone);
	/** Keeps the Supabase session from an Auth answer (access/refresh token). False when it has none. */
	bool TakeAuthSession(const TSharedPtr<FJsonObject>& Json);
	/** email-login with the current Supabase access token. bRenewal keeps the state. */
	void ExchangeEmailSession(bool bRenewal, const FString& Nickname, FAccountResult OnDone);
	/** Refresh the Supabase session with the refresh token, then email-login. */
	void RefreshEmailSession(bool bRenewal, FAccountResult OnDone);
	void RememberSession();
	void ReadLinked(const TSharedPtr<FJsonObject>& Json);
	/** account-link with the current login token. */
	void CallAccountLink(const TSharedRef<FJsonObject>& Body, FAccountResult OnDone);
	/** A failed email step during sign-in: back to the form, with the message. */
	void FailEmail(const FString& Message, FAccountResult& OnDone);
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

	// v2.4
	ECSSignInMethod SignInMethod = ECSSignInMethod::None;
	bool bEpicLinked = false;
	bool bEmailLinked = false;
	FString EmailAddress;
	FString RememberedEmail;
	FString Notice;
	/** The Supabase Auth session of an email sign-in (never logged). */
	FString SupabaseAccessToken;
	FString SupabaseRefreshToken;
	bool bEmailBusy = false;
	/** The Epic login in flight only links the Epic account to the signed-in profile. */
	FAccountResult PendingEpicLink;

	/** FPlatformTime::Seconds() when the backend token expires / should be renewed. */
	double TokenExpiresAt = 0.0;
	double TokenRenewAt = 0.0;
	bool bRenewing = false;
	TArray<TFunction<void(bool)>> RenewWaiters;
};
