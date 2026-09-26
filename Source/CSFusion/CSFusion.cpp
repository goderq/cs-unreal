// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "CSFusion.h"
#include "Account/CSBackendConfig.h"
#include "Core/CSLog.h"
#include "Graphics/CSGraphics.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FCSFusionModule"

void FCSFusionModule::StartupModule()
{
	// Accounts: pull Config/Backend.ini into the engine config before anything
	// asks EOS for its settings (docs/ACCOUNTS.md).
	FCSBackendConfig::LoadAndApply();

	// v2.0 phase 5: DLSS (an optional plugin) answers only after PostEngineInit.
	FCoreDelegates::GetOnPostEngineInit().AddStatic(&CSGraphics::HandlePostEngineInit);

#if CS_WITH_FUSION
	UE_LOG(LogCS, Log, TEXT("CSFusion module started (Photon Fusion 3 backend ENABLED)."));
#else
	UE_LOG(LogCS, Warning,
		TEXT("CSFusion module started (Photon Fusion 3 backend DISABLED - offline fallback). ")
		TEXT("Install the SDK into Plugins/PhotonFusion and rebuild. See docs/PHOTON_SETUP.md."));
#endif
}

void FCSFusionModule::ShutdownModule()
{
	UE_LOG(LogCS, Log, TEXT("CSFusion module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_PRIMARY_GAME_MODULE(FCSFusionModule, CSFusion, "CSFusion");
