// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v2.0 phase 3 (AUDIT C10): builds the project's Niagara systems from the
// engine's lightweight (stateless) templates. Editor only:
//
//   UnrealEditor-Cmd.exe CSFusion.uproject -run=CSFXBuilder            build /Game/FX/Niagara
//   UnrealEditor-Cmd.exe CSFusion.uproject -run=CSFXBuilder -dump      print the templates
//
// Lightweight emitters keep their modules as plain objects with properties,
// so every setting here is a property value in UE's text format - the same
// text the details panel copies. Idempotent: each run rebuilds the systems
// from the template and overwrites them.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CSFXBuilderCommandlet.generated.h"

UCLASS()
class UCSFXBuilderCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UCSFXBuilderCommandlet();

	virtual int32 Main(const FString& Params) override;
};
