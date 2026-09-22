// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Where the account credentials live.
//
// This repository is public, so no key is committed. The game reads
// Config/Backend.ini (git-ignored, see Config/Backend.ini.example) at module
// startup and, for EOS, writes the values straight into the in-memory engine
// config that the EOS plugin reads - nothing is written back to disk.
//
// Environment variables of the same name override the file, which is what a
// build machine uses:
//   CS_SUPABASE_URL, CS_SUPABASE_ANON_KEY,
//   CS_EOS_PRODUCT_ID, CS_EOS_SANDBOX_ID, CS_EOS_DEPLOYMENT_ID,
//   CS_EOS_CLIENT_ID, CS_EOS_CLIENT_SECRET, CS_EOS_ENCRYPTION_KEY
//
// With no configuration the game still builds and runs: the login screen says
// that accounts are not configured, and -noaccount skips it (self-tests).

#pragma once

#include "CoreMinimal.h"

struct CSFUSION_API FCSBackendConfig
{
	// --- Supabase (accounts, stats) ---
	FString SupabaseUrl;
	FString SupabaseAnonKey;

	// --- Epic Online Services ---
	FString ProductId;
	FString SandboxId;
	FString DeploymentId;
	FString ClientId;
	FString ClientSecret;
	FString EncryptionKey;

	/** The artifact name the EOS plugin looks up; must match DefaultEngine.ini. */
	static const TCHAR* ArtifactName() { return TEXT("CSFusion"); }

	bool HasSupabase() const { return !SupabaseUrl.IsEmpty() && !SupabaseAnonKey.IsEmpty(); }
	bool HasEOS() const { return !ProductId.IsEmpty() && !ClientId.IsEmpty() && !DeploymentId.IsEmpty(); }

	FString FunctionUrl(const TCHAR* FunctionName) const
	{
		return FString::Printf(TEXT("%s/functions/v1/%s"), *SupabaseUrl.TrimEnd().TrimChar('/'), FunctionName);
	}

	FString RestUrl(const FString& Path) const
	{
		return FString::Printf(TEXT("%s/rest/v1/%s"), *SupabaseUrl.TrimEnd().TrimChar('/'), *Path);
	}

	/** Loaded once at module startup. */
	static const FCSBackendConfig& Get();

	/**
	 * Reads Config/Backend.ini and the environment, then pushes the EOS values
	 * into the engine config so the EOS plugin picks them up. Called from
	 * FCSFusionModule::StartupModule.
	 */
	static void LoadAndApply();
};
