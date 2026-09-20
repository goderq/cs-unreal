// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class CSFusionEditorTarget : TargetRules
{
	public CSFusionEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;

		CppStandard = CppStandardVersion.Cpp20;

		ExtraModuleNames.AddRange(new string[] { "CSFusion" });
	}
}
