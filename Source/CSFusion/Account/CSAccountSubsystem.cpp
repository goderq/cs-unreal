// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Account/CSAccountSubsystem.h"

#include "Account/CSBackendConfig.h"
#include "Account/CSAuthSelfTest.h"
#include "Account/CSSessionStore.h"
#include "Core/CSLog.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "GenericPlatform/GenericPlatformHttp.h"
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

	/** Supabase Auth and our functions answer with codes; the player reads sentences. */
	FString FriendlyError(int32 Code, const TSharedPtr<FJsonObject>& Json)
	{
		if (Code == 0)
		{
			return TEXT("No connection to the account server. Check the internet and try again.");
		}
		FString Key;
		FString Text;
		if (Json.IsValid())
		{
			Json->TryGetStringField(TEXT("error_code"), Key);
			if (!Json->TryGetStringField(TEXT("msg"), Text) && !Json->TryGetStringField(TEXT("error_description"), Text))
			{
				Json->TryGetStringField(TEXT("message"), Text);
			}
			FString Error;
			if (Json->TryGetStringField(TEXT("error"), Error))
			{
				if (Key.IsEmpty())
				{
					Key = Error;
				}
				if (Text.IsEmpty())
				{
					Text = Error;
				}
			}
		}
		static const TMap<FString, FString> Known = {
			{ TEXT("invalid_credentials"), TEXT("Wrong email or password.") },
			{ TEXT("invalid_grant"), TEXT("Wrong email or password.") },
			{ TEXT("user_already_exists"), TEXT("An account with this email already exists. Sign in, or reset the password.") },
			{ TEXT("email_exists"), TEXT("An account with this email already exists. Sign in, or reset the password.") },
			{ TEXT("weak_password"), TEXT("The password is too weak: at least 8 characters.") },
			{ TEXT("email_address_invalid"), TEXT("Check the email address.") },
			{ TEXT("validation_failed"), TEXT("Check the email address and the password.") },
			{ TEXT("otp_expired"), TEXT("The code is wrong or has expired. Request a new one.") },
			{ TEXT("over_email_send_rate_limit"), TEXT("Too many emails were sent. Wait a few minutes and try again.") },
			{ TEXT("over_request_rate_limit"), TEXT("Too many attempts. Wait a few minutes.") },
			{ TEXT("email_not_confirmed"), TEXT("Confirm your email first (see the letter we sent).") },
			{ TEXT("signup_disabled"), TEXT("Registration is closed right now.") },
			{ TEXT("same_password"), TEXT("The new password must differ from the old one.") },
			{ TEXT("nickname_required"), TEXT("Choose a nickname.") },
			{ TEXT("nickname_length"), TEXT("The nickname must be 3 to 20 characters.") },
			{ TEXT("nickname_chars"), TEXT("The nickname may contain letters, digits, _ and - (single spaces between words).") },
			{ TEXT("nickname_reserved"), TEXT("This nickname is reserved. Choose another.") },
			{ TEXT("nickname_taken"), TEXT("This nickname is taken. Choose another.") },
			{ TEXT("taken"), TEXT("This account already belongs to another profile. It cannot be linked to this one.") },
			{ TEXT("already_linked"), TEXT("This profile already has one linked.") },
		};
		if (const FString* Friendly = Known.Find(Key))
		{
			return *Friendly;
		}
		if (Text.Contains(TEXT("Invalid login credentials")))
		{
			return TEXT("Wrong email or password.");
		}
		if (Text.Contains(TEXT("expired or is invalid")))
		{
			return TEXT("The code is wrong or has expired. Request a new one.");
		}
		if (Code == 429)
		{
			return TEXT("Too many attempts. Wait a few minutes.");
		}
		if (Code == 403 && Key == TEXT("banned"))
		{
			FString Until;
			if (Json.IsValid() && Json->TryGetStringField(TEXT("banned_until"), Until))
			{
				return FString::Printf(TEXT("This account is banned until %s."), *Until.Left(16).Replace(TEXT("T"), TEXT(" ")));
			}
		}
		return Text.IsEmpty() ? FString::Printf(TEXT("The account server answered %d."), Code) : Text;
	}

	bool LooksLikeEmail(const FString& Email)
	{
		int32 At = INDEX_NONE;
		return Email.Len() <= 254 && Email.FindChar(TEXT('@'), At) && At > 0
			&& Email.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd) > At + 1
			&& !Email.Contains(TEXT(" "));
	}

	constexpr int32 MinPasswordLength = 8;

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
		TryAutoSignIn();
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
	return UE_BUILD_SHIPPING || !FParse::Param(FCommandLine::Get(), TEXT("noaccount")); // never skipped in Shipping (B12)
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
	SignInMethod = ECSSignInMethod::None;
	bEpicLinked = false;
	bEmailLinked = false;
	EmailAddress.Reset();
	SupabaseAccessToken.Reset();
	SupabaseRefreshToken.Reset();
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

	// v2.4: this login only links the Epic account to the signed-in profile.
	if (PendingEpicLink)
	{
		FAccountResult OnLinked = MoveTemp(PendingEpicLink);
		PendingEpicLink = nullptr;
		bSilentAttempt = false;
		const FString LinkToken = bWasSuccessful ? ReadEpicToken() : FString();
		if (LinkToken.IsEmpty())
		{
			bEmailBusy = false;
			OnLinked(false, Error.IsEmpty() ? TEXT("The Epic sign-in was cancelled.") : Error);
			return;
		}
		EpicDisplayName = Identity.IsValid() ? Identity->GetPlayerNickname(0) : FString();
		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("kind"), TEXT("epic"));
		Body->SetStringField(TEXT("epic_token"), LinkToken);
		CallAccountLink(Body, MoveTemp(OnLinked));
		return;
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
	// No profile for a new Epic account yet: the player decides (ChoosingProfile).
	ExchangeTokenForSession(Token, /*bRenewal*/ false, /*bCreate*/ false);
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

void UCSAccountSubsystem::ExchangeTokenForSession(const FString& EpicToken, bool bRenewal, bool bCreate)
{
	const FCSBackendConfig& Config = FCSBackendConfig::Get();
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("epic_token"), EpicToken);
	Body->SetBoolField(TEXT("create"), bRenewal || bCreate);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(Config.FunctionUrl(TEXT("eos-login")), TEXT("POST"), false);
	Request->SetContentAsString(ToJson(Body));
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, bRenewal](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			const int32 Code = (bConnected && Response.IsValid()) ? Response->GetResponseCode() : 0;
			const TSharedPtr<FJsonObject> Json = Response.IsValid() ? ParseJson(Response->GetContentAsString()) : nullptr;

			if (Code == 404 && !bRenewal && ErrorFromJson(Json, FString()) == TEXT("no_profile"))
			{
				// A new Epic account: create a profile, or link it to the email one.
				UE_LOG(LogCSAuth, Log, TEXT("Accounts: this Epic account has no profile yet - asking the player."));
				ClearSession();
				LastError.Reset();
				SetState(ECSAccountState::ChoosingProfile);
				return;
			}
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

			ReadLinked(Json);
			if (bRenewal)
			{
				UE_LOG(LogCSAuth, Log, TEXT("Accounts: session renewed."));
				FinishRenewal(true);
				OnAccountChanged.Broadcast();
				return;
			}
			SignInMethod = ECSSignInMethod::Epic;
			RememberSession();
			UE_LOG(LogCSAuth, Log, TEXT("Accounts: signed in with Epic as '%s' (%s, %d kills, %d matches)."),
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

	if (SignInMethod == ECSSignInMethod::Email)
	{
		// The Supabase refresh token gets a new access token, email-login a new login token.
		RefreshEmailSession(/*bRenewal*/ true, [this](bool bOk, const FString&)
		{
			FinishRenewal(bOk);
			if (bOk)
			{
				OnAccountChanged.Broadcast();
			}
		});
		return;
	}

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
	if (!SupabaseAccessToken.IsEmpty())
	{
		// Revoke this device's email session (refresh token) on the server too.
		AuthRequest(TEXT("logout?scope=local"), TEXT("POST"), MakeShared<FJsonObject>(), SupabaseAccessToken,
			[](int32 Code, const TSharedPtr<FJsonObject>&)
			{
				UE_LOG(LogCSAuth, Log, TEXT("Accounts: email session revoked (%d)."), Code);
			});
	}
	FCSSessionStore::Clear();
	PendingEpicLink = nullptr;
	bEmailBusy = false;
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
	// An Epic player goes straight to the Epic window; an email player back to
	// the sign-in screen, where both ways are offered.
	bSignInAfterLogout = SignInMethod == ECSSignInMethod::Epic;
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

FString UCSAccountSubsystem::DebugSupabaseAccessToken() const
{
#if UE_BUILD_SHIPPING
	return FString();
#else
	return SupabaseAccessToken;
#endif
}

FString UCSAccountSubsystem::MakePhotonAuthParameters() const
{
	return IsReady() && !AccessToken.IsEmpty()
		? FString::Printf(TEXT("token=%s"), *FGenericPlatformHttp::UrlEncode(AccessToken))
		: FString();
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

// ---------------------------------------------------------------------------
// v2.4: launch sign-in, email + password, linking
// ---------------------------------------------------------------------------

void UCSAccountSubsystem::TryAutoSignIn()
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
	FCSStoredSession Stored;
	if (FCSSessionStore::Load(Stored))
	{
		RememberedEmail = Stored.Email;
		if (Stored.Method == TEXT("email") && !Stored.RefreshToken.IsEmpty())
		{
			UE_LOG(LogCSAuth, Log, TEXT("Accounts: signing in with the remembered email session."));
			LastError.Reset();
			SupabaseRefreshToken = Stored.RefreshToken;
			SetState(ECSAccountState::CheckingSession);
			RefreshEmailSession(/*bRenewal*/ false, [](bool, const FString&) {});
			return;
		}
	}
	// The email self-tests must not be signed in with the developer's Epic session.
	const FString TestStage = CSAuthSelfTest::Stage();
	if (!TestStage.IsEmpty() && TestStage != TEXT("link"))
	{
		SetState(ECSAccountState::SignedOut);
		return;
	}
	TrySilentSignIn();
}

void UCSAccountSubsystem::AuthRequest(const FString& Path, const FString& Verb, const TSharedRef<FJsonObject>& Body,
	const FString& BearerToken, FResponseHandler OnDone)
{
	const FCSBackendConfig& Config = FCSBackendConfig::Get();
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(
		FString::Printf(TEXT("%s/auth/v1/%s"), *Config.SupabaseUrl.TrimEnd().TrimChar('/'), *Path), Verb, false);
	if (!BearerToken.IsEmpty())
	{
		Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *BearerToken));
	}
	Request->SetContentAsString(ToJson(Body));
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[OnDone](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			const int32 Code = (bConnected && Response.IsValid()) ? Response->GetResponseCode() : 0;
			OnDone(Code, Response.IsValid() ? ParseJson(Response->GetContentAsString()) : nullptr);
		});
	Request->ProcessRequest();
}

bool UCSAccountSubsystem::TakeAuthSession(const TSharedPtr<FJsonObject>& Json)
{
	FString Access;
	FString Refresh;
	if (!Json.IsValid() || !Json->TryGetStringField(TEXT("access_token"), Access) || Access.IsEmpty())
	{
		return false;
	}
	Json->TryGetStringField(TEXT("refresh_token"), Refresh);
	SupabaseAccessToken = Access;
	if (!Refresh.IsEmpty())
	{
		SupabaseRefreshToken = Refresh;
	}
	const TSharedPtr<FJsonObject>* User = nullptr;
	FString Email;
	if (Json->TryGetObjectField(TEXT("user"), User) && (*User)->TryGetStringField(TEXT("email"), Email))
	{
		EmailAddress = Email;
	}
	return true;
}

void UCSAccountSubsystem::RememberSession()
{
	FCSStoredSession Stored;
	if (SignInMethod == ECSSignInMethod::Email && !SupabaseRefreshToken.IsEmpty())
	{
		Stored.Method = TEXT("email");
		Stored.Email = EmailAddress;
		Stored.RefreshToken = SupabaseRefreshToken;
		RememberedEmail = EmailAddress;
	}
	else if (SignInMethod == ECSSignInMethod::Epic)
	{
		// The EOS SDK keeps the Epic session itself; only the choice is stored.
		Stored.Method = TEXT("epic");
		Stored.Email = RememberedEmail;
	}
	else
	{
		return;
	}
	FCSSessionStore::Save(Stored);
}

void UCSAccountSubsystem::ReadLinked(const TSharedPtr<FJsonObject>& Json)
{
	const TSharedPtr<FJsonObject>* Linked = nullptr;
	if (!Json.IsValid() || !Json->TryGetObjectField(TEXT("linked"), Linked))
	{
		return;
	}
	(*Linked)->TryGetBoolField(TEXT("epic"), bEpicLinked);
	(*Linked)->TryGetBoolField(TEXT("email"), bEmailLinked);
	FString Address;
	if ((*Linked)->TryGetStringField(TEXT("email_address"), Address))
	{
		EmailAddress = Address;
	}
}

void UCSAccountSubsystem::FailEmail(const FString& Message, FAccountResult& OnDone)
{
	UE_LOG(LogCSAuth, Warning, TEXT("Accounts: email sign-in failed - %s"), *Message);
	const bool bWasChoosing = State == ECSAccountState::ChoosingProfile;
	ClearSession();
	bEmailBusy = false;
	LastError = Message;
	SetState(bWasChoosing ? ECSAccountState::ChoosingProfile : ECSAccountState::SignedOut);
	if (OnDone)
	{
		OnDone(false, Message);
	}
}

void UCSAccountSubsystem::ExchangeEmailSession(bool bRenewal, const FString& WantedNickname, FAccountResult OnDone)
{
	const FCSBackendConfig& Config = FCSBackendConfig::Get();
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("action"), TEXT("login"));
	Body->SetStringField(TEXT("access_token"), SupabaseAccessToken);
	if (!WantedNickname.IsEmpty())
	{
		Body->SetStringField(TEXT("nickname"), WantedNickname);
	}
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(Config.FunctionUrl(TEXT("email-login")), TEXT("POST"), false);
	Request->SetContentAsString(ToJson(Body));
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, bRenewal, OnDone](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected) mutable
		{
			const int32 Code = (bConnected && Response.IsValid()) ? Response->GetResponseCode() : 0;
			const TSharedPtr<FJsonObject> Json = Response.IsValid() ? ParseJson(Response->GetContentAsString()) : nullptr;
			if (Code == 200)
			{
				ApplySession(Json);
			}
			if (Code != 200 || ProfileId.IsEmpty() || AccessToken.IsEmpty())
			{
				const FString Message = Code == 200 ? TEXT("The account server sent an answer the game did not understand.") : FriendlyError(Code, Json);
				if (bRenewal)
				{
					if (Code == 401 || Code == 403)
					{
						UE_LOG(LogCSAuth, Warning, TEXT("Accounts: email session renewal refused (%d) - signing out."), Code);
						FCSSessionStore::Clear();
						ClearSession();
						LastError = Message;
						SetState(Code == 403 ? ECSAccountState::Failed : ECSAccountState::SignedOut);
					}
					OnDone(false, Message);
					return;
				}
				FailEmail(Message, OnDone);
				return;
			}
			ReadLinked(Json);
			SignInMethod = ECSSignInMethod::Email;
			bEmailLinked = true;
			RememberSession();
			bEmailBusy = false;
			if (!bRenewal)
			{
				LastError.Reset();
				UE_LOG(LogCSAuth, Log, TEXT("Accounts: signed in with email as '%s' (%s, %d kills, %d matches)."),
					*Nickname, *Role, Stats.Kills, Stats.Matches);
				SetState(ECSAccountState::Ready);
			}
			OnDone(true, FString());
		});
	Request->ProcessRequest();
}

void UCSAccountSubsystem::RefreshEmailSession(bool bRenewal, FAccountResult OnDone)
{
	if (SupabaseRefreshToken.IsEmpty())
	{
		OnDone(false, TEXT("No saved session."));
		return;
	}
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("refresh_token"), SupabaseRefreshToken);
	AuthRequest(TEXT("token?grant_type=refresh_token"), TEXT("POST"), Body, FString(),
		[this, bRenewal, OnDone](int32 Code, const TSharedPtr<FJsonObject>& Json) mutable
		{
			if (Code == 200 && TakeAuthSession(Json))
			{
				// Supabase rotated the refresh token: keep the new one right away.
				if (SignInMethod == ECSSignInMethod::None)
				{
					SignInMethod = ECSSignInMethod::Email;
				}
				RememberSession();
				ExchangeEmailSession(bRenewal, FString(), MoveTemp(OnDone));
				return;
			}
			const FString Message = FriendlyError(Code, Json);
			if (Code == 0)
			{
				// Offline: a renewal tries again later; a launch offers retry / offline.
				if (!bRenewal)
				{
					ClearSession();
					Fail(Message);
				}
				OnDone(false, Message);
				return;
			}
			// The session was revoked or has expired: forget it, ask again.
			UE_LOG(LogCSAuth, Log, TEXT("Accounts: the remembered email session is no longer valid (%d)."), Code);
			FCSSessionStore::Clear();
			ClearSession();
			LastError = TEXT("Your saved session has expired. Sign in again.");
			SetState(ECSAccountState::SignedOut);
			OnDone(false, LastError);
		});
}

void UCSAccountSubsystem::SignInWithEmail(const FString& InEmail, const FString& Password, FAccountResult OnDone)
{
	const FString Email = InEmail.TrimStartAndEnd().ToLower();
	if (!LooksLikeEmail(Email))
	{
		OnDone(false, TEXT("Check the email address."));
		return;
	}
	if (Password.IsEmpty())
	{
		OnDone(false, TEXT("Enter the password."));
		return;
	}
	if (IsBusy())
	{
		OnDone(false, TEXT("Please wait..."));
		return;
	}
	const bool bChoosing = State == ECSAccountState::ChoosingProfile;
	bEmailBusy = true;
	LastError.Reset();
	if (!bChoosing)
	{
		SetState(ECSAccountState::SigningIn);
	}
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("email"), Email);
	Body->SetStringField(TEXT("password"), Password);
	AuthRequest(TEXT("token?grant_type=password"), TEXT("POST"), Body, FString(),
		[this, Email, OnDone](int32 Code, const TSharedPtr<FJsonObject>& Json) mutable
		{
			if (Code != 200 || !TakeAuthSession(Json))
			{
				FailEmail(FriendlyError(Code, Json), OnDone);
				return;
			}
			if (EmailAddress.IsEmpty())
			{
				EmailAddress = Email;
			}
			ExchangeEmailSession(/*bRenewal*/ false, FString(), MoveTemp(OnDone));
		});
}

void UCSAccountSubsystem::RegisterWithEmail(const FString& InEmail, const FString& Password, const FString& InNickname, FAccountResult OnDone)
{
	const FString Email = InEmail.TrimStartAndEnd().ToLower();
	const FString Nick = InNickname.TrimStartAndEnd();
	if (Nick.Len() < 3 || Nick.Len() > 20)
	{
		OnDone(false, TEXT("The nickname must be 3 to 20 characters."));
		return;
	}
	if (!LooksLikeEmail(Email))
	{
		OnDone(false, TEXT("Check the email address."));
		return;
	}
	if (Password.Len() < MinPasswordLength)
	{
		OnDone(false, TEXT("The password must be at least 8 characters."));
		return;
	}
	if (IsBusy())
	{
		OnDone(false, TEXT("Please wait..."));
		return;
	}
	bEmailBusy = true;
	LastError.Reset();
	SetState(ECSAccountState::SigningIn);

	// 1. Is the nickname free? (Checked again when the profile is created.)
	const TSharedRef<FJsonObject> Check = MakeShared<FJsonObject>();
	Check->SetStringField(TEXT("action"), TEXT("check_nickname"));
	Check->SetStringField(TEXT("nickname"), Nick);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(FCSBackendConfig::Get().FunctionUrl(TEXT("email-login")), TEXT("POST"), false);
	Request->SetContentAsString(ToJson(Check));
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, Email, Password, Nick, OnDone](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected) mutable
		{
			const int32 Code = (bConnected && Response.IsValid()) ? Response->GetResponseCode() : 0;
			if (Code != 200)
			{
				FailEmail(FriendlyError(Code, Response.IsValid() ? ParseJson(Response->GetContentAsString()) : nullptr), OnDone);
				return;
			}
			// 2. The Supabase Auth account (the nickname rides along for the first sign-in).
			const TSharedRef<FJsonObject> SignUp = MakeShared<FJsonObject>();
			SignUp->SetStringField(TEXT("email"), Email);
			SignUp->SetStringField(TEXT("password"), Password);
			const TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetStringField(TEXT("nickname"), Nick);
			SignUp->SetObjectField(TEXT("data"), Data);
			AuthRequest(TEXT("signup"), TEXT("POST"), SignUp, FString(),
				[this, Email, Password, Nick, OnDone](int32 SignUpCode, const TSharedPtr<FJsonObject>& Json) mutable
				{
					if (SignUpCode == 200 && TakeAuthSession(Json))
					{
						EmailAddress = Email;
						UE_LOG(LogCSAuth, Log, TEXT("Accounts: email account created."));
						ExchangeEmailSession(/*bRenewal*/ false, Nick, MoveTemp(OnDone));
						return;
					}
					if (SignUpCode == 200)
					{
						FailEmail(TEXT("Account created. Confirm your email (see the letter), then sign in."), OnDone);
						return;
					}
					FString ErrorCode;
					if (Json.IsValid())
					{
						Json->TryGetStringField(TEXT("error_code"), ErrorCode);
					}
					if (SignUpCode != 422 || (ErrorCode != TEXT("user_already_exists") && ErrorCode != TEXT("email_exists")))
					{
						FailEmail(FriendlyError(SignUpCode, Json), OnDone);
						return;
					}
					// 3. The email exists: with the right password this is the same person
					// finishing a registration (e.g. their nickname was taken last time).
					const TSharedRef<FJsonObject> Login = MakeShared<FJsonObject>();
					Login->SetStringField(TEXT("email"), Email);
					Login->SetStringField(TEXT("password"), Password);
					AuthRequest(TEXT("token?grant_type=password"), TEXT("POST"), Login, FString(),
						[this, Email, Nick, OnDone](int32 LoginCode, const TSharedPtr<FJsonObject>& LoginJson) mutable
						{
							if (LoginCode != 200 || !TakeAuthSession(LoginJson))
							{
								FailEmail(TEXT("An account with this email already exists. Sign in, or reset the password."), OnDone);
								return;
							}
							EmailAddress = Email;
							ExchangeEmailSession(/*bRenewal*/ false, Nick, MoveTemp(OnDone));
						});
				});
		});
	Request->ProcessRequest();
}

void UCSAccountSubsystem::RequestPasswordReset(const FString& InEmail, FAccountResult OnDone)
{
	const FString Email = InEmail.TrimStartAndEnd().ToLower();
	if (!LooksLikeEmail(Email))
	{
		OnDone(false, TEXT("Check the email address."));
		return;
	}
	if (bEmailBusy)
	{
		OnDone(false, TEXT("Please wait..."));
		return;
	}
	bEmailBusy = true;
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("email"), Email);
	AuthRequest(TEXT("recover"), TEXT("POST"), Body, FString(),
		[this, OnDone](int32 Code, const TSharedPtr<FJsonObject>& Json)
		{
			bEmailBusy = false;
			if (Code != 200)
			{
				OnDone(false, FriendlyError(Code, Json));
				return;
			}
			UE_LOG(LogCSAuth, Log, TEXT("Accounts: recovery code requested."));
			OnDone(true, TEXT("If this email has an account, a 6-digit code is on its way. Check the inbox and the spam folder."));
		});
}

void UCSAccountSubsystem::ResetPassword(const FString& InEmail, const FString& InCode, const FString& NewPassword, FAccountResult OnDone)
{
	const FString Email = InEmail.TrimStartAndEnd().ToLower();
	FString Code = InCode.Replace(TEXT(" "), TEXT("")).TrimStartAndEnd();
	// The standard Supabase letter has a link, not a code (the code needs a custom
	// template, which needs custom SMTP): the link pasted whole works too - its
	// "token" parameter is the one-time token hash.
	FString TokenHash;
	const int32 TokenAt = Code.Find(TEXT("token="));
	if (TokenAt != INDEX_NONE)
	{
		TokenHash = Code.Mid(TokenAt + 6);
		int32 End = INDEX_NONE;
		if (TokenHash.FindChar(TEXT('&'), End))
		{
			TokenHash.LeftInline(End);
		}
	}
	if (!LooksLikeEmail(Email) && TokenHash.IsEmpty())
	{
		OnDone(false, TEXT("Check the email address."));
		return;
	}
	if (TokenHash.IsEmpty() && (Code.Len() < 6 || Code.Len() > 10))
	{
		OnDone(false, TEXT("Enter the code (or paste the link) from the email."));
		return;
	}
	if (NewPassword.Len() < MinPasswordLength)
	{
		OnDone(false, TEXT("The password must be at least 8 characters."));
		return;
	}
	if (IsBusy())
	{
		OnDone(false, TEXT("Please wait..."));
		return;
	}
	bEmailBusy = true;
	LastError.Reset();
	SetState(ECSAccountState::SigningIn);
	const TSharedRef<FJsonObject> Verify = MakeShared<FJsonObject>();
	Verify->SetStringField(TEXT("type"), TEXT("recovery"));
	if (TokenHash.IsEmpty())
	{
		Verify->SetStringField(TEXT("email"), Email);
		Verify->SetStringField(TEXT("token"), Code);
	}
	else
	{
		Verify->SetStringField(TEXT("token_hash"), TokenHash);
	}
	AuthRequest(TEXT("verify"), TEXT("POST"), Verify, FString(),
		[this, Email, NewPassword, OnDone](int32 VerifyCode, const TSharedPtr<FJsonObject>& Json) mutable
		{
			if (VerifyCode != 200 || !TakeAuthSession(Json))
			{
				FailEmail(FriendlyError(VerifyCode, Json), OnDone);
				return;
			}
			if (EmailAddress.IsEmpty())
			{
				EmailAddress = Email;
			}
			const TSharedRef<FJsonObject> Update = MakeShared<FJsonObject>();
			Update->SetStringField(TEXT("password"), NewPassword);
			AuthRequest(TEXT("user"), TEXT("PUT"), Update, SupabaseAccessToken,
				[this, OnDone](int32 UpdateCode, const TSharedPtr<FJsonObject>& UpdateJson) mutable
				{
					if (UpdateCode != 200)
					{
						FailEmail(FriendlyError(UpdateCode, UpdateJson), OnDone);
						return;
					}
					UE_LOG(LogCSAuth, Log, TEXT("Accounts: password changed with a recovery code."));
					ExchangeEmailSession(/*bRenewal*/ false, FString(), MoveTemp(OnDone));
				});
		});
}

void UCSAccountSubsystem::CreateProfileForEpic()
{
	if (State != ECSAccountState::ChoosingProfile)
	{
		return;
	}
	const FString Token = ReadEpicToken();
	if (Token.IsEmpty())
	{
		Fail(TEXT("The Epic session has ended. Sign in again."));
		return;
	}
	SetState(ECSAccountState::SigningIn);
	ExchangeTokenForSession(Token, /*bRenewal*/ false, /*bCreate*/ true);
}

void UCSAccountSubsystem::LinkEpicToEmailProfile(const FString& Email, const FString& Password, FAccountResult OnDone)
{
	if (State != ECSAccountState::ChoosingProfile)
	{
		OnDone(false, TEXT("Sign in with Epic first."));
		return;
	}
	const FString EpicToken = ReadEpicToken();
	if (EpicToken.IsEmpty())
	{
		OnDone(false, TEXT("The Epic session has ended. Sign in with Epic again."));
		return;
	}
	SignInWithEmail(Email, Password, [this, EpicToken, OnDone](bool bOk, const FString& Message)
	{
		if (!bOk)
		{
			OnDone(false, Message);
			return;
		}
		// Signed in to the email profile: now the Epic account joins it.
		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("kind"), TEXT("epic"));
		Body->SetStringField(TEXT("epic_token"), EpicToken);
		CallAccountLink(Body, [this, OnDone](bool bLinked, const FString& LinkMessage)
		{
			Notice = bLinked ? TEXT("Epic Games is now linked to this profile.") : FString::Printf(TEXT("Epic Games was not linked: %s"), *LinkMessage);
			OnAccountChanged.Broadcast();
			OnDone(bLinked, Notice);
		});
	});
}

void UCSAccountSubsystem::CancelProfileChoice()
{
	if (State == ECSAccountState::ChoosingProfile)
	{
		SignOut();
	}
}

void UCSAccountSubsystem::CallAccountLink(const TSharedRef<FJsonObject>& Body, FAccountResult OnDone)
{
	bEmailBusy = true;
	PostAuthenticated(FCSBackendConfig::Get().FunctionUrl(TEXT("account-link")), ToJson(Body),
		[this, OnDone](int32 Code, const TSharedPtr<FJsonObject>& Json)
		{
			bEmailBusy = false;
			if (Code != 200)
			{
				const FString Message = FriendlyError(Code, Json);
				UE_LOG(LogCSAuth, Warning, TEXT("Accounts: link refused (%d) - %s"), Code, *Message);
				OnDone(false, Message);
				OnAccountChanged.Broadcast();
				return;
			}
			ReadLinked(Json);
			UE_LOG(LogCSAuth, Log, TEXT("Accounts: sign-in methods now: Epic %s, email %s."),
				bEpicLinked ? TEXT("linked") : TEXT("-"), bEmailLinked ? TEXT("linked") : TEXT("-"));
			OnDone(true, TEXT("Linked."));
			OnAccountChanged.Broadcast();
		});
}

void UCSAccountSubsystem::LinkEmail(const FString& InEmail, const FString& Password, FAccountResult OnDone)
{
	const FString Email = InEmail.TrimStartAndEnd().ToLower();
	if (!IsReady())
	{
		OnDone(false, TEXT("Sign in first."));
		return;
	}
	if (bEmailLinked)
	{
		OnDone(false, TEXT("This profile already has an email."));
		return;
	}
	if (!LooksLikeEmail(Email))
	{
		OnDone(false, TEXT("Check the email address."));
		return;
	}
	if (Password.Len() < MinPasswordLength)
	{
		OnDone(false, TEXT("The password must be at least 8 characters."));
		return;
	}
	if (bEmailBusy)
	{
		OnDone(false, TEXT("Please wait..."));
		return;
	}
	bEmailBusy = true;
	OnAccountChanged.Broadcast();

	// The email's own session proves it; it is used for the link, then revoked.
	auto LinkWith = [this, OnDone](const TSharedPtr<FJsonObject>& SessionJson)
	{
		FString Access;
		SessionJson->TryGetStringField(TEXT("access_token"), Access);
		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("kind"), TEXT("email"));
		Body->SetStringField(TEXT("access_token"), Access);
		CallAccountLink(Body, [this, Access, OnDone](bool bOk, const FString& Message)
		{
			AuthRequest(TEXT("logout?scope=local"), TEXT("POST"), MakeShared<FJsonObject>(), Access,
				[](int32, const TSharedPtr<FJsonObject>&) {});
			OnDone(bOk, bOk ? TEXT("Email linked. You can now sign in with it too.") : Message);
		});
	};
	const TSharedRef<FJsonObject> SignUp = MakeShared<FJsonObject>();
	SignUp->SetStringField(TEXT("email"), Email);
	SignUp->SetStringField(TEXT("password"), Password);
	AuthRequest(TEXT("signup"), TEXT("POST"), SignUp, FString(),
		[this, Email, Password, OnDone, LinkWith](int32 Code, const TSharedPtr<FJsonObject>& Json)
		{
			FString Access;
			if (Code == 200 && Json.IsValid() && Json->TryGetStringField(TEXT("access_token"), Access) && !Access.IsEmpty())
			{
				LinkWith(Json);
				return;
			}
			FString ErrorCode;
			if (Json.IsValid())
			{
				Json->TryGetStringField(TEXT("error_code"), ErrorCode);
			}
			if (Code != 422 || (ErrorCode != TEXT("user_already_exists") && ErrorCode != TEXT("email_exists")))
			{
				bEmailBusy = false;
				OnDone(false, Code == 200 ? TEXT("Confirm your email (see the letter), then link it again.") : FriendlyError(Code, Json));
				OnAccountChanged.Broadcast();
				return;
			}
			// An existing email account: the password proves it is theirs.
			const TSharedRef<FJsonObject> Login = MakeShared<FJsonObject>();
			Login->SetStringField(TEXT("email"), Email);
			Login->SetStringField(TEXT("password"), Password);
			AuthRequest(TEXT("token?grant_type=password"), TEXT("POST"), Login, FString(),
				[this, OnDone, LinkWith](int32 LoginCode, const TSharedPtr<FJsonObject>& LoginJson)
				{
					if (LoginCode != 200 || !LoginJson.IsValid() || !LoginJson->HasField(TEXT("access_token")))
					{
						bEmailBusy = false;
						OnDone(false, FriendlyError(LoginCode, LoginJson));
						OnAccountChanged.Broadcast();
						return;
					}
					LinkWith(LoginJson);
				});
		});
}

void UCSAccountSubsystem::LinkEpic(FAccountResult OnDone)
{
	if (!IsReady())
	{
		OnDone(false, TEXT("Sign in first."));
		return;
	}
	if (bEpicLinked)
	{
		OnDone(false, TEXT("This profile already has an Epic account."));
		return;
	}
	if (bEmailBusy || bRenewing)
	{
		OnDone(false, TEXT("Please wait..."));
		return;
	}
	if (!GetEpicIdentity().IsValid())
	{
		OnDone(false, TEXT("Epic Online Services is unavailable."));
		return;
	}
	bEmailBusy = true;
	PendingEpicLink = MoveTemp(OnDone);
	OnAccountChanged.Broadcast();
	// The Epic window (or the Epic session already open this launch).
	BeginEpicLogin(TEXT("accountportal"));
}
