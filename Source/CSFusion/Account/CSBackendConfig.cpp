// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Account/CSBackendConfig.h"

#include "Core/CSLog.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Class.h"

namespace
{
	FCSBackendConfig GConfigValues;
	bool GLoaded = false;
	FDelegateHandle GModulesChangedHandle;

	/**
	 * Backend.ini is read by hand rather than through FConfigFile: the engine
	 * parser ends a value at "//", which eats every URL after "https:".
	 * Comments are whole lines starting with ';' or '#'.
	 */
	void ReadIni(const FString& Path, TMap<FString, FString>& OutPairs)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			return;
		}

		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		FString Section;
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (Line.IsEmpty() || Line.StartsWith(TEXT(";")) || Line.StartsWith(TEXT("#")))
			{
				continue;
			}
			if (Line.StartsWith(TEXT("[")) && Line.EndsWith(TEXT("]")))
			{
				Section = Line.Mid(1, Line.Len() - 2).TrimStartAndEnd();
				continue;
			}
			int32 Equals = INDEX_NONE;
			if (!Line.FindChar(TEXT('='), Equals))
			{
				continue;
			}
			FString Key = Line.Left(Equals).TrimStartAndEnd();
			FString Value = Line.Mid(Equals + 1).TrimStartAndEnd();
			Value.TrimQuotesInline();
			OutPairs.Add(FString::Printf(TEXT("%s/%s"), *Section, *Key).ToLower(), Value);
		}
	}

	/** Environment first (build machines), then the local ini file. */
	FString Value(const TMap<FString, FString>& Pairs, const TCHAR* Section, const TCHAR* Key, const TCHAR* EnvName)
	{
		FString FromEnv = FPlatformMisc::GetEnvironmentVariable(EnvName);
		if (!FromEnv.IsEmpty())
		{
			return FromEnv.TrimStartAndEnd();
		}
		const FString* FromFile = Pairs.Find(FString::Printf(TEXT("%s/%s"), Section, Key).ToLower());
		return FromFile ? *FromFile : FString();
	}

	/** Keys never go to the log; this is what is safe to print. */
	FString Mask(const FString& Secret)
	{
		return Secret.IsEmpty() ? TEXT("(not set)") : FString::Printf(TEXT("set, %d chars"), Secret.Len());
	}

	/**
	 * Writes the artifact into the in-memory engine config the EOS plugin reads.
	 * Returns false while the plugin is not loaded yet: its settings object does
	 * not exist then, so its cached defaults cannot be refreshed.
	 */
	bool ApplyEOSArtifact(const FCSBackendConfig& C)
	{
		static const TCHAR* EOSSection = TEXT("/Script/OnlineSubsystemEOS.EOSSettings");
		const FString Artifact = FString::Printf(
			TEXT("(ArtifactName=\"%s\",ClientId=\"%s\",ClientSecret=\"%s\",ProductId=\"%s\",SandboxId=\"%s\",DeploymentId=\"%s\",ClientEncryptionKey=\"%s\")"),
			FCSBackendConfig::ArtifactName(), *C.ClientId, *C.ClientSecret, *C.ProductId, *C.SandboxId, *C.DeploymentId, *C.EncryptionKey);

		TArray<FString> Artifacts;
		Artifacts.Add(Artifact);
		GConfig->SetArray(EOSSection, TEXT("Artifacts"), Artifacts, GEngineIni);
		GConfig->SetString(EOSSection, TEXT("DefaultArtifactName"), FCSBackendConfig::ArtifactName(), GEngineIni);

		// The settings object caches its config in the class default object, so
		// it has to re-read it. Done by name to avoid a compile-time dependency
		// on the plugin module.
		if (UClass* SettingsClass = FindObject<UClass>(nullptr, TEXT("/Script/OnlineSubsystemEOS.EOSSettings")))
		{
			if (UObject* Settings = SettingsClass->GetDefaultObject())
			{
				Settings->ReloadConfig();
				UE_LOG(LogCS, Log, TEXT("EOS artifact '%s' applied to the engine config."), FCSBackendConfig::ArtifactName());
				return true;
			}
		}
		return false;
	}
}

const FCSBackendConfig& FCSBackendConfig::Get()
{
	if (!GLoaded)
	{
		LoadAndApply();
	}
	return GConfigValues;
}

void FCSBackendConfig::LoadAndApply()
{
	GLoaded = true;

	TMap<FString, FString> Pairs;
	ReadIni(FPaths::ProjectConfigDir() / TEXT("Backend.ini"), Pairs);

	FCSBackendConfig& C = GConfigValues;
	C.SupabaseUrl = Value(Pairs, TEXT("Supabase"), TEXT("Url"), TEXT("CS_SUPABASE_URL"));
	C.SupabaseAnonKey = Value(Pairs, TEXT("Supabase"), TEXT("AnonKey"), TEXT("CS_SUPABASE_ANON_KEY"));
	C.ProductId = Value(Pairs, TEXT("EOS"), TEXT("ProductId"), TEXT("CS_EOS_PRODUCT_ID"));
	C.SandboxId = Value(Pairs, TEXT("EOS"), TEXT("SandboxId"), TEXT("CS_EOS_SANDBOX_ID"));
	C.DeploymentId = Value(Pairs, TEXT("EOS"), TEXT("DeploymentId"), TEXT("CS_EOS_DEPLOYMENT_ID"));
	C.ClientId = Value(Pairs, TEXT("EOS"), TEXT("ClientId"), TEXT("CS_EOS_CLIENT_ID"));
	C.ClientSecret = Value(Pairs, TEXT("EOS"), TEXT("ClientSecret"), TEXT("CS_EOS_CLIENT_SECRET"));
	C.EncryptionKey = Value(Pairs, TEXT("EOS"), TEXT("EncryptionKey"), TEXT("CS_EOS_ENCRYPTION_KEY"));
	C.PhotonTestAppId = Value(Pairs, TEXT("Photon"), TEXT("TestAppId"), TEXT("CS_PHOTON_TEST_APP_ID"));

	UE_LOG(LogCS, Log, TEXT("Backend config: Supabase %s (key %s), EOS product %s (client %s, secret %s)."),
		C.SupabaseUrl.IsEmpty() ? TEXT("(not set)") : *C.SupabaseUrl, *Mask(C.SupabaseAnonKey),
		C.ProductId.IsEmpty() ? TEXT("(not set)") : *C.ProductId,
		C.ClientId.IsEmpty() ? TEXT("(not set)") : *C.ClientId, *Mask(C.ClientSecret));

	if (!C.HasEOS())
	{
		UE_LOG(LogCS, Warning,
			TEXT("EOS is not configured: sign-in is unavailable. Fill Config/Backend.ini ")
			TEXT("(template: Config/Backend.ini.example, guide: docs/ACCOUNTS.md)."));
		return;
	}

	// The EOS plugin reads its artifact list from the engine config at
	// runtime (UEOSSettings). Writing it here keeps the keys out of the
	// repository: GConfig is the in-memory copy, and nothing is flushed to
	// disk.
	if (ApplyEOSArtifact(C))
	{
		return;
	}

	// This game module starts before the EOS plugin is loaded, so the settings
	// object does not exist yet. The values are already in GConfig; re-apply
	// as soon as the plugin arrives so its cached defaults are refreshed too.
	if (!GModulesChangedHandle.IsValid())
	{
		GModulesChangedHandle = FModuleManager::Get().OnModulesChanged().AddLambda(
			[](FName ModuleName, EModuleChangeReason Reason)
			{
				if (Reason != EModuleChangeReason::ModuleLoaded || ModuleName != TEXT("OnlineSubsystemEOS"))
				{
					return;
				}
				if (ApplyEOSArtifact(GConfigValues))
				{
					FModuleManager::Get().OnModulesChanged().Remove(GModulesChangedHandle);
					GModulesChangedHandle.Reset();
				}
			});
	}
	UE_LOG(LogCS, Log, TEXT("EOS settings are staged; they apply once OnlineSubsystemEOS loads."));
}
