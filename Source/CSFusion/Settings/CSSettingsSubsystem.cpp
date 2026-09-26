// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Settings/CSSettingsSubsystem.h"

#include "AudioDevice.h"
#include "Audio/CSAudio.h"
#include "Core/CSLog.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "Graphics/CSGraphics.h"
#include "Settings/CSGameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Rendering/SlateRenderer.h"

void UCSSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	LoadPreferences();
	ApplyAudio();
	ApplyVisual();

	// The engine applies GameUserSettings.ini on its own at startup
	// (UCSGameUserSettings adds the upscaler and ray tracing). The first-run
	// hardware pick happens a frame after PostEngineInit (CSGraphics).
	UE_LOG(LogCS, Log, TEXT("Settings loaded: sensitivity %.2f, FOV %.0f, master volume %.2f, %d key override(s)."),
		Preferences.MouseSensitivity, Preferences.FieldOfView, Preferences.MasterVolume, Preferences.KeyOverrides.Num());
}

UCSSettingsSubsystem* UCSSettingsSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UCSSettingsSubsystem>() : nullptr;
}

// ---------------------------------------------------------------------------
// Preferences
// ---------------------------------------------------------------------------

bool UCSSettingsSubsystem::LoadPreferencesFromSlot(const FString& Slot, FCSPlayerPreferences& Out, int32* OutSchema)
{
	if (!UGameplayStatics::DoesSaveGameExist(Slot, 0))
	{
		return false;
	}
	const UCSSettingsSave* Save = Cast<UCSSettingsSave>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	if (!Save)
	{
		UE_LOG(LogCS, Warning, TEXT("Settings save '%s' is unreadable; using defaults."), *Slot);
		return false;
	}
	// Schema migration: fields a file does not have keep their class
	// defaults, so a schema-1 file (before the C11 options) already reads as
	// schema 2 with those options at their defaults. Nothing to convert yet;
	// a field that changes meaning later is converted here, by SchemaVersion.
	Out = Save->Preferences;
	SanitizePreferences(Out);   // a hand-edited file must not produce absurd values
	if (OutSchema)
	{
		*OutSchema = Save->SchemaVersion;
	}
	return true;
}

bool UCSSettingsSubsystem::SavePreferencesToSlot(const FCSPlayerPreferences& In, const FString& Slot)
{
	UCSSettingsSave* Save = Cast<UCSSettingsSave>(UGameplayStatics::CreateSaveGameObject(UCSSettingsSave::StaticClass()));
	if (!Save)
	{
		return false;
	}
	Save->SchemaVersion = UCSSettingsSave::CurrentSchema;
	Save->Preferences = In;
	return UGameplayStatics::SaveGameToSlot(Save, Slot, 0);
}

void UCSSettingsSubsystem::LoadPreferences()
{
	Preferences = DefaultPreferences();
	int32 Schema = UCSSettingsSave::CurrentSchema;
	if (LoadPreferencesFromSlot(SlotName(), Preferences, &Schema) && Schema < UCSSettingsSave::CurrentSchema)
	{
		UE_LOG(LogCS, Log, TEXT("Settings save migrated from schema %d to %d."), Schema, UCSSettingsSave::CurrentSchema);
		SavePreferences();
	}
}

void UCSSettingsSubsystem::SavePreferences() const
{
	if (!SavePreferencesToSlot(Preferences, SlotName()))
	{
		UE_LOG(LogCS, Warning, TEXT("Failed to write settings save '%s'."), SlotName());
	}
}

void UCSSettingsSubsystem::ApplyVisual() const
{
	// Colour vision: the engine's correction (daltonisation) over the whole
	// frame, 3D and UI alike.
	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
	{
		static const EColorVisionDeficiency Types[] = { EColorVisionDeficiency::NormalVision, EColorVisionDeficiency::Protanope,
			EColorVisionDeficiency::Deuteranope, EColorVisionDeficiency::Tritanope };
		const EColorVisionDeficiency Type = Types[FMath::Clamp(Preferences.ColorVision, 0, 3)];
		FSlateApplication::Get().GetRenderer()->SetColorVisionDeficiencyType(Type, Type == EColorVisionDeficiency::NormalVision ? 0 : 10,
			/*bCorrectDeficiency*/ true, /*bShowCorrectionWithDeficiency*/ false);
		FSlateApplication::Get().SetApplicationScale(Preferences.UIScale);
	}
	if (IConsoleVariable* Blur = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlur.Amount")))
	{
		Blur->Set(Preferences.bMotionBlur ? 0.4f : 0.f, ECVF_SetByCode);
	}
}

void UCSSettingsSubsystem::SetPreferences(const FCSPlayerPreferences& NewPreferences, bool bSave)
{
	Preferences = NewPreferences;
	SanitizePreferences(Preferences);
	ApplyAudio();
	ApplyVisual();
	if (bSave)
	{
		SavePreferences();
	}

	UE_LOG(LogCS, Log, TEXT("Settings applied: sensitivity %.2f, FOV %.0f, volumes %.2f/%.2f/%.2f, %d key override(s)."),
		Preferences.MouseSensitivity, Preferences.FieldOfView,
		Preferences.MasterVolume, Preferences.MusicVolume, Preferences.EffectsVolume,
		Preferences.KeyOverrides.Num());

	OnPreferencesChanged.Broadcast();
}

FKey UCSSettingsSubsystem::GetKeyFor(FName BindingId, const FKey& Default) const
{
	const FKey* Override = Preferences.KeyOverrides.Find(BindingId);
	return (Override && Override->IsValid()) ? *Override : Default;
}

void UCSSettingsSubsystem::ApplyAudio() const
{
	// Master volume scales the whole mix; music and effects go through the
	// sound classes every generated sound is assigned to (see CSAudioSettings).
	CSAudio::ApplyVolumes(GetGameInstance(), Preferences.MusicVolume, Preferences.EffectsVolume);
	if (GEngine)
	{
		if (FAudioDeviceHandle Device = GEngine->GetMainAudioDevice())
		{
			Device->SetTransientPrimaryVolume(Preferences.MasterVolume);
		}
	}
}

// ---------------------------------------------------------------------------
// Graphics
// ---------------------------------------------------------------------------

FCSGraphicsSettings UCSSettingsSubsystem::DefaultGraphics()
{
	FCSGraphicsSettings Defaults;
	// DLSS where it works (Quality), otherwise TSR at native resolution.
	if (CSGraphics::GetCaps().bDLSS)
	{
		Defaults.Upscaler = static_cast<int32>(ECSUpscaler::DLSS);
		Defaults.RenderScale = static_cast<int32>(ECSRenderScale::Quality);
	}
	Defaults.Resolution = UGameUserSettings::GetDefaultResolution();
	if (Defaults.Resolution.X <= 0 || Defaults.Resolution.Y <= 0)
	{
		if (const UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
		{
			Defaults.Resolution = Settings->GetDesktopResolution();
		}
	}
	return Defaults;
}

namespace
{
	// ECSGraphicsGroup order.
	int32 GroupLevel(const Scalability::FQualityLevels& Q, int32 Group)
	{
		const int32 Levels[] = { Q.ViewDistanceQuality, Q.AntiAliasingQuality, Q.ShadowQuality, Q.GlobalIlluminationQuality,
			Q.ReflectionQuality, Q.PostProcessQuality, Q.TextureQuality, Q.EffectsQuality, Q.ShadingQuality };
		return Levels[Group];
	}
}

FCSGraphicsSettings UCSSettingsSubsystem::GetGraphics() const
{
	FCSGraphicsSettings Out;
	if (const UCSGameUserSettings* Settings = UCSGameUserSettings::Get())
	{
		Out.Resolution = Settings->GetScreenResolution();
		Out.WindowMode = Settings->GetFullscreenMode();
		Out.FrameRateLimit = Settings->GetFrameRateLimit();
		Out.bVSync = Settings->IsVSyncEnabled();
		for (int32 g = 0; g < static_cast<int32>(ECSGraphicsGroup::Count); ++g)
		{
			Out.Groups[g] = GroupLevel(Settings->ScalabilityQuality, g);
		}
		Out.Preset = Settings->GraphicsPreset;
		Out.Upscaler = Settings->Upscaler;
		Out.RenderScale = Settings->RenderScale;
		Out.bRayTracing = Settings->bRayTracing;
	}
	SanitizeGraphics(Out);
	return Out;
}

void UCSSettingsSubsystem::ApplyGraphics(const FCSGraphicsSettings& InGraphics)
{
	UCSGameUserSettings* Settings = UCSGameUserSettings::Get();
	if (!Settings)
	{
		return;
	}
	FCSGraphicsSettings Graphics = InGraphics;
	SanitizeGraphics(Graphics);

	Settings->SetScreenResolution(Graphics.Resolution);
	Settings->SetFullscreenMode(Graphics.WindowMode);
	Settings->SetFrameRateLimit(Graphics.FrameRateLimit);
	Settings->SetVSyncEnabled(Graphics.bVSync);
	const int32* G = Graphics.Groups;
	Settings->SetViewDistanceQuality(G[static_cast<int32>(ECSGraphicsGroup::ViewDistance)]);
	Settings->SetAntiAliasingQuality(G[static_cast<int32>(ECSGraphicsGroup::AntiAliasing)]);
	Settings->SetShadowQuality(G[static_cast<int32>(ECSGraphicsGroup::Shadows)]);
	Settings->SetGlobalIlluminationQuality(G[static_cast<int32>(ECSGraphicsGroup::GlobalIllumination)]);
	Settings->SetReflectionQuality(G[static_cast<int32>(ECSGraphicsGroup::Reflections)]);
	Settings->SetPostProcessingQuality(G[static_cast<int32>(ECSGraphicsGroup::PostProcess)]);
	Settings->SetTextureQuality(G[static_cast<int32>(ECSGraphicsGroup::Textures)]);
	Settings->SetVisualEffectQuality(G[static_cast<int32>(ECSGraphicsGroup::Effects)]);
	Settings->SetShadingQuality(G[static_cast<int32>(ECSGraphicsGroup::Shading)]);
	// Not on the screen (no foliage or landscape on the maps): follow the shading level.
	Settings->SetFoliageQuality(G[static_cast<int32>(ECSGraphicsGroup::Shading)]);
	Settings->ScalabilityQuality.LandscapeQuality = G[static_cast<int32>(ECSGraphicsGroup::Shading)];
	Settings->GraphicsPreset = Graphics.Preset;
	Settings->Upscaler = static_cast<uint8>(Graphics.Upscaler);
	Settings->RenderScale = static_cast<uint8>(Graphics.RenderScale);
	Settings->bRayTracing = Graphics.bRayTracing;
	Settings->ApplySettings(/*bCheckForCommandLineOverrides*/ false);
	Settings->SaveSettings();

	UE_LOG(LogCS, Log, TEXT("Graphics applied: %dx%d mode %d, limit %.0f, preset %d, upscaler %d, render scale %d, ray tracing %s, vsync %s."),
		Graphics.Resolution.X, Graphics.Resolution.Y, static_cast<int32>(Graphics.WindowMode), Graphics.FrameRateLimit,
		Graphics.Preset, Graphics.Upscaler, Graphics.RenderScale, Graphics.bRayTracing ? TEXT("on") : TEXT("off"),
		Graphics.bVSync ? TEXT("on") : TEXT("off"));
}

void UCSSettingsSubsystem::SetPreset(FCSGraphicsSettings& InOut, int32 Preset)
{
	InOut.Preset = FMath::Clamp(Preset, 0, FCSGraphicsSettings::CustomPreset);
	if (InOut.Preset == FCSGraphicsSettings::CustomPreset)
	{
		return;
	}
	for (int32& Level : InOut.Groups)
	{
		Level = InOut.Preset;
	}
}

void UCSSettingsSubsystem::SanitizeGraphics(FCSGraphicsSettings& InOut)
{
	InOut.Preset = FMath::Clamp(InOut.Preset, 0, FCSGraphicsSettings::CustomPreset);
	bool bAllSame = true;
	for (int32& Level : InOut.Groups)
	{
		Level = FMath::Clamp(Level, 0, 4);
		bAllSame &= Level == InOut.Groups[0];
	}
	if (InOut.Preset != FCSGraphicsSettings::CustomPreset && (!bAllSame || InOut.Groups[0] != InOut.Preset))
	{
		InOut.Preset = bAllSame ? InOut.Groups[0] : FCSGraphicsSettings::CustomPreset;
	}
	InOut.Upscaler = FMath::Clamp(InOut.Upscaler, 0, 2);
	InOut.RenderScale = FMath::Clamp(InOut.RenderScale, 0, 4);
	InOut.FrameRateLimit = FMath::Max(InOut.FrameRateLimit, 0.f);
}

void UCSSettingsSubsystem::AutoDetectGraphics()
{
	if (UCSGameUserSettings* Settings = UCSGameUserSettings::Get())
	{
		Settings->AutoDetect();
	}
}

TArray<FIntPoint> UCSSettingsSubsystem::GetSupportedResolutions()
{
	TArray<FIntPoint> Found;
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Found);
	if (Found.Num() == 0)
	{
		UKismetSystemLibrary::GetConvenientWindowedResolutions(Found);
	}

	// Ignore tiny modes nobody plays at, then de-duplicate (the platform lists
	// every refresh rate separately).
	TArray<FIntPoint> Out;
	for (const FIntPoint& R : Found)
	{
		if (R.X >= 1024 && R.Y >= 720)
		{
			Out.AddUnique(R);
		}
	}
	if (Out.Num() == 0)
	{
		Out = { FIntPoint(1280, 720), FIntPoint(1600, 900), FIntPoint(1920, 1080), FIntPoint(2560, 1440) };
	}
	Out.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
	return Out;
}

const TArray<float>& UCSSettingsSubsystem::GetFrameRateChoices()
{
	static const TArray<float> Choices = { 30.f, 60.f, 90.f, 120.f, 144.f, 165.f, 240.f, 0.f };
	return Choices;
}

void UCSSettingsSubsystem::SanitizePreferences(FCSPlayerPreferences& InOut)
{
	InOut.MouseSensitivity = FMath::Clamp(InOut.MouseSensitivity, 0.05f, 5.f);
	InOut.FieldOfView = FMath::Clamp(InOut.FieldOfView, 70.f, 120.f);
	InOut.MasterVolume = FMath::Clamp(InOut.MasterVolume, 0.f, 1.f);
	InOut.MusicVolume = FMath::Clamp(InOut.MusicVolume, 0.f, 1.f);
	InOut.EffectsVolume = FMath::Clamp(InOut.EffectsVolume, 0.f, 1.f);
	InOut.AimSensitivity = FMath::Clamp(InOut.AimSensitivity, 0.2f, 2.f);
	InOut.CrosshairStyle = FMath::Clamp(InOut.CrosshairStyle, 0, 3);
	InOut.CrosshairColor = FMath::Clamp(InOut.CrosshairColor, 0, 5);
	InOut.CrosshairSize = FMath::Clamp(InOut.CrosshairSize, 2.f, 20.f);
	InOut.CrosshairGap = FMath::Clamp(InOut.CrosshairGap, 0.f, 16.f);
	InOut.CrosshairThickness = FMath::Clamp(InOut.CrosshairThickness, 1.f, 5.f);
	InOut.ColorVision = FMath::Clamp(InOut.ColorVision, 0, 3);
	InOut.UIScale = FMath::Clamp(InOut.UIScale, 0.8f, 1.3f);
	InOut.CameraMotion = FMath::Clamp(InOut.CameraMotion, 0.f, 1.f);
	for (auto It = InOut.KeyOverrides.CreateIterator(); It; ++It)
	{
		if (!It->Value.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}
