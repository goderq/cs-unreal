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
		// Photon Fusion 3 (Unreal SDK) is a hard dependency.
		//
		// An earlier revision tried to make the SDK optional by guarding every
		// Fusion declaration with #if CS_WITH_FUSION. That does not work, and
		// fails silently, which is worse than failing loudly:
		//
		//   UnrealHeaderTool only understands a fixed set of preprocessor
		//   conditions (CPP, !CPP, 0, 1, WITH_EDITOR, WITH_EDITORONLY_DATA,
		//   WITH_ENGINE, WITH_COREUOBJECT, WITH_HOT_RELOAD, WITH_VERSE_VM,
		//   WITH_VERSE_BPVM, WITH_TESTS). Anything else is classified
		//   UhtCompilerDirective.Unrecognized, and
		//   UhtHeaderFileParser.IncludeCurrentCompilerDirective() then skips
		//   the whole block. A UPROPERTY inside it is never registered with
		//   the reflection system (so the GC can collect it), a UFUNCTION is
		//   never registered (so AddDynamic fails at runtime), and a
		//   SEND_FUSIONRPC is never seen by PhotonFusionUbtPlugin (so the send
		//   function is declared but never defined -> link error).
		//
		// So: the SDK is required, CS_WITH_FUSION is always 1, and no reflected
		// declaration is ever wrapped in a project-specific #if.
		//
		// Offline play is still supported, but as a RUNTIME path: with no room
		// joined, UCSAuthority::IsGameAuthority() returns true and every
		// Request* wrapper calls its _Receive handler directly instead of
		// sending an RPC. See docs/ARCHITECTURE.md.
		//
		// The SDK cannot be committed (Photon licence), so a fresh clone must
		// install it first - see docs/PHOTON_SETUP.md.
		// ------------------------------------------------------------------
		string ProjectRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", ".."));
		string FusionUPlugin = Path.Combine(ProjectRoot, "Plugins", "PhotonFusion", "PhotonFusion.uplugin");

		if (!File.Exists(FusionUPlugin))
		{
			throw new BuildException(
				"\n================================================================\n" +
				"Photon Fusion 3 SDK not found.\n\n" +
				"Expected: " + FusionUPlugin + "\n\n" +
				"Download 'Fusion Unreal 5.8 SDK 3.0' from the Photon Dashboard\n" +
				"and unpack the PhotonFusion folder into <Project>/Plugins/.\n" +
				"Full instructions: docs/PHOTON_SETUP.md\n" +
				"================================================================\n");
		}

		// PhotonFusion.Build.cs re-exports Core, Engine, DeveloperSettings and
		// PhysicsCore, so those do not have to be repeated here.
		PublicDependencyModuleNames.Add("PhotonFusion");
		PublicDefinitions.Add("CS_WITH_FUSION=1");

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
