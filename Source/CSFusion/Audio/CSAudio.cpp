// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Audio/CSAudio.h"

#include "Audio/CSAudioSettings.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"

namespace CSAudio
{
	namespace
	{
		USoundBase* Resolve(const TSoftObjectPtr<USoundBase>& Sound)
		{
			return Sound.IsNull() ? nullptr : Sound.LoadSynchronous();
		}

		bool CanPlay(const UObject* WorldContext)
		{
			const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
			// No audio on a dedicated server, and none during shutdown.
			return World && !World->bIsTearingDown && World->GetNetMode() != NM_DedicatedServer;
		}
	}

	void PlayAt(const UObject* WorldContext, const TSoftObjectPtr<USoundBase>& Sound, const FVector& Location, float Volume, float Pitch)
	{
		USoundBase* Asset = Resolve(Sound);
		if (!Asset || !CanPlay(WorldContext))
		{
			return;
		}
		USoundAttenuation* Attenuation = UCSAudioSettings::Get()->WorldAttenuation.LoadSynchronous();
		UGameplayStatics::PlaySoundAtLocation(WorldContext, Asset, Location, FRotator::ZeroRotator,
			Volume, Pitch, 0.f, Attenuation);
	}

	void Play2D(const UObject* WorldContext, const TSoftObjectPtr<USoundBase>& Sound, float Volume, float Pitch)
	{
		USoundBase* Asset = Resolve(Sound);
		if (!Asset || !CanPlay(WorldContext))
		{
			return;
		}
		UGameplayStatics::PlaySound2D(WorldContext, Asset, Volume, Pitch);
	}

	UAudioComponent* PlayMusic(const UObject* WorldContext, const TSoftObjectPtr<USoundBase>& Sound)
	{
		USoundBase* Asset = Resolve(Sound);
		if (!Asset || !CanPlay(WorldContext))
		{
			return nullptr;
		}
		// Persists only for this world; the menu restarts it on its own map.
		return UGameplayStatics::SpawnSound2D(WorldContext, Asset, 1.f, 1.f, 0.f, nullptr, false, true);
	}

	void ApplyVolumes(const UObject* WorldContext, float MusicVolume, float EffectsVolume)
	{
		if (!CanPlay(WorldContext))
		{
			return;
		}
		const UCSAudioSettings* Settings = UCSAudioSettings::Get();
		USoundMix* Mix = Settings->VolumeMix.LoadSynchronous();
		USoundClass* Music = Settings->MusicClass.LoadSynchronous();
		USoundClass* Effects = Settings->EffectsClass.LoadSynchronous();
		if (!Mix)
		{
			return;
		}
		if (Music)
		{
			UGameplayStatics::SetSoundMixClassOverride(WorldContext, Mix, Music, MusicVolume, 1.f, 0.f, true);
		}
		if (Effects)
		{
			UGameplayStatics::SetSoundMixClassOverride(WorldContext, Mix, Effects, EffectsVolume, 1.f, 0.f, true);
		}
		UGameplayStatics::PushSoundMixModifier(WorldContext, Mix);
	}
}
