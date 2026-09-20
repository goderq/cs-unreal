// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class CSFusionTarget : TargetRules
{
	public CSFusionTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;

		// Must match the installed engine, which is built with Latest. A lower
		// BuildSettingsVersion downgrades CppCompileWarningSettings and UBT then
		// refuses the target: "modifies the values of properties ... has build
		// products in common with UnrealEditor".
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;


		ExtraModuleNames.AddRange(new string[] { "CSFusion" });
	}
}
