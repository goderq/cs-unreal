// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Account/CSSessionStore.h"

#include "Core/CSLog.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <wincrypt.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
	// Extra DPAPI entropy: another program running as the same Windows user has to
	// know it too (it is no secret - it only keeps casual tools from decrypting).
	const char SessionEntropy[] = "CS-Fusion session v1";

	bool Protect(const TArray<uint8>& In, TArray<uint8>& Out, bool bEncrypt)
	{
#if PLATFORM_WINDOWS
		DATA_BLOB Input{ static_cast<DWORD>(In.Num()), const_cast<BYTE*>(In.GetData()) };
		DATA_BLOB Entropy{ static_cast<DWORD>(sizeof(SessionEntropy) - 1), (BYTE*)SessionEntropy };
		DATA_BLOB Result{ 0, nullptr };
		const BOOL bOk = bEncrypt
			? CryptProtectData(&Input, L"CS-Fusion", &Entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &Result)
			: CryptUnprotectData(&Input, nullptr, &Entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &Result);
		if (!bOk || !Result.pbData)
		{
			return false;
		}
		Out.SetNumUninitialized(Result.cbData);
		FMemory::Memcpy(Out.GetData(), Result.pbData, Result.cbData);
		SecureZeroMemory(Result.pbData, Result.cbData);
		LocalFree(Result.pbData);
		return true;
#else
		return false;
#endif
	}
}

FString FCSSessionStore::FilePath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Account"), TEXT("session.dat"));
}

bool FCSSessionStore::Save(const FCSStoredSession& Session)
{
	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("method"), Session.Method);
	Json->SetStringField(TEXT("email"), Session.Email);
	Json->SetStringField(TEXT("refresh_token"), Session.RefreshToken);
	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	FJsonSerializer::Serialize(Json, Writer);

	FTCHARToUTF8 Utf8(*Text);
	TArray<uint8> Plain(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	TArray<uint8> Sealed;
	const bool bOk = Protect(Plain, Sealed, true);
	FMemory::Memzero(Plain.GetData(), Plain.Num());
	if (!bOk)
	{
		UE_LOG(LogCSAuth, Warning, TEXT("Accounts: the session could not be encrypted - it is not remembered."));
		Clear();
		return false;
	}
	return FFileHelper::SaveArrayToFile(Sealed, *FilePath());
}

bool FCSSessionStore::Load(FCSStoredSession& Out)
{
	Out = FCSStoredSession();
	TArray<uint8> Sealed;
	if (!FFileHelper::LoadFileToArray(Sealed, *FilePath(), FILEREAD_Silent) || Sealed.Num() == 0)
	{
		return false;
	}
	TArray<uint8> Plain;
	if (!Protect(Sealed, Plain, false))
	{
		// Another Windows user or PC, or a damaged file: forget it.
		UE_LOG(LogCSAuth, Warning, TEXT("Accounts: the saved session cannot be read here - forgetting it."));
		Clear();
		return false;
	}
	const FString Text(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Plain.GetData()), Plain.Num()).Get(), Plain.Num());
	FMemory::Memzero(Plain.GetData(), Plain.Num());

	TSharedPtr<FJsonObject> Json;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		Clear();
		return false;
	}
	Json->TryGetStringField(TEXT("method"), Out.Method);
	Json->TryGetStringField(TEXT("email"), Out.Email);
	Json->TryGetStringField(TEXT("refresh_token"), Out.RefreshToken);
	return !Out.Method.IsEmpty();
}

void FCSSessionStore::Clear()
{
	IFileManager::Get().Delete(*FilePath(), false, true, true);
}
