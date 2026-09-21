// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// The only place gameplay code plays sounds from. Resolves the soft
// references in UCSAudioSettings / UCSWeaponDefinition, applies the shared
// world attenuation, and applies the player's music / effects volumes.

#pragma once

#include "CoreMinimal.h"

class UAudioComponent;
class USoundBase;
class UObject;

namespace CSAudio
{
	/** 3D sound at a location with the project attenuation. Null-safe. */
	void PlayAt(const UObject* WorldContext, const TSoftObjectPtr<USoundBase>& Sound, const FVector& Location,
		float Volume = 1.f, float Pitch = 1.f);

	/** 2D sound for the local player (UI, hit markers). Null-safe. */
	void Play2D(const UObject* WorldContext, const TSoftObjectPtr<USoundBase>& Sound, float Volume = 1.f, float Pitch = 1.f);

	/** Looping 2D music; returns the component so the caller can stop it. */
	UAudioComponent* PlayMusic(const UObject* WorldContext, const TSoftObjectPtr<USoundBase>& Sound);

	/**
	 * Pushes the volume mix and sets the music / effects class volumes.
	 * Call after loading settings and whenever a new world starts.
	 */
	void ApplyVolumes(const UObject* WorldContext, float MusicVolume, float EffectsVolume);
}
