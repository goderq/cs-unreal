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
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void UCSSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	LoadPreferences();
	ApplyAudio();

	// The engine applies GameUserSettings.ini on its own at startup
	// (UCSGameUserSettings adds the upscaler and ray tracing). The first time
	// on a PC the settings are picked for its hardware instead of the defaults;
	// automated tests keep the defaults so their numbers stay comparable.
	UCSGameUserSettings* Gfx = UCSGameUserSettings::Get();
	if (Gfx && !Gfx->bAutoDetected && !FParse::Param(FCommandLine::Get(), TEXT("noautodetect")) && !GIsAutomationTesting)
	{
		AutoDetectGraphics();
	}
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

void UCSSettingsSubsystem::LoadPreferences()
{
	Preferences = DefaultPreferences();

	if (!UGameplayStatics::DoesSaveGameExist(SlotName(), 0))
	{
		return;
	}

	if (const UCSSettingsSave* Save = Cast<UCSSettingsSave>(UGameplayStatics::LoadGameFromSlot(SlotName(), 0)))
	{
		Preferences = Save->Preferences;

		// A hand-edited file must not be able to produce absurd values.
		SanitizePreferences(Preferences);
	}
	else
	{
		UE_LOG(LogCS, Warning, TEXT("Settings save '%s' is unreadable; using defaults."), SlotName());
	}
}

void UCSSettingsSubsystem::SavePreferences() const
{
	UCSSettingsSave* Save = Cast<UCSSettingsSave>(UGameplayStatics::CreateSaveGameObject(UCSSettingsSave::StaticClass()));
	if (!Save)
	{
		return;
	}
	Save->Preferences = Preferences;
	if (!UGameplayStatics::SaveGameToSlot(Save, SlotName(), 0))
	{
		UE_LOG(LogCS, Warning, TEXT("Failed to write settings save '%s'."), SlotName());
	}
}

void UCSSettingsSubsystem::SetPreferences(const FCSPlayerPreferences& NewPreferences, bool bSave)
{
	Preferences = NewPreferences;
	SanitizePreferences(Preferences);
	ApplyAudio();
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
	for (auto It = InOut.KeyOverrides.CreateIterator(); It; ++It)
	{
		if (!It->Value.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}
