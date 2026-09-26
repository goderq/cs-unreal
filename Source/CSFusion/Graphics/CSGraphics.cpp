// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Graphics/CSGraphics.h"

#include "Containers/Ticker.h"
#include "Core/CSLog.h"
#include "Settings/CSGameUserSettings.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "DynamicRHI.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "RenderUtils.h"
#include "RHI.h"
#include "RHIStats.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

namespace
{
	// UDLSSLibrary in the DLSS plugin's DLSSBlueprint module, and its enum.
	const TCHAR* DLSSLibraryPath = TEXT("/Script/DLSSBlueprint.DLSSLibrary");
	const TCHAR* DLSSSupportEnumPath = TEXT("/Script/DLSSBlueprint.UDLSSSupport");

	// UDLSSMode values (DLSSLibrary.h): Off, Auto, DLAA, UltraQuality, Quality, Balanced, Performance, UltraPerformance.
	constexpr uint8 DLSSModeFor[] = { 2, 4, 5, 6, 7 };

	// The DLSS modules load at PostEngineInit, after the engine has applied the
	// saved settings once; asking them earlier only logs errors.
	bool GEngineReady = false;

	UClass* DLSSLibrary()
	{
		return GEngineReady ? FindObject<UClass>(nullptr, DLSSLibraryPath) : nullptr;
	}

	/**
	 * Calls a static function of the DLSS Blueprint library by name. Arguments
	 * and results are set and read through the function's own properties, so
	 * nothing here depends on the plugin's headers.
	 */
	bool CallDLSS(const TCHAR* Name, TFunctionRef<void(UFunction*, uint8*)> SetArgs, TFunctionRef<void(UFunction*, uint8*)> ReadResult)
	{
		UClass* Library = DLSSLibrary();
		UFunction* Function = Library ? Library->FindFunctionByName(Name) : nullptr;
		if (!Function)
		{
			return false;
		}
		TArray<uint8, TInlineAllocator<64>> Buffer;
		Buffer.SetNumZeroed(FMath::Max<int32>(Function->ParmsSize, 1));
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Buffer.GetData());
		}
		SetArgs(Function, Buffer.GetData());
		Library->GetDefaultObject()->ProcessEvent(Function, Buffer.GetData());
		ReadResult(Function, Buffer.GetData());
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Buffer.GetData());
		}
		return true;
	}

	void NoArgs(UFunction*, uint8*) {}

	int64 ReadInteger(const FProperty* Property, const uint8* Buffer)
	{
		if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
		{
			return Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Enum->ContainerPtrToValuePtr<void>(Buffer));
		}
		if (const FNumericProperty* Number = CastField<FNumericProperty>(Property))
		{
			return Number->GetSignedIntPropertyValue(Number->ContainerPtrToValuePtr<void>(Buffer));
		}
		return 0;
	}

	bool DLSSBool(const TCHAR* Name, bool bFallback = false)
	{
		bool bResult = bFallback;
		CallDLSS(Name, NoArgs, [&bResult](UFunction* F, uint8* Buffer)
		{
			if (const FBoolProperty* Ret = CastField<FBoolProperty>(F->GetReturnProperty()))
			{
				bResult = Ret->GetPropertyValue_InContainer(Buffer);
			}
		});
		return bResult;
	}

	// SetByCode: r.AntiAliasingMethod and r.Lumen.HardwareRayTracing come from
	// the project settings, which outrank SetByGameSetting - a game setting was
	// silently refused (graphics self-test, phase 5). The console still wins.
	void SetCVar(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			CVar->Set(Value, ECVF_SetByCode);
		}
	}
}

namespace CSGraphics
{
	const FCSGraphicsCaps& GetCaps()
	{
		static FCSGraphicsCaps Caps;
		static bool bQueried = false;
		static bool bQueriedReady = false;
		if ((bQueried && bQueriedReady == GEngineReady) || !GIsRHIInitialized)
		{
			return Caps;
		}
		bQueried = true;
		bQueriedReady = GEngineReady;
		Caps = FCSGraphicsCaps();

		Caps.Adapter = GRHIAdapterName;
		FTextureMemoryStats Memory;
		RHIGetTextureMemoryStats(Memory);
		Caps.VideoMemoryMB = Memory.DedicatedVideoMemory > 0 ? Memory.DedicatedVideoMemory / (1024 * 1024) : 0;
		Caps.bSM6 = GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM6;
		Caps.bRayTracing = IsRayTracingAllowed();

		Caps.bDLSSPlugin = DLSSLibrary() != nullptr;
		if (!GEngineReady)
		{
			Caps.DLSSStatus = TEXT("Not checked yet");
			return Caps;
		}
		if (!Caps.bDLSSPlugin)
		{
			Caps.DLSSStatus = TEXT("Plugin not installed");
		}
		else
		{
			Caps.bDLSS = DLSSBool(TEXT("IsDLSSSupported"));
			int64 Support = -1;
			CallDLSS(TEXT("QueryDLSSSupport"), NoArgs, [&Support](UFunction* F, uint8* Buffer)
			{
				Support = ReadInteger(F->GetReturnProperty(), Buffer);
			});
			const UEnum* SupportEnum = FindObject<UEnum>(nullptr, DLSSSupportEnumPath);
			Caps.DLSSStatus = SupportEnum && Support >= 0 ? SupportEnum->GetDisplayNameTextByValue(Support).ToString()
				: (Caps.bDLSS ? TEXT("Supported") : TEXT("Not supported"));
		}
		UE_LOG(LogCS, Log, TEXT("Graphics: %s"), *Describe(Caps));
		return Caps;
	}

	float ScreenPercentage(ECSRenderScale Scale)
	{
		// The ratios DLSS uses; TSR and TAA take the same ones.
		static const float Percent[] = { 100.f, 66.7f, 58.f, 50.f, 33.3f };
		return Percent[FMath::Clamp(static_cast<int32>(Scale), 0, 4)];
	}

	ECSUpscaler Effective(ECSUpscaler Wanted)
	{
		return (Wanted == ECSUpscaler::DLSS && !GetCaps().bDLSS) ? ECSUpscaler::TSR : Wanted;
	}

	void ApplyRendering(ECSUpscaler Upscaler, ECSRenderScale Scale, bool bRayTracing)
	{
		const FCSGraphicsCaps& Caps = GetCaps();
		const ECSUpscaler Used = Effective(Upscaler);

		// DLSS takes over the temporal upscaler's place, so the method under it stays temporal.
		SetCVar(TEXT("r.AntiAliasingMethod"), Used == ECSUpscaler::TAA ? 2 : 4);
		if (Caps.bDLSSPlugin)
		{
			const bool bDLSS = Used == ECSUpscaler::DLSS;
			bool bModeOk = true;
			if (bDLSS)
			{
				CallDLSS(TEXT("IsDLSSModeSupported"), [Scale](UFunction* F, uint8* Buffer)
				{
					for (TFieldIterator<FProperty> It(F); It; ++It)
					{
						if (It->HasAnyPropertyFlags(CPF_Parm) && !It->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm))
						{
							const FEnumProperty* Enum = CastField<FEnumProperty>(*It);
							const FNumericProperty* Number = Enum ? Enum->GetUnderlyingProperty() : CastField<FNumericProperty>(*It);
							if (Number)
							{
								Number->SetIntPropertyValue(It->ContainerPtrToValuePtr<void>(Buffer), static_cast<int64>(DLSSModeFor[static_cast<int32>(Scale)]));
							}
							break;
						}
					}
				}, [&bModeOk](UFunction* F, uint8* Buffer)
				{
					if (const FBoolProperty* Ret = CastField<FBoolProperty>(F->GetReturnProperty()))
					{
						bModeOk = Ret->GetPropertyValue_InContainer(Buffer);
					}
				});
			}
			CallDLSS(TEXT("EnableDLSS"), [bDLSS](UFunction* F, uint8* Buffer)
			{
				if (const FBoolProperty* Arg = CastField<FBoolProperty>(F->FindPropertyByName(TEXT("bEnabled"))))
				{
					Arg->SetPropertyValue_InContainer(Buffer, bDLSS);
				}
			}, NoArgs);
			if (bDLSS && !bModeOk)
			{
				UE_LOG(LogCS, Warning, TEXT("Graphics: DLSS reports mode %d as unsupported at this resolution; it picks the nearest itself."), static_cast<int32>(Scale));
			}
		}

		const bool bRT = bRayTracing && Caps.bRayTracing;
		SetCVar(TEXT("r.RayTracing.Enable"), bRT ? 1 : 0);
		SetCVar(TEXT("r.Lumen.HardwareRayTracing"), bRT ? 1 : 0);

		static const TCHAR* Names[] = { TEXT("DLSS"), TEXT("TSR"), TEXT("TAA") };
		UE_LOG(LogCS, Log, TEXT("Graphics: upscaler %s (asked %s), render %.0f%%, ray tracing %s."),
			Names[static_cast<int32>(Used)], Names[static_cast<int32>(Upscaler)], ScreenPercentage(Scale),
			bRT ? TEXT("on") : (bRayTracing ? TEXT("off (not supported here)") : TEXT("off")));
	}

	void HandlePostEngineInit()
	{
		// One frame later: the DLSS module marks itself ready in its own
		// PostEngineInit handler, which may run after this one.
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
		{
			GEngineReady = true;
			GetCaps();
			// Apply again now that DLSS can answer (the first apply ran without it).
			// Not in the editor: its viewports keep the editor's own settings.
			UCSGameUserSettings* Settings = GIsEditor ? nullptr : UCSGameUserSettings::Get();
			if (Settings)
			{
				Settings->ApplyNonResolutionSettings();
			}
			return false;
		}));
	}

	bool IsDLSSRunning()
	{
		return GetCaps().bDLSSPlugin && DLSSBool(TEXT("IsDLSSEnabled"));
	}

	FString Describe(const FCSGraphicsCaps& Caps)
	{
		return FString::Printf(TEXT("%s, %lld MB video memory, %s, hardware ray tracing %s, DLSS %s"),
			*Caps.Adapter, Caps.VideoMemoryMB, Caps.bSM6 ? TEXT("SM6") : TEXT("SM5"),
			Caps.bRayTracing ? TEXT("available") : TEXT("not available"), *Caps.DLSSStatus);
	}
}
