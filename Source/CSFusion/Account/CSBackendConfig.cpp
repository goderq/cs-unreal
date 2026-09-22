// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Account/CSBackendConfig.h"

#include "Core/CSLog.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Class.h"

namespace
{
	FCSBackendConfig GConfigValues;
	bool GLoaded = false;

	/** Environment first (build machines), then the local ini file. */
	FString Value(const FConfigFile& File, const TCHAR* Section, const TCHAR* Key, const TCHAR* EnvName)
	{
		FString FromEnv = FPlatformMisc::GetEnvironmentVariable(EnvName);
		if (!FromEnv.IsEmpty())
		{
			return FromEnv.TrimStartAndEnd();
		}
		FString FromFile;
		File.GetString(Section, Key, FromFile);
		return FromFile.TrimStartAndEnd();
	}

	/** Keys never go to the log; this is what is safe to print. */
	FString Mask(const FString& Secret)
	{
		return Secret.IsEmpty() ? TEXT("(not set)") : FString::Printf(TEXT("set, %d chars"), Secret.Len());
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

	const FString Path = FPaths::ProjectConfigDir() / TEXT("Backend.ini");
	FConfigFile File;
	if (FPaths::FileExists(Path))
	{
		File.Read(Path);
	}

	FCSBackendConfig& C = GConfigValues;
	C.SupabaseUrl = Value(File, TEXT("Supabase"), TEXT("Url"), TEXT("CS_SUPABASE_URL"));
	C.SupabaseAnonKey = Value(File, TEXT("Supabase"), TEXT("AnonKey"), TEXT("CS_SUPABASE_ANON_KEY"));
	C.ProductId = Value(File, TEXT("EOS"), TEXT("ProductId"), TEXT("CS_EOS_PRODUCT_ID"));
	C.SandboxId = Value(File, TEXT("EOS"), TEXT("SandboxId"), TEXT("CS_EOS_SANDBOX_ID"));
	C.DeploymentId = Value(File, TEXT("EOS"), TEXT("DeploymentId"), TEXT("CS_EOS_DEPLOYMENT_ID"));
	C.ClientId = Value(File, TEXT("EOS"), TEXT("ClientId"), TEXT("CS_EOS_CLIENT_ID"));
	C.ClientSecret = Value(File, TEXT("EOS"), TEXT("ClientSecret"), TEXT("CS_EOS_CLIENT_SECRET"));
	C.EncryptionKey = Value(File, TEXT("EOS"), TEXT("EncryptionKey"), TEXT("CS_EOS_ENCRYPTION_KEY"));

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
	// runtime (UEOSSettings, section below). Writing it here keeps the keys
	// out of the repository: GConfig is the in-memory copy, and nothing is
	// flushed to disk.
	static const TCHAR* EOSSection = TEXT("/Script/OnlineSubsystemEOS.EOSSettings");
	const FString Artifact = FString::Printf(
		TEXT("(ArtifactName=\"%s\",ClientId=\"%s\",ClientSecret=\"%s\",ProductId=\"%s\",SandboxId=\"%s\",DeploymentId=\"%s\",ClientEncryptionKey=\"%s\")"),
		ArtifactName(), *C.ClientId, *C.ClientSecret, *C.ProductId, *C.SandboxId, *C.DeploymentId, *C.EncryptionKey);

	TArray<FString> Artifacts;
	Artifacts.Add(Artifact);
	GConfig->SetArray(EOSSection, TEXT("Artifacts"), Artifacts, GEngineIni);
	GConfig->SetString(EOSSection, TEXT("DefaultArtifactName"), ArtifactName(), GEngineIni);

	// The settings object caches its config in the class default object, so it
	// has to re-read it. Done by name to avoid a compile-time dependency on
	// the plugin module.
	if (UClass* SettingsClass = FindObject<UClass>(nullptr, TEXT("/Script/OnlineSubsystemEOS.EOSSettings")))
	{
		if (UObject* Settings = SettingsClass->GetDefaultObject())
		{
			Settings->ReloadConfig();
			UE_LOG(LogCS, Log, TEXT("EOS artifact '%s' applied to the engine config."), ArtifactName());
		}
	}
	else
	{
		UE_LOG(LogCS, Warning, TEXT("OnlineSubsystemEOS is not loaded; EOS settings were not applied."));
	}
}
