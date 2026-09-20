// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class CSFusionTarget : TargetRules
{
	public CSFusionTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;

		// Photon Fusion Unreal SDK 3.0 requires C++17 or newer.
		CppStandard = CppStandardVersion.Cpp20;

		ExtraModuleNames.AddRange(new string[] { "CSFusion" });
	}
}
