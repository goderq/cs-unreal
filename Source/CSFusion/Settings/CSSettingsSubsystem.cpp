// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Settings/CSSettingsSubsystem.h"

#include "AudioDevice.h"
#include "Audio/CSAudio.h"
#include "Core/CSLog.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

void UCSSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	LoadPreferences();
	ApplyAudio();

	// The engine applies GameUserSettings.ini on its own at startup; nothing
	// to do for graphics here.
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
		Preferences.MouseSensitivity = FMath::Clamp(Preferences.MouseSensitivity, 0.05f, 5.f);
		Preferences.FieldOfView = FMath::Clamp(Preferences.FieldOfView, 70.f, 120.f);
		Preferences.MasterVolume = FMath::Clamp(Preferences.MasterVolume, 0.f, 1.f);
		Preferences.MusicVolume = FMath::Clamp(Preferences.MusicVolume, 0.f, 1.f);
		Preferences.EffectsVolume = FMath::Clamp(Preferences.EffectsVolume, 0.f, 1.f);
		for (auto It = Preferences.KeyOverrides.CreateIterator(); It; ++It)
		{
			if (!It->Value.IsValid())
			{
				It.RemoveCurrent();
			}
		}
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

FCSGraphicsSettings UCSSettingsSubsystem::GetGraphics() const
{
	FCSGraphicsSettings Out;
	if (const UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		Out.Resolution = Settings->GetScreenResolution();
		Out.WindowMode = Settings->GetFullscreenMode();
		Out.FrameRateLimit = Settings->GetFrameRateLimit();
		Out.Quality = FMath::Clamp(Settings->GetOverallScalabilityLevel(), 0, 3);
		if (Settings->GetOverallScalabilityLevel() < 0)
		{
			// -1 = custom mix; show it as the highest individual level.
			Out.Quality = 3;
		}
		Out.bVSync = Settings->IsVSyncEnabled();
	}
	return Out;
}

void UCSSettingsSubsystem::ApplyGraphics(const FCSGraphicsSettings& Graphics)
{
	UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!Settings)
	{
		return;
	}

	Settings->SetScreenResolution(Graphics.Resolution);
	Settings->SetFullscreenMode(Graphics.WindowMode);
	Settings->SetFrameRateLimit(Graphics.FrameRateLimit);
	Settings->SetOverallScalabilityLevel(FMath::Clamp(Graphics.Quality, 0, 3));
	Settings->SetVSyncEnabled(Graphics.bVSync);
	Settings->ApplySettings(/*bCheckForCommandLineOverrides*/ false);
	Settings->SaveSettings();

	UE_LOG(LogCS, Log, TEXT("Graphics applied: %dx%d mode %d, limit %.0f, quality %d, vsync %s."),
		Graphics.Resolution.X, Graphics.Resolution.Y, static_cast<int32>(Graphics.WindowMode),
		Graphics.FrameRateLimit, Graphics.Quality, Graphics.bVSync ? TEXT("on") : TEXT("off"));
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
