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
#include "OnlineSubsystemTypes.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	const FName EOSSubsystemName(TEXT("EOS"));

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

	FString ErrorFromBody(const FString& Body, const FString& Fallback)
	{
		if (const TSharedPtr<FJsonObject> Json = ParseJson(Body))
		{
			FString Message;
			if (Json->TryGetStringField(TEXT("error"), Message) || Json->TryGetStringField(TEXT("message"), Message))
			{
				return Message;
			}
		}
		return Fallback;
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

	const FCSBackendConfig& Config = FCSBackendConfig::Get();
	if (!Config.HasEOS() || !Config.HasSupabase())
	{
		State = ECSAccountState::NotConfigured;
		LastError = TEXT("Config/Backend.ini is missing or incomplete (see docs/ACCOUNTS.md).");
		UE_LOG(LogCS, Warning, TEXT("Accounts: %s"), *LastError);
	}
}

void UCSAccountSubsystem::Deinitialize()
{
	if (LoginHandle.IsValid())
	{
		if (const IOnlineIdentityPtr Identity = GetEpicIdentity())
		{
			Identity->ClearOnLoginCompleteDelegate_Handle(0, LoginHandle);
		}
		LoginHandle.Reset();
	}
	Super::Deinitialize();
}

bool UCSAccountSubsystem::IsSignInRequired() const
{
	// Self-tests and offline debugging run without an account.
	return !FParse::Param(FCommandLine::Get(), TEXT("noaccount"));
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
	UE_LOG(LogCS, Warning, TEXT("Accounts: sign-in failed - %s"), *Error);
	SetState(ECSAccountState::Failed);
}

void UCSAccountSubsystem::SignIn()
{
	if (State == ECSAccountState::SigningIn || State == ECSAccountState::Ready)
	{
		return;
	}
	const FCSBackendConfig& Config = FCSBackendConfig::Get();
	if (!Config.HasEOS() || !Config.HasSupabase())
	{
		State = ECSAccountState::NotConfigured;
		OnAccountChanged.Broadcast();
		return;
	}
	LastError.Reset();
	SetState(ECSAccountState::SigningIn);
	BeginEpicLogin();
}

void UCSAccountSubsystem::SignOut()
{
	if (const IOnlineIdentityPtr Identity = GetEpicIdentity())
	{
		Identity->Logout(0);
	}
	AccessToken.Reset();
	ProfileId.Reset();
	Nickname.Reset();
	Stats = FCSAccountStats();
	SetState(ECSAccountState::SignedOut);
}

void UCSAccountSubsystem::BeginEpicLogin()
{
	const IOnlineIdentityPtr Identity = GetEpicIdentity();
	if (!Identity.IsValid())
	{
		Fail(TEXT("Epic Online Services is unavailable (plugin or configuration missing)."));
		return;
	}

	// Already signed in this session (a second visit to the menu).
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

	// "accountportal" is the Epic-hosted login: the overlay if it is available,
	// otherwise the browser. The SDK reuses a saved session when there is one,
	// so returning players are not asked again.
	FOnlineAccountCredentials Credentials(TEXT("accountportal"), FString(), FString());
	UE_LOG(LogCS, Log, TEXT("Accounts: starting the Epic login."));
	Identity->Login(0, Credentials);
}

void UCSAccountSubsystem::HandleEpicLogin(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error)
{
	const IOnlineIdentityPtr Identity = GetEpicIdentity();
	if (Identity.IsValid() && LoginHandle.IsValid())
	{
		Identity->ClearOnLoginCompleteDelegate_Handle(0, LoginHandle);
		LoginHandle.Reset();
	}
	if (!bWasSuccessful)
	{
		Fail(Error.IsEmpty() ? TEXT("the Epic login was cancelled") : Error);
		return;
	}

	EpicDisplayName = Identity.IsValid() ? Identity->GetPlayerNickname(0) : FString();
	const FString Token = ReadEpicToken();
	if (Token.IsEmpty())
	{
		Fail(TEXT("Epic signed in but returned no token."));
		return;
	}
	UE_LOG(LogCS, Log, TEXT("Accounts: Epic login OK (%s); asking the backend."), *EpicDisplayName);
	ExchangeTokenForSession(Token);
}

FString UCSAccountSubsystem::ReadEpicToken() const
{
	const IOnlineIdentityPtr Identity = GetEpicIdentity();
	if (!Identity.IsValid())
	{
		return FString();
	}
	FString Token = Identity->GetAuthToken(0);
	if (!Token.IsEmpty())
	{
		return Token;
	}
	// Some versions expose it only as an account attribute.
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

void UCSAccountSubsystem::ExchangeTokenForSession(const FString& EpicToken)
{
	const FCSBackendConfig& Config = FCSBackendConfig::Get();

	FString Body;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("epic_token"), EpicToken);
	Writer->WriteValue(TEXT("display_name"), EpicDisplayName);
	Writer->WriteObjectEnd();
	Writer->Close();

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(Config.FunctionUrl(TEXT("eos-login")), TEXT("POST"), false);
	Request->SetContentAsString(Body);
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			if (!bConnected || !Response.IsValid())
			{
				Fail(TEXT("the account server did not answer."));
				return;
			}
			const FString Content = Response->GetContentAsString();
			if (Response->GetResponseCode() != 200)
			{
				Fail(ErrorFromBody(Content, FString::Printf(TEXT("the account server answered %d"), Response->GetResponseCode())));
				return;
			}
			const TSharedPtr<FJsonObject> Json = ParseJson(Content);
			const TSharedPtr<FJsonObject>* Profile = nullptr;
			if (!Json.IsValid() || !Json->TryGetObjectField(TEXT("profile"), Profile))
			{
				Fail(TEXT("the account server sent an answer the game did not understand."));
				return;
			}
			Json->TryGetStringField(TEXT("token"), AccessToken);
			(*Profile)->TryGetStringField(TEXT("id"), ProfileId);
			(*Profile)->TryGetStringField(TEXT("nickname"), Nickname);

			const TSharedPtr<FJsonObject>* StatsJson = nullptr;
			if (Json->TryGetObjectField(TEXT("stats"), StatsJson))
			{
				(*StatsJson)->TryGetNumberField(TEXT("matches"), Stats.Matches);
				(*StatsJson)->TryGetNumberField(TEXT("wins"), Stats.Wins);
				(*StatsJson)->TryGetNumberField(TEXT("kills"), Stats.Kills);
				(*StatsJson)->TryGetNumberField(TEXT("deaths"), Stats.Deaths);
				(*StatsJson)->TryGetNumberField(TEXT("headshots"), Stats.Headshots);
				(*StatsJson)->TryGetNumberField(TEXT("playtime_seconds"), Stats.PlaytimeSeconds);
			}
			int32 ExpiresIn = 0;
			Json->TryGetNumberField(TEXT("expires_in"), ExpiresIn);
			TokenExpiresAt = FPlatformTime::Seconds() + FMath::Max(60, ExpiresIn);

			if (ProfileId.IsEmpty() || AccessToken.IsEmpty())
			{
				Fail(TEXT("the account server sent no profile."));
				return;
			}
			UE_LOG(LogCS, Log, TEXT("Accounts: signed in as '%s' (%d kills, %d matches)."), *Nickname, Stats.Kills, Stats.Matches);
			SetState(ECSAccountState::Ready);
		});
	Request->ProcessRequest();
}

void UCSAccountSubsystem::SetNickname(const FString& NewNickname, TFunction<void(bool, const FString&)> OnDone)
{
	if (!IsReady())
	{
		OnDone(false, TEXT("not signed in"));
		return;
	}
	const FString Trimmed = NewNickname.TrimStartAndEnd();
	if (Trimmed.Len() < 3 || Trimmed.Len() > 20)
	{
		OnDone(false, TEXT("the name must be 3 to 20 characters"));
		return;
	}

	const FCSBackendConfig& Config = FCSBackendConfig::Get();
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request =
		MakeRequest(Config.RestUrl(FString::Printf(TEXT("profiles?id=eq.%s"), *ProfileId)), TEXT("PATCH"), true);
	Request->SetHeader(TEXT("Prefer"), TEXT("return=representation"));
	Request->SetContentAsString(FString::Printf(TEXT("{\"nickname\":\"%s\"}"), *Trimmed.ReplaceCharWithEscapedChar()));
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, Trimmed, OnDone](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			const int32 Code = (bConnected && Response.IsValid()) ? Response->GetResponseCode() : 0;
			if (Code == 200 || Code == 204)
			{
				Nickname = Trimmed;
				OnAccountChanged.Broadcast();
				OnDone(true, FString());
				return;
			}
			const FString Body = Response.IsValid() ? Response->GetContentAsString() : FString();
			OnDone(false, Body.Contains(TEXT("duplicate")) ? TEXT("that name is taken") : ErrorFromBody(Body, TEXT("could not rename")));
		});
	Request->ProcessRequest();
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
		MakeRequest(Config.RestUrl(TEXT("leaderboard?select=*&limit=20")), TEXT("GET"), true);
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

void UCSAccountSubsystem::ReportMatch(const FString& Mode, const FString& Map, const FString& Room,
	const FDateTime& StartedAtUtc, int32 WinnerTeam, int32 Rounds, const TArray<FCSMatchReportPlayer>& Players)
{
	if (!IsReady() || Players.Num() == 0)
	{
		return;
	}
	const FCSBackendConfig& Config = FCSBackendConfig::Get();

	FString Body;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("mode"), Mode);
	Writer->WriteValue(TEXT("map"), Map);
	Writer->WriteValue(TEXT("room"), Room);
	Writer->WriteValue(TEXT("started_at"), StartedAtUtc.ToIso8601());
	Writer->WriteValue(TEXT("ended_at"), FDateTime::UtcNow().ToIso8601());
	Writer->WriteValue(TEXT("winner_team"), WinnerTeam);
	Writer->WriteValue(TEXT("rounds"), Rounds);
	Writer->WriteArrayStart(TEXT("players"));
	for (const FCSMatchReportPlayer& Player : Players)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("profile_id"), Player.ProfileId);
		Writer->WriteValue(TEXT("team"), Player.Team);
		Writer->WriteValue(TEXT("kills"), Player.Kills);
		Writer->WriteValue(TEXT("deaths"), Player.Deaths);
		Writer->WriteValue(TEXT("headshots"), Player.Headshots);
		Writer->WriteValue(TEXT("damage"), Player.Damage);
		Writer->WriteValue(TEXT("money"), Player.Money);
		Writer->WriteValue(TEXT("won"), Player.bWon);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	Writer->Close();

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = MakeRequest(Config.FunctionUrl(TEXT("report-match")), TEXT("POST"), true);
	Request->SetContentAsString(Body);
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, Count = Players.Num()](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			const int32 Code = (bConnected && Response.IsValid()) ? Response->GetResponseCode() : 0;
			if (Code != 200)
			{
				UE_LOG(LogCS, Warning, TEXT("Accounts: the match was not recorded (%d: %s)."), Code,
					Response.IsValid() ? *ErrorFromBody(Response->GetContentAsString(), TEXT("no answer")) : TEXT("no answer"));
				return;
			}
			UE_LOG(LogCS, Log, TEXT("Accounts: match recorded for %d player(s)."), Count);
			// Our own totals moved; pull them again for the menu.
			const FCSBackendConfig& Config = FCSBackendConfig::Get();
			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Refresh =
				MakeRequest(Config.RestUrl(FString::Printf(TEXT("player_stats?profile_id=eq.%s&select=*"), *ProfileId)), TEXT("GET"), true);
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
						if (const TSharedPtr<FJsonObject> Row = Values[0]->AsObject())
						{
							Row->TryGetNumberField(TEXT("matches"), Stats.Matches);
							Row->TryGetNumberField(TEXT("wins"), Stats.Wins);
							Row->TryGetNumberField(TEXT("kills"), Stats.Kills);
							Row->TryGetNumberField(TEXT("deaths"), Stats.Deaths);
							Row->TryGetNumberField(TEXT("headshots"), Stats.Headshots);
							Row->TryGetNumberField(TEXT("playtime_seconds"), Stats.PlaytimeSeconds);
							OnAccountChanged.Broadcast();
						}
					}
				});
			Refresh->ProcessRequest();
		});
	Request->ProcessRequest();
}
