// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Account/CSAuthSelfTest.h"

#include "Account/CSAccountSubsystem.h"
#include "Account/CSBackendConfig.h"
#include "Account/CSSessionStore.h"
#include "Containers/Ticker.h"
#include "Core/CSLog.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if !UE_BUILD_SHIPPING

namespace
{
	using FNext = TFunction<void()>;
	using FStep = TFunction<void(FNext)>;

	FString StateFile()
	{
		return FPaths::ProjectSavedDir() / TEXT("CSTest") / TEXT("email_auth.json");
	}

	TSharedPtr<FJsonObject> LoadState()
	{
		FString Text;
		TSharedPtr<FJsonObject> Json;
		if (FFileHelper::LoadFileToString(Text, *StateFile()))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
		}
		return Json.IsValid() ? Json : MakeShared<FJsonObject>();
	}

	void SaveState(const TSharedPtr<FJsonObject>& Json)
	{
		FString Text;
		FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
		FFileHelper::SaveStringToFile(Text, *StateFile());
	}

	FString RandomTag()
	{
		return FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(10).ToLower();
	}

	class FAuthTest : public TSharedFromThis<FAuthTest>
	{
	public:
		FAuthTest(UCSAccountSubsystem* InAccount, const FString& InStage) : Account(InAccount), Stage(InStage) {}

		TWeakObjectPtr<UCSAccountSubsystem> Account;
		FString Stage;
		TArray<FStep> Steps;
		int32 Passed = 0;
		TArray<FString> Failed;
		TSharedPtr<FJsonObject> State = LoadState();

		void Check(bool bOk, const FString& What)
		{
			UE_LOG(LogCSAuth, Log, TEXT("EMAIL AUTH TEST: %s - %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
			if (bOk)
			{
				++Passed;
			}
			else
			{
				Failed.Add(What);
			}
		}

		void Run(int32 Index = 0)
		{
			if (!Account.IsValid())
			{
				return;
			}
			if (Index >= Steps.Num())
			{
				Finish();
				return;
			}
			TSharedRef<FAuthTest> Self = AsShared();
			Steps[Index]([Self, Index]() { Self->Run(Index + 1); });
		}

		/** Calls Then once Pred holds, or after Timeout seconds anyway. */
		void WaitFor(TFunction<bool()> Pred, float Timeout, FNext Then)
		{
			const double Deadline = FPlatformTime::Seconds() + Timeout;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Pred, Deadline, Then](float)
			{
				if (Pred() || FPlatformTime::Seconds() > Deadline)
				{
					Then();
					return false;
				}
				return true;
			}), 0.25f);
		}

		bool Settled() const
		{
			const ECSAccountState S = Account.IsValid() ? Account->GetState() : ECSAccountState::Failed;
			return S != ECSAccountState::CheckingSession && S != ECSAccountState::SigningIn;
		}

		void Finish()
		{
			SaveState(State);
			const bool bOk = Failed.Num() == 0;
			UE_LOG(LogCSAuth, Log, TEXT("EMAIL AUTH TEST RESULT: stage %s, %d passed, %d failed%s -> %s"), *Stage, Passed, Failed.Num(),
				bOk ? TEXT("") : *(TEXT(" (") + FString::Join(Failed, TEXT("; ")) + TEXT(")")),
				bOk ? TEXT("EMAIL AUTH OK") : TEXT("EMAIL AUTH BROKEN"));
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) { FPlatformMisc::RequestExit(false); return false; }), 1.5f);
		}

		// --- building blocks --------------------------------------------------------

		void AddWaitSettled(float Timeout)
		{
			Steps.Add([this, Timeout](FNext Next) { WaitFor([this]() { return Settled(); }, Timeout, Next); });
		}

		void AddSignIn(const FString& Email, const FString& Password, bool bExpectOk, const FString& Expect, const FString& What)
		{
			Steps.Add([this, Email, Password, bExpectOk, Expect, What](FNext Next)
			{
				Account->SignInWithEmail(Email, Password, [this, bExpectOk, Expect, What, Next](bool bOk, const FString& Message)
				{
					Check(bOk == bExpectOk && (Expect.IsEmpty() || Message.Contains(Expect)), FString::Printf(TEXT("%s ('%s')"), *What, *Message));
					Next();
				});
			});
		}

		void AddSignOut()
		{
			Steps.Add([this](FNext Next)
			{
				Account->SignOut();
				WaitFor([]() { return false; }, 1.5f, [this, Next]()
				{
					FCSStoredSession Stored;
					Check(Account->GetState() == ECSAccountState::SignedOut && !FCSSessionStore::Load(Stored),
						TEXT("sign out: signed out and the remembered session is deleted"));
					Next();
				});
			});
		}

		void CheckFunction(const FString& Function, const TSharedRef<FJsonObject>& Body, TFunction<bool(int32)> Expect, const FString& What, FNext Next)
		{
			Account->DebugCallFunction(Function, Body, [this, Expect, What, Next](int32 Code, const TSharedPtr<FJsonObject>& Json)
			{
				FString Error;
				if (Json.IsValid())
				{
					Json->TryGetStringField(TEXT("error"), Error);
				}
				Check(Expect(Code), FString::Printf(TEXT("%s (%d %s)"), *What, Code, *Error));
				Next();
			});
		}
	};

	void BuildRegister(FAuthTest& T)
	{
		const FString Tag = RandomTag();
		const FString Email = FString::Printf(TEXT("cstest-%s@example.com"), *Tag);
		const FString Password = FString::Printf(TEXT("Cs-%s-9x"), *RandomTag());
		const FString Nick = FString::Printf(TEXT("zt%s"), *Tag.Left(8));
		T.State = MakeShared<FJsonObject>();
		T.State->SetStringField(TEXT("email1"), Email);
		T.State->SetStringField(TEXT("password"), Password);
		T.State->SetStringField(TEXT("nick1"), Nick);

		T.AddWaitSettled(40.f);
		T.Steps.Add([&T](FNext Next)
		{
			T.Check(T.Account->GetState() == ECSAccountState::SignedOut, FString::Printf(TEXT("starts signed out (state %s)"),
				*UEnum::GetValueAsString(T.Account->GetState())));
			Next();
		});
		T.AddSignIn(FString::Printf(TEXT("cstest-nobody-%s@example.com"), *RandomTag()), TEXT("wrong-pass-1"), false,
			TEXT("Wrong email or password"), TEXT("unknown email / wrong password refused"));
		T.Steps.Add([&T, Email](FNext Next)
		{
			T.Account->ResetPassword(Email, TEXT("000000"), TEXT("Another-pass-1"), [&T, Next](bool bOk, const FString& Message)
			{
				T.Check(!bOk, FString::Printf(TEXT("a made-up recovery code is refused ('%s')"), *Message));
				Next();
			});
		});
		T.Steps.Add([&T, Email, Password](FNext Next)
		{
			T.Account->RegisterWithEmail(Email, Password, TEXT("Admin_x"), [&T, Next](bool bOk, const FString& Message)
			{
				T.Check(!bOk && Message.Contains(TEXT("reserved")), FString::Printf(TEXT("reserved nickname refused ('%s')"), *Message));
				Next();
			});
		});
		T.Steps.Add([&T, Email, Password](FNext Next)
		{
			T.Account->RegisterWithEmail(Email, Password, TEXT("x"), [&T, Next](bool bOk, const FString& Message)
			{
				T.Check(!bOk, FString::Printf(TEXT("too short nickname refused ('%s')"), *Message));
				Next();
			});
		});
		T.Steps.Add([&T, Email, Password, Nick](FNext Next)
		{
			T.Account->RegisterWithEmail(Email, Password, Nick, [&T, Nick, Next](bool bOk, const FString& Message)
			{
				UCSAccountSubsystem* A = T.Account.Get();
				FCSStoredSession Stored;
				const bool bStored = FCSSessionStore::Load(Stored) && Stored.Method == TEXT("email") && !Stored.RefreshToken.IsEmpty();
				T.Check(bOk && A->IsReady() && A->GetSignInMethod() == ECSSignInMethod::Email && A->GetNickname() == Nick
					&& !A->GetProfileId().IsEmpty() && A->HasEmailLinked() && !A->HasEpicLinked(),
					FString::Printf(TEXT("register + sign in: '%s', profile %s ('%s')"), *A->GetNickname(), *A->GetProfileId(), *Message));
				T.Check(bStored, TEXT("the session is remembered (encrypted file)"));
				T.State->SetStringField(TEXT("profile1"), A->GetProfileId());
				Next();
			});
		});
		T.Steps.Add([&T](FNext Next)
		{
			const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
			Body->SetStringField(TEXT("action"), TEXT("ticket"));
			Body->SetStringField(TEXT("match_id"), FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
			T.CheckFunction(TEXT("match"), Body, [](int32 Code) { return Code == 404; },
				TEXT("the email login token works for the game backend (unknown match -> 404, not 401)"), Next);
		});
		T.Steps.Add([&T, Nick](FNext Next)
		{
			const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
			Body->SetStringField(TEXT("action"), TEXT("check_nickname"));
			Body->SetStringField(TEXT("nickname"), Nick.ToUpper());
			T.CheckFunction(TEXT("email-login"), Body, [](int32 Code) { return Code == 409; },
				TEXT("the new nickname is taken now, in any letter case"), Next);
		});
		T.Steps.Add([&T](FNext Next)
		{
			const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
			Body->SetStringField(TEXT("kind"), TEXT("email"));
			Body->SetStringField(TEXT("access_token"), TEXT("not-a-token"));
			T.CheckFunction(TEXT("account-link"), Body, [](int32 Code) { return Code == 401; },
				TEXT("account-link refuses a fake email token"), Next);
		});
		T.Steps.Add([&T](FNext Next)
		{
			const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
			Body->SetStringField(TEXT("kind"), TEXT("epic"));
			Body->SetStringField(TEXT("epic_token"), TEXT("not-a-token"));
			T.CheckFunction(TEXT("account-link"), Body, [](int32 Code) { return Code == 401; },
				TEXT("account-link refuses a fake Epic token"), Next);
		});
		T.Steps.Add([&T](FNext Next)
		{
			// A Supabase Auth token is no login token: account-link must not take it as one.
			const FCSBackendConfig& Config = FCSBackendConfig::Get();
			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
			Request->SetURL(Config.FunctionUrl(TEXT("account-link")));
			Request->SetVerb(TEXT("POST"));
			Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
			Request->SetHeader(TEXT("apikey"), Config.SupabaseAnonKey);
			Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + T.Account->DebugSupabaseAccessToken());
			Request->SetContentAsString(TEXT("{\"kind\":\"epic\",\"epic_token\":\"x\"}"));
			Request->OnProcessRequestComplete().BindLambda([&T, Next](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
			{
				const int32 Code = bConnected && Response.IsValid() ? Response->GetResponseCode() : 0;
				T.Check(Code == 401, FString::Printf(TEXT("a Supabase Auth token is refused as a login token (%d)"), Code));
				Next();
			});
			Request->ProcessRequest();
		});
	}

	void BuildRelaunch(FAuthTest& T)
	{
		const FString Profile = T.State->GetStringField(TEXT("profile1"));
		T.AddWaitSettled(40.f);
		T.Steps.Add([&T, Profile](FNext Next)
		{
			UCSAccountSubsystem* A = T.Account.Get();
			T.Check(A->IsReady() && A->GetSignInMethod() == ECSSignInMethod::Email && A->GetProfileId() == Profile && !Profile.IsEmpty(),
				FString::Printf(TEXT("signed in automatically after the restart (state %s, profile %s, error '%s')"),
					*UEnum::GetValueAsString(A->GetState()), *A->GetProfileId(), *A->GetLastError()));
			Next();
		});
		T.AddSignOut();
	}

	void BuildAfter(FAuthTest& T)
	{
		const FString Email = T.State->GetStringField(TEXT("email1"));
		const FString Password = T.State->GetStringField(TEXT("password"));
		const FString Profile = T.State->GetStringField(TEXT("profile1"));
		T.AddWaitSettled(40.f);
		T.Steps.Add([&T](FNext Next)
		{
			T.Check(T.Account->GetState() == ECSAccountState::SignedOut, TEXT("after sign out nobody is signed in automatically"));
			Next();
		});
		T.AddSignIn(Email, TEXT("wrong-password-1"), false, TEXT("Wrong email or password"), TEXT("wrong password refused"));
		T.AddSignIn(Email, Password, true, FString(), TEXT("sign in with email + password"));
		T.Steps.Add([&T, Profile](FNext Next)
		{
			T.Check(T.Account->GetProfileId() == Profile, TEXT("the same profile, no duplicate"));
			Next();
		});
		T.AddSignOut();
	}

	void BuildLink(FAuthTest& T)
	{
		const FString Email1 = T.State->GetStringField(TEXT("email1"));
		const FString Password = T.State->GetStringField(TEXT("password"));
		const FString Email2 = FString::Printf(TEXT("cstest-%s@example.com"), *RandomTag());
		T.State->SetStringField(TEXT("email2"), Email2);
		T.AddWaitSettled(40.f);
		T.Steps.Add([&T](FNext Next)
		{
			UCSAccountSubsystem* A = T.Account.Get();
			T.Check(A->IsReady() && A->GetSignInMethod() == ECSSignInMethod::Epic && A->HasEpicLinked(),
				FString::Printf(TEXT("signed in with the saved Epic session as '%s' (state %s)"), *A->GetNickname(), *UEnum::GetValueAsString(A->GetState())));
			T.State->SetStringField(TEXT("epic_profile"), A->GetProfileId());
			T.State->SetBoolField(TEXT("epic_had_email"), A->HasEmailLinked());
			Next();
		});
		T.Steps.Add([&T, Email1, Password](FNext Next)
		{
			if (T.Account->HasEmailLinked())
			{
				Next();
				return;
			}
			T.Account->LinkEmail(Email1, Password, [&T, Next](bool bOk, const FString& Message)
			{
				T.Check(!bOk && Message.Contains(TEXT("another profile")), FString::Printf(TEXT("an email of another profile cannot be linked ('%s')"), *Message));
				Next();
			});
		});
		T.Steps.Add([&T, Email2, Password](FNext Next)
		{
			if (T.Account->HasEmailLinked())
			{
				T.Check(false, TEXT("this Epic profile already has an email - remove the old test link first (docs/ACCOUNTS.md)"));
				Next();
				return;
			}
			T.Account->LinkEmail(Email2, Password, [&T, Next](bool bOk, const FString& Message)
			{
				T.Check(bOk && T.Account->HasEmailLinked(), FString::Printf(TEXT("a new email is linked to the Epic profile ('%s')"), *Message));
				Next();
			});
		});
	}

	void BuildLinkCheck(FAuthTest& T)
	{
		const FString Email2 = T.State->GetStringField(TEXT("email2"));
		const FString Password = T.State->GetStringField(TEXT("password"));
		const FString EpicProfile = T.State->GetStringField(TEXT("epic_profile"));
		T.AddWaitSettled(40.f);
		T.AddSignIn(Email2, Password, true, FString(), TEXT("sign in with the linked email"));
		T.Steps.Add([&T, EpicProfile](FNext Next)
		{
			UCSAccountSubsystem* A = T.Account.Get();
			T.Check(A->GetProfileId() == EpicProfile && A->HasEpicLinked() && A->HasEmailLinked(),
				FString::Printf(TEXT("the email opens the Epic profile '%s' (%s)"), *A->GetNickname(), *A->GetProfileId()));
			Next();
		});
		T.AddSignOut();
	}
}

FString CSAuthSelfTest::Stage()
{
	FString Stage;
	FParse::Value(FCommandLine::Get(), TEXT("cstestemailauth="), Stage);
	return Stage;
}

void CSAuthSelfTest::Start(UCSAccountSubsystem* Account)
{
	static bool bStarted = false;
	const FString TestStage = Stage();
	if (bStarted || TestStage.IsEmpty() || !Account)
	{
		return;
	}
	bStarted = true;
	TSharedRef<FAuthTest> Test = MakeShared<FAuthTest>(Account, TestStage);
	if (TestStage == TEXT("register"))			BuildRegister(*Test);
	else if (TestStage == TEXT("relaunch"))		BuildRelaunch(*Test);
	else if (TestStage == TEXT("after"))		BuildAfter(*Test);
	else if (TestStage == TEXT("link"))			BuildLink(*Test);
	else if (TestStage == TEXT("linkcheck"))	BuildLinkCheck(*Test);
	else
	{
		UE_LOG(LogCSAuth, Error, TEXT("EMAIL AUTH TEST RESULT: unknown stage '%s' -> EMAIL AUTH BROKEN"), *TestStage);
		return;
	}
	UE_LOG(LogCSAuth, Log, TEXT("EMAIL AUTH TEST: stage %s, %d steps."), *TestStage, Test->Steps.Num());
	// The steps capture the test by reference; it lives until the process quits.
	static TSharedPtr<FAuthTest> Keep;
	Keep = Test;
	Test->Run();
}

#else

FString CSAuthSelfTest::Stage() { return FString(); }
void CSAuthSelfTest::Start(UCSAccountSubsystem*) {}

#endif
