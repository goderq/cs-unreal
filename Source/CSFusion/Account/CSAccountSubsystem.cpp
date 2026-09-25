// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Account/CSAccountSubsystem.h"

#include "Account/CSBackendConfig.h"
#include "Core/CSLog.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OnlineSubsystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	const FName EOSSubsystemName(TEXT("EOS"));

	/** The backend token is renewed when this share of its lifetime has passed. */
	constexpr double RenewAtFraction = 0.75;

	IOnlineIdentityPtr GetEpicIdentity()
	{
		IOnlineSubsystem* OSS = IOnlineSubsystem::Get(EOSSubsystemName);
		return OSS ? OSS->GetIdentityInterface() : nullptr;
	}

	TSharedPtr<FJsonObject> ParseJson(const FString& Body)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
		return FJsonSerializer::Deserialize(Reader, Object) ? Object : nullptr;
	}

	FString ErrorFromJson(const TSharedPtr<FJsonObject>& Json, const FString& Fallback)
	{
		FString Message;
		if (Json.IsValid() && (Json->TryGetStringField(TEXT("error"), Message) || Json->TryGetStringField(TEXT("message"), Message)))
		{
			return Message;
		}
		return Fallback;
	}

	FString ToJson(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	void ReadStats(const TSharedPtr<FJsonObject>& Json, FCSAccountStats& Out)
	{
		if (!Json.IsValid())
		{
			return;
		}
		Json->TryGetNumberField(TEXT("matches"), Out.Matches);
		Json->TryGetNumberField(TEXT("wins"), Out.Wins);
		Json->TryGetNumberField(TEXT("kills"), Out.Kills);
		Json->TryGetNumberField(TEXT("deaths"), Out.Deaths);
		Json->TryGetNumberField(TEXT("headshots"), Out.Headshots);
		Json->TryGetNumberField(TEXT("playtime_seconds"), Out.PlaytimeSeconds);
		Json->TryGetNumberField(TEXT("practice_matches"), Out.PracticeMatches);
		Json->TryGetNumberField(TEXT("practice_kills"), Out.PracticeKills);
		Json->TryGetNumberField(TEXT("practice_deaths"), Out.PracticeDeaths);
	}
}

UCSAccountSubsystem* UCSAccountSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UCSAccountSubsystem>() : nullptr;
}

void UCSAccountSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UCSAccountSubsystem::TickSession), 15.f);

	const FCSBackendConfig& Config = FCSBackendConfig::Get();
	if (!Config.HasEOS() || !Config.HasSupabase())
	{
		State = ECSAccountState::NotConfigured;
		LastError = TEXT("Config/Backend.ini is missing or incomplete (see docs/ACCOUNTS.md).");
		UE_LOG(LogCSAuth, Warning, TEXT("Accounts: %s"), *LastError);
		return;
	}

	// Sign in as the game starts rather than waiting for the login screen: a
	// build launched straight into a map (a test, or -mode= on the command
	// line) would otherwise play the whole match signed out. The attempt is
	// silent: the saved Epic session, no window.
	if (IsSignInRequired())
	{
		TrySilentSignIn();
	}
}

void UCSAccountSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickerHandle);
	if (const IOnlineIdentityPtr Identity = GetEpicIdentity())
	{
		if (LoginHandle.IsValid())
		{
			Identity->ClearOnLoginCompleteDelegate_Handle(0, LoginHandle);
		}
		if (LogoutHandle.IsValid())
		{
			Identity->ClearOnLogoutCompleteDelegate_Handle(0, LogoutHandle);
		}
		if (LoginStatusHandle.IsValid())
		{
			Identity->ClearOnLoginStatusChangedDelegate_Handle(0, LoginStatusHandle);
		}
	}
	LoginHandle.Reset();
	LogoutHandle.Reset();
	LoginStatusHandle.Reset();
	Super::Deinitialize();
}

bool UCSAccountSubsystem::IsSignInRequired() const
{
	// Self-tests and offline debugging run without an account.
	return !FParse::Param(FCommandLine::Get(), TEXT("noaccount"));
}

FString UCSAccountSubsystem::DescribeBackend()
{
	const FCSBackendConfig& Config = FCSBackendConfig::Get();
	const IOnlineSubsystem* OSS = IOnlineSubsystem::Get(EOSSubsystemName);
	const IOnlineIdentityPtr Identity = GetEpicIdentity();
	return FString::Printf(
		TEXT("EOS subsystem %s, identity interface %s, keys %s (product %s); Supabase %s"),
		OSS ? TEXT("up") : TEXT("MISSING"),
		Identity.IsValid() ? TEXT("up") : TEXT("MISSING"),
		Config.HasEOS() ? TEXT("set") : TEXT("MISSING"),
		Config.ProductId.IsEmpty() ? TEXT("-") : *Config.ProductId,
		Config.HasSupabase() ? TEXT("set") : TEXT("MISSING"));
}

void UCSAccountSubsystem::SetState(ECSAccountState NewState)
{
	if (State != NewState)
	{
		State = NewState;
		OnAccountChanged.Broadcast();
	}
}

void UCSAccountSubsystem::Fail(const FString& Error)
{
	LastError = Error;
	UE_LOG(LogCSAuth, Warning, TEXT("Accounts: sign-in failed - %s"), *Error);
	SetState(ECSAccountState::Failed);
}

void UCSAccountSubsystem::ClearSession()
{
	AccessToken.Reset();
	ProfileId.Reset();
	Nickname.Reset();
	Role.Reset();
	Stats = FCSAccountStats();
	TokenExpiresAt = 0.0;
	TokenRenewAt = 0.0;
}

// ---------------------------------------------------------------------------
// Epic sign-in
// ---------------------------------------------------------------------------

void UCSAccountSubsystem::TrySilentSignIn()
{
	if (State == ECSAccountState::SigningIn || State == ECSAccountState::CheckingSession || State == ECSAccountState::Ready)
	{
		return;
	}
	const FCSBackendConfig& Config = FCSBackendConfig::Get();
	if (!Config.HasEOS() || !Config.HasSupabase())
	{
		SetState(ECSAccountState::NotConfigured);
		return;
	}
	LastError.Reset();
	bSilentAttempt = true;
	SetState(ECSAccountState::CheckingSession);
	BeginEpicLogin(TEXT("persistentauth"));
}

void UCSAccountSubsystem::SignIn()
{
	if (State == ECSAccountState::SigningIn || State == ECSAccountState::CheckingSession || State == ECSAccountState::Ready)
	{
		return;
	}
	const FCSBackendConfig& Config = FCSBackendConfig::Get();
	if (!Config.HasEOS() || !Config.HasSupabase())
	{
		SetState(ECSAccountState::NotConfigured);
		return;
	}
	LastError.Reset();
	bSilentAttempt = false;
	SetState(ECSAccountState::SigningIn);
	// "accountportal" is the Epic-hosted login: the overlay if it is available,
	// otherwise the browser. The game never sees the password.
	BeginEpicLogin(TEXT("accountportal"));
}

void UCSAccountSubsystem::BeginEpicLogin(const TCHAR* CredentialType)
{
	const IOnlineIdentityPtr Identity = GetEpicIdentity();
	if (!Identity.IsValid())
	{
		bSilentAttempt = false;
		Fail(TEXT("Epic Online Services is unavailable (plugin or configuration missing)."));
		return;
	}

	if (!LoginStatusHandle.IsValid())
	{
		LoginStatusHandle = Identity->AddOnLoginStatusChangedDelegate_Handle(0,
			FOnLoginStatusChangedDelegate::CreateUObject(this, &UCSAccountSubsystem::HandleLoginStatusChanged));
	}

	// Already signed in to Epic this session (the menu was visited before).
	if (Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn)
	{
		HandleEpicLogin(0, true, *Identity->GetUniquePlayerId(0), FString());
		return;
	}

	if (LoginHandle.IsValid())
	{
		Identity->ClearOnLoginCompleteDelegate_Handle(0, LoginHandle);
	}
	LoginHandle = Identity->AddOnLoginCompleteDelegate_Handle(0,
		FOnLoginCompleteDelegate::CreateUObject(this, &UCSAccountSubsystem::HandleEpicLogin));

	UE_LOG(LogCSAuth, Log, TEXT("Accounts: Epic sign-in via %s."), CredentialType);
	Identity->Login(0, FOnlineAccountCredentials(CredentialType, FString(), FString()));
}

void UCSAccountSubsystem::HandleEpicLogin(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error)
{
	const IOnlineIdentityPtr Identity = GetEpicIdentity();
	if (Identity.IsValid() && LoginHandle.IsValid())
	{
		Identity->ClearOnLoginCompleteDelegate_Handle(0, LoginHandle);
		LoginHandle.Reset();
	}

	const bool bWasSilent = bSilentAttempt;
	bSilentAttempt = false;
	const bool bRenewal = bRenewing;

	if (!bWasSuccessful)
	{
		if (bRenewal)
		{
			UE_LOG(LogCSAuth, Warning, TEXT("Accounts: the saved Epic session could not renew the login (%s)."), *Error);
			FinishRenewal(false);
			return;
		}
		if (bWasSilent)
		{
			// Not an error: there simply is no saved session (first launch, or
			// the player signed out). The screen shows the sign-in button.
			UE_LOG(LogCSAuth, Log, TEXT("Accounts: no saved Epic session (%s) - waiting for the player to sign in."), *Error);
			LastError.Reset();
			SetState(ECSAccountState::SignedOut);
			return;
		}
		Fail(Error.IsEmpty() ? TEXT("the Epic sign-in was cancelled") : Error);
		return;
	}

	EpicDisplayName = Identity.IsValid() ? Identity->GetPlayerNickname(0) : FString();
	const FString Token = ReadEpicToken();
	if (Token.IsEmpty())
	{
		if (bRenewal)
		{
			FinishRenewal(false);
			return;
		}
		Fail(TEXT("Epic signed in but returned no token."));
		return;
	}
	UE_LOG(LogCSAuth, Log, TEXT("Accounts: Epic sign-in OK (%s)."),
		bWasSilent ? TEXT("saved session, no window") : TEXT("account portal"));

	if (bRenewal)
	{
		ExchangeTokenForSession(Token, /*bRenewal*/ true);
		return;
	}
	SetState(ECSAccountState::SigningIn);
	ExchangeTokenForSession(Token, /*bRenewal*/ false);
}

void UCSAccountSubsystem::HandleLoginStatusChanged(int32 LocalUserNum, ELoginStatus::Type OldStatus, ELoginStatus::Type NewStatus, const FUniqueNetId& UserId)
{
	// The backend token stays valid until it expires; the next renewal tries
	// the saved Epic session, and only if that fails is the player asked again.
	UE_LOG(LogCSAuth, Log, TEXT("Accounts: Epic login status %s -> %s."),
		ELoginStatus::ToString(OldStatus), ELoginStatus::ToString(NewStatus));
}

FString UCSAccountSubsystem::ReadEpicToken() const
{
	const IOnlineIdentityPtr Identity = GetEpicIdentity();
	if (!Identity.IsValid())
	{
		return FString();
	}
	// The EOS SDK keeps this token fresh; each call copies the current one.
	FString Token = Identity->GetAuthToken(0);
	if (!Token.IsEmpty())
	{
		return Token;
	}
	if (const TSharedPtr<FUserOnlineAccount> Account = Identity->GetUserAccount(*Identity->GetUniquePlayerId(0)))
	{
		for (const TCHAR* Key : { TEXT("authToken"), TEXT("auth_token"), TEXT("access_token"), TEXT("id_token") })
		{
			if (Account->GetAuthAttribute(Key, Token) && !Token.IsEmpty())
			{
				return Token;
			}
		}
	}
	return FString();
}

// ---------------------------------------------------------------------------
// Backend session
// ---------------------------------------------------------------------------

TSharedRef<IHttpRequest, ESPMode::ThreadSafe> UCSAccountSubsystem::MakeRequest(const FString& Url, const FString& Verb, bool bAuthenticated) const
{
	const FCSBackendConfig& Config = FCSBackendConfig::Get();
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(Verb);
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("apikey"), Config.SupabaseAnonKey);
	Request->SetHeader(TEXT("Authorization"),
		FString::Printf(TEXT("Bearer %s"), bAuthenticated && !AccessToken.IsEmpty() ? *AccessToken : *Config.SupabaseAnonKey));
	Request->SetTimeout(20.f);
	return Request;
}

void UCSAccountSubsystem::ExchangeTokenForSession(const FString& EpicToken, bool bRenewal)
{
	const FCSBackendConfig& Config = FCSBackendConfig::Get();
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("epic_token"), EpicToken);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(Config.FunctionUrl(TEXT("eos-login")), TEXT("POST"), false);
	Request->SetContentAsString(ToJson(Body));
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, bRenewal](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			const int32 Code = (bConnected && Response.IsValid()) ? Response->GetResponseCode() : 0;
			const TSharedPtr<FJsonObject> Json = Response.IsValid() ? ParseJson(Response->GetContentAsString()) : nullptr;

			if (Code != 200)
			{
				FString Message = Code == 0
					? FString(TEXT("no connection to the account server"))
					: ErrorFromJson(Json, FString::Printf(TEXT("the account server answered %d"), Code));
				FString BannedUntil;
				if (Code == 403 && Json.IsValid() && Json->TryGetStringField(TEXT("banned_until"), BannedUntil))
				{
					Message = FString::Printf(TEXT("This account is banned until %s."), *BannedUntil.Left(16).Replace(TEXT("T"), TEXT(" ")));
				}
				if (bRenewal)
				{
					// A refused renewal (banned, account gone) ends the session now;
					// a network hiccup keeps it until the token really expires.
					if (Code == 401 || Code == 403)
					{
						UE_LOG(LogCSAuth, Warning, TEXT("Accounts: session renewal refused (%d) - signing out."), Code);
						ClearSession();
						LastError = Message;
						SetState(Code == 403 ? ECSAccountState::Failed : ECSAccountState::SignedOut);
					}
					FinishRenewal(false);
					return;
				}
				ClearSession();
				Fail(Message);
				return;
			}

			ApplySession(Json);
			if (ProfileId.IsEmpty() || AccessToken.IsEmpty())
			{
				ClearSession();
				if (bRenewal)
				{
					FinishRenewal(false);
					return;
				}
				Fail(TEXT("the account server sent an answer the game did not understand."));
				return;
			}

			if (bRenewal)
			{
				UE_LOG(LogCSAuth, Log, TEXT("Accounts: session renewed."));
				FinishRenewal(true);
				OnAccountChanged.Broadcast();
				return;
			}
			UE_LOG(LogCSAuth, Log, TEXT("Accounts: signed in as '%s' (%s, %d kills, %d matches)."),
				*Nickname, *Role, Stats.Kills, Stats.Matches);
			SetState(ECSAccountState::Ready);
		});
	Request->ProcessRequest();
}

void UCSAccountSubsystem::ApplySession(const TSharedPtr<FJsonObject>& Json)
{
	const TSharedPtr<FJsonObject>* Profile = nullptr;
	if (!Json.IsValid() || !Json->TryGetObjectField(TEXT("profile"), Profile))
	{
		return;
	}
	Json->TryGetStringField(TEXT("token"), AccessToken);
	(*Profile)->TryGetStringField(TEXT("id"), ProfileId);
	(*Profile)->TryGetStringField(TEXT("nickname"), Nickname);
	Role = TEXT("player");
	(*Profile)->TryGetStringField(TEXT("role"), Role);

	const TSharedPtr<FJsonObject>* StatsJson = nullptr;
	if (Json->TryGetObjectField(TEXT("stats"), StatsJson))
	{
		Stats = FCSAccountStats();
		ReadStats(*StatsJson, Stats);
	}

	int32 ExpiresIn = 0;
	Json->TryGetNumberField(TEXT("expires_in"), ExpiresIn);
	ExpiresIn = FMath::Max(120, ExpiresIn);
	const double Now = FPlatformTime::Seconds();
	TokenExpiresAt = Now + ExpiresIn;
	TokenRenewAt = Now + ExpiresIn * RenewAtFraction;
}

void UCSAccountSubsystem::RenewSession(TFunction<void(bool bOk)> OnDone)
{
	if (OnDone)
	{
		RenewWaiters.Add(MoveTemp(OnDone));
	}
	if (bRenewing)
	{
		return;
	}
	if (State != ECSAccountState::Ready)
	{
		FinishRenewal(false);
		return;
	}
	bRenewing = true;

	const IOnlineIdentityPtr Identity = GetEpicIdentity();
	if (Identity.IsValid() && Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn)
	{
		const FString Token = ReadEpicToken();
		if (!Token.IsEmpty())
		{
			ExchangeTokenForSession(Token, /*bRenewal*/ true);
			return;
		}
	}
	// The Epic session itself is gone: try the saved one, still silently.
	bSilentAttempt = true;
	BeginEpicLogin(TEXT("persistentauth"));
}

void UCSAccountSubsystem::FinishRenewal(bool bOk)
{
	bRenewing = false;
	if (!bOk)
	{
		// Try again in a minute; TickSession signs out once the token is really gone.
		TokenRenewAt = FPlatformTime::Seconds() + 60.0;
	}
	TArray<TFunction<void(bool)>> Waiters = MoveTemp(RenewWaiters);
	RenewWaiters.Reset();
	for (TFunction<void(bool)>& Waiter : Waiters)
	{
		Waiter(bOk);
	}
}

bool UCSAccountSubsystem::TickSession(float DeltaTime)
{
	if (State != ECSAccountState::Ready)
	{
		return true;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now >= TokenExpiresAt)
	{
		UE_LOG(LogCSAuth, Warning, TEXT("Accounts: the session expired and could not be renewed."));
		ClearSession();
		LastError = TEXT("Your session has expired. Sign in again.");
		SetState(ECSAccountState::SignedOut);
	}
	else if (Now >= TokenRenewAt && !bRenewing)
	{
		RenewSession(nullptr);
	}
	return true;
}

void UCSAccountSubsystem::PostAuthenticated(const FString& Url, const FString& Body, FResponseHandler OnDone, bool bIsRetry)
{
	if (AccessToken.IsEmpty())
	{
		OnDone(401, nullptr);
		return;
	}
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(Url, TEXT("POST"), true);
	Request->SetContentAsString(Body);
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, Url, Body, OnDone, bIsRetry](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			const int32 Code = (bConnected && Response.IsValid()) ? Response->GetResponseCode() : 0;
			const TSharedPtr<FJsonObject> Json = Response.IsValid() ? ParseJson(Response->GetContentAsString()) : nullptr;
			if (Code == 401 && !bIsRetry && State == ECSAccountState::Ready)
			{
				// The token ran out mid-request: renew once and try again.
				RenewSession([this, Url, Body, OnDone](bool bOk)
				{
					if (bOk)
					{
						PostAuthenticated(Url, Body, OnDone, /*bIsRetry*/ true);
					}
					else
					{
						OnDone(401, nullptr);
					}
				});
				return;
			}
			OnDone(Code, Json);
		});
	Request->ProcessRequest();
}

// ---------------------------------------------------------------------------
// Sign out, switch account, offline
// ---------------------------------------------------------------------------

void UCSAccountSubsystem::SignOut()
{
	ClearSession();
	LastError.Reset();
	UE_LOG(LogCSAuth, Log, TEXT("Accounts: signing out."));

	const IOnlineIdentityPtr Identity = GetEpicIdentity();
	if (Identity.IsValid() && Identity->GetLoginStatus(0) != ELoginStatus::NotLoggedIn)
	{
		if (LogoutHandle.IsValid())
		{
			Identity->ClearOnLogoutCompleteDelegate_Handle(0, LogoutHandle);
		}
		LogoutHandle = Identity->AddOnLogoutCompleteDelegate_Handle(0,
			FOnLogoutCompleteDelegate::CreateUObject(this, &UCSAccountSubsystem::HandleEpicLogout));
		SetState(ECSAccountState::SignedOut);
		// EOS Logout also deletes the saved session, so the next launch asks
		// for an account instead of signing the old one back in.
		Identity->Logout(0);
		return;
	}

	SetState(ECSAccountState::SignedOut);
	if (bSignInAfterLogout)
	{
		bSignInAfterLogout = false;
		SignIn();
	}
}

void UCSAccountSubsystem::HandleEpicLogout(int32 LocalUserNum, bool bWasSuccessful)
{
	if (const IOnlineIdentityPtr Identity = GetEpicIdentity(); Identity.IsValid() && LogoutHandle.IsValid())
	{
		Identity->ClearOnLogoutCompleteDelegate_Handle(0, LogoutHandle);
	}
	LogoutHandle.Reset();
	UE_LOG(LogCSAuth, Log, TEXT("Accounts: Epic sign-out %s."), bWasSuccessful ? TEXT("done") : TEXT("failed"));
	if (bSignInAfterLogout)
	{
		bSignInAfterLogout = false;
		SignIn();
	}
}

void UCSAccountSubsystem::SwitchAccount()
{
	bSignInAfterLogout = true;
	SignOut();
}

void UCSAccountSubsystem::PlayOffline()
{
	ClearSession();
	LastError.Reset();
	UE_LOG(LogCSAuth, Log, TEXT("Accounts: playing offline (practice only, nothing is recorded)."));
	SetState(ECSAccountState::Offline);
}

// ---------------------------------------------------------------------------
// Backend calls
// ---------------------------------------------------------------------------

void UCSAccountSubsystem::AdminCall(const FString& Action, const TSharedRef<FJsonObject>& Params,
	TFunction<void(bool, int32, const TSharedPtr<FJsonObject>&)> OnDone)
{
	if (!IsStaff())
	{
		OnDone(false, 403, nullptr);
		return;
	}
	// The admin function checks the role again against the database; the
	// role here only decides whether the panel is shown.
	Params->SetStringField(TEXT("action"), Action);
	PostAuthenticated(FCSBackendConfig::Get().FunctionUrl(TEXT("admin")), ToJson(Params),
		[OnDone, Action](int32 Code, const TSharedPtr<FJsonObject>& Json)
		{
			UE_LOG(LogCSAdmin, Log, TEXT("Admin: %s -> %d"), *Action, Code);
			OnDone(Code == 200, Code, Json);
		});
}

void UCSAccountSubsystem::FetchLeaderboard(TFunction<void(bool, const TArray<TSharedPtr<FJsonObject>>&)> OnDone)
{
	const FCSBackendConfig& Config = FCSBackendConfig::Get();
	if (!Config.HasSupabase())
	{
		OnDone(false, {});
		return;
	}
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request =
		MakeRequest(Config.RestUrl(TEXT("leaderboard?select=*&limit=20")), TEXT("GET"), IsReady());
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[OnDone](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			TArray<TSharedPtr<FJsonObject>> Rows;
			if (!bConnected || !Response.IsValid() || Response->GetResponseCode() != 200)
			{
				OnDone(false, Rows);
				return;
			}
			TArray<TSharedPtr<FJsonValue>> Values;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
			if (FJsonSerializer::Deserialize(Reader, Values))
			{
				for (const TSharedPtr<FJsonValue>& Value : Values)
				{
					if (const TSharedPtr<FJsonObject> Object = Value->AsObject())
					{
						Rows.Add(Object);
					}
				}
			}
			OnDone(true, Rows);
		});
	Request->ProcessRequest();
}

void UCSAccountSubsystem::MatchStart(const FString& Mode, const FString& Map, const FString& Room, bool bOffline,
	TFunction<void(bool, const FString&, const FString&)> OnDone)
{
	if (!IsReady())
	{
		OnDone(false, FString(), FString());
		return;
	}
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("action"), TEXT("start"));
	Body->SetStringField(TEXT("mode"), Mode);
	Body->SetStringField(TEXT("map"), Map);
	Body->SetStringField(TEXT("room"), Room);
	Body->SetBoolField(TEXT("offline"), bOffline);
	PostAuthenticated(FCSBackendConfig::Get().FunctionUrl(TEXT("match")), ToJson(Body),
		[OnDone](int32 Code, const TSharedPtr<FJsonObject>& Json)
		{
			const TSharedPtr<FJsonObject>* Result = nullptr;
			FString MatchId;
			FString Ticket;
			if (Code == 200 && Json.IsValid() && Json->TryGetObjectField(TEXT("result"), Result))
			{
				(*Result)->TryGetStringField(TEXT("match_id"), MatchId);
				(*Result)->TryGetStringField(TEXT("ticket"), Ticket);
			}
			if (MatchId.IsEmpty())
			{
				UE_LOG(LogCS, Warning, TEXT("Accounts: the match was not registered (%d: %s)."), Code, *ErrorFromJson(Json, TEXT("no answer")));
				OnDone(false, FString(), FString());
				return;
			}
			UE_LOG(LogCS, Log, TEXT("Accounts: match %s registered."), *MatchId);
			OnDone(true, MatchId, Ticket);
		});
}

void UCSAccountSubsystem::MatchTicket(const FString& MatchId, TFunction<void(bool, const FString&)> OnDone)
{
	if (!IsReady() || MatchId.IsEmpty())
	{
		OnDone(false, FString());
		return;
	}
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("action"), TEXT("ticket"));
	Body->SetStringField(TEXT("match_id"), MatchId);
	PostAuthenticated(FCSBackendConfig::Get().FunctionUrl(TEXT("match")), ToJson(Body),
		[OnDone](int32 Code, const TSharedPtr<FJsonObject>& Json)
		{
			FString Ticket;
			if (Code != 200 || !Json.IsValid() || !Json->TryGetStringField(TEXT("result"), Ticket) || Ticket.IsEmpty())
			{
				UE_LOG(LogCS, Warning, TEXT("Accounts: no match ticket (%d: %s)."), Code, *ErrorFromJson(Json, TEXT("no answer")));
				OnDone(false, FString());
				return;
			}
			OnDone(true, Ticket);
		});
}

void UCSAccountSubsystem::DebugCallFunction(const FString& Function, const TSharedRef<FJsonObject>& Body, FResponseHandler OnDone)
{
#if UE_BUILD_SHIPPING
	OnDone(0, nullptr);
#else
	PostAuthenticated(FCSBackendConfig::Get().FunctionUrl(*Function), ToJson(Body), MoveTemp(OnDone));
#endif
}

void UCSAccountSubsystem::MatchIncident(const FString& MatchId, const FString& Ticket, int32 PlayerNumber, const FString& Kind, const FString& Reason)
{
	if (!IsReady() || MatchId.IsEmpty())
	{
		return;
	}
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("action"), TEXT("incident"));
	Body->SetStringField(TEXT("match_id"), MatchId);
	Body->SetStringField(TEXT("kind"), Kind);
	if (!Ticket.IsEmpty())
	{
		Body->SetStringField(TEXT("ticket"), Ticket);
	}
	Body->SetNumberField(TEXT("player"), PlayerNumber);
	Body->SetStringField(TEXT("reason"), Reason.Left(200));
	PostAuthenticated(FCSBackendConfig::Get().FunctionUrl(TEXT("match")), ToJson(Body),
		[Kind, PlayerNumber](int32 Code, const TSharedPtr<FJsonObject>& Json)
		{
			if (Code == 200)
			{
				UE_LOG(LogCSSecurity, Log, TEXT("Accounts: incident '%s' for player %d is in the security log."), *Kind, PlayerNumber);
			}
			else
			{
				UE_LOG(LogCSSecurity, Warning, TEXT("Accounts: incident '%s' for player %d not logged (%d: %s)."), *Kind, PlayerNumber,
					Code, *ErrorFromJson(Json, TEXT("no answer")));
			}
		});
}

void UCSAccountSubsystem::MatchReport(const FString& MatchId, const TSharedRef<FJsonObject>& Report)
{
	if (!IsReady() || MatchId.IsEmpty())
	{
		return;
	}
	Report->SetStringField(TEXT("action"), TEXT("report"));
	Report->SetStringField(TEXT("match_id"), MatchId);
	PostAuthenticated(FCSBackendConfig::Get().FunctionUrl(TEXT("match")), ToJson(Report),
		[this](int32 Code, const TSharedPtr<FJsonObject>& Json)
		{
			const TSharedPtr<FJsonObject>* Result = nullptr;
			if (Code != 200 || !Json.IsValid() || !Json->TryGetObjectField(TEXT("result"), Result))
			{
				UE_LOG(LogCS, Warning, TEXT("Accounts: the match was not recorded (%d: %s)."), Code, *ErrorFromJson(Json, TEXT("no answer")));
				return;
			}
			bool bRanked = false;
			bool bSuspicious = false;
			int32 Counted = 0;
			(*Result)->TryGetBoolField(TEXT("ranked"), bRanked);
			(*Result)->TryGetBoolField(TEXT("suspicious"), bSuspicious);
			(*Result)->TryGetNumberField(TEXT("counted"), Counted);
			UE_LOG(LogCS, Log, TEXT("Accounts: match recorded for %d player(s) - %s%s."), Counted,
				bRanked ? TEXT("ranked") : TEXT("practice"), bSuspicious ? TEXT(", held for review") : TEXT(""));

			// Our own totals moved; pull them again for the menu.
			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Refresh = MakeRequest(FCSBackendConfig::Get().RestUrl(
				FString::Printf(TEXT("player_stats?profile_id=eq.%s&select=*"), *ProfileId)), TEXT("GET"), true);
			Refresh->OnProcessRequestComplete().BindWeakLambda(this,
				[this](FHttpRequestPtr, FHttpResponsePtr StatsResponse, bool bOk)
				{
					if (!bOk || !StatsResponse.IsValid() || StatsResponse->GetResponseCode() != 200)
					{
						return;
					}
					TArray<TSharedPtr<FJsonValue>> Values;
					const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(StatsResponse->GetContentAsString());
					if (FJsonSerializer::Deserialize(Reader, Values) && Values.Num() > 0)
					{
						Stats = FCSAccountStats();
						ReadStats(Values[0]->AsObject(), Stats);
						OnAccountChanged.Broadcast();
					}
				});
			Refresh->ProcessRequest();
		});
}
