// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class CSFusion : ModuleRules
{
	public CSFusion(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		// ------------------------------------------------------------------
		// Photon Fusion 3 (Unreal SDK) detection.
		//
		// The Fusion SDK is distributed from the Photon Dashboard as a 7z that
		// is unpacked into <Project>/Plugins/PhotonFusion. It cannot be
		// redistributed inside this repository, so the gameplay module is
		// written to build BOTH with and without it:
		//
		//   CS_WITH_FUSION = 1 -> networking runs through Photon Fusion 3.
		//   CS_WITH_FUSION = 0 -> the project still compiles and runs
		//                         standalone/offline so gameplay can be worked
		//                         on before the SDK is installed. Every Fusion
		//                         RPC falls back to a direct local call.
		//
		// See docs/PHOTON_SETUP.md for the installation steps.
		// ------------------------------------------------------------------
		string ProjectRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", ".."));
		string FusionUPlugin = Path.Combine(ProjectRoot, "Plugins", "PhotonFusion", "PhotonFusion.uplugin");
		bool bHasPhotonFusion = File.Exists(FusionUPlugin);

		if (bHasPhotonFusion)
		{
			// PhotonFusion.Build.cs re-exports Core, Engine, DeveloperSettings
			// and PhysicsCore, so those do not have to be repeated here.
			PublicDependencyModuleNames.Add("PhotonFusion");
			PublicDefinitions.Add("CS_WITH_FUSION=1");
			System.Console.WriteLine("[CSFusion] Photon Fusion 3 SDK found -> CS_WITH_FUSION=1");
		}
		else
		{
			PublicDefinitions.Add("CS_WITH_FUSION=0");
			System.Console.WriteLine(
				"[CSFusion] Photon Fusion 3 SDK NOT found at " + FusionUPlugin +
				" -> building in OFFLINE mode (CS_WITH_FUSION=0). See docs/PHOTON_SETUP.md.");
		}

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"NetCore",
			"PhysicsCore",
			"DeveloperSettings",
			"GameplayTags",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"UMG",
			"AIModule",
			"NavigationSystem",
			"GameplayTasks",
		});

		PublicIncludePaths.Add(ModuleDirectory);
	}
}
