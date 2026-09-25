// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// The only place gameplay code plays sounds from. Resolves the soft
// references in UCSAudioSettings / UCSWeaponDefinition, picks attenuation and
// concurrency by the kind of sound, and applies the player's volumes.

#pragma once

#include "CoreMinimal.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Engine/EngineTypes.h"

class AActor;
class UAudioComponent;
class USoundBase;
class UObject;

/** What a 3D sound is; selects its attenuation (with occlusion) and concurrency. */
enum class ECSSound : uint8
{
	World,     // equip, reload, pickup, death: the shared world attenuation
	Weapon,    // gunshots: heard far, 16 at a time
	Footstep,  // steps and landings: short range, dulled behind walls
	Impact,    // bullet / knife / grenade bounce hits
	Explosion, // grenades: heard across the map
};

/**
 * Physical surface types. Must match [/Script/Engine.PhysicsSettings] in
 * Config/DefaultEngine.ini and Scripts/bootstrap_phase3_audio.py. Anything
 * else (concrete, brick, plaster, the default) counts as stone.
 */
namespace CSSurface
{
	constexpr EPhysicalSurface Metal = SurfaceType1;
	constexpr EPhysicalSurface Wood = SurfaceType2;
	constexpr EPhysicalSurface Dirt = SurfaceType3;
}

namespace CSCollision
{
	/**
	 * Trace channel of sound occlusion (DefaultEngine.ini, "AudioOcclusion").
	 * Blocked by the world; characters and ragdolls ignore it.
	 */
	constexpr ECollisionChannel AudioOcclusion = ECC_GameTraceChannel1;
}

namespace CSAudio
{
	/** 3D sound at a location, attenuated for its kind. Null-safe. */
	void PlayAt(const UObject* WorldContext, const TSoftObjectPtr<USoundBase>& Sound, const FVector& Location,
		float Volume = 1.f, float Pitch = 1.f, ECSSound Kind = ECSSound::World);

	/**
	 * Surface under a point: a short trace down on the visibility channel that
	 * ignores Ignored. SurfaceType_Default when nothing is hit.
	 */
	EPhysicalSurface SurfaceBelow(const UObject* WorldContext, const FVector& Location, const AActor* Ignored);

	/** Footstep set for a surface; the stone set when that surface has none. */
	const TArray<TSoftObjectPtr<USoundBase>>& FootstepsFor(EPhysicalSurface Surface);

	/** 2D sound for the local player (UI, hit markers). Null-safe. */
	void Play2D(const UObject* WorldContext, const TSoftObjectPtr<USoundBase>& Sound, float Volume = 1.f, float Pitch = 1.f);

	/** 2D sound that the caller can stop early (the flashbang ring). Null-safe. */
	UAudioComponent* Spawn2D(const UObject* WorldContext, const TSoftObjectPtr<USoundBase>& Sound, float Volume = 1.f, float Pitch = 1.f);

	/** Looping 2D music; returns the component so the caller can stop it. */
	UAudioComponent* PlayMusic(const UObject* WorldContext, const TSoftObjectPtr<USoundBase>& Sound);

	/**
	 * Pushes the volume mix and sets the music / effects class volumes.
	 * Call after loading settings and whenever a new world starts.
	 */
	void ApplyVolumes(const UObject* WorldContext, float MusicVolume, float EffectsVolume);
}
