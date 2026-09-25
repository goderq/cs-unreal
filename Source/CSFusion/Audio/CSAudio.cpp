// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Audio/CSAudio.h"

#include "Audio/CSAudioSettings.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundConcurrency.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

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

	void PlayAt(const UObject* WorldContext, const TSoftObjectPtr<USoundBase>& Sound, const FVector& Location, float Volume, float Pitch,
		ECSSound Kind)
	{
		USoundBase* Asset = Resolve(Sound);
		if (!Asset || !CanPlay(WorldContext))
		{
			return;
		}
		const UCSAudioSettings* Settings = UCSAudioSettings::Get();
		const TSoftObjectPtr<USoundAttenuation>* Attenuation = &Settings->WorldAttenuation;
		const TSoftObjectPtr<USoundConcurrency>* Concurrency = nullptr;
		switch (Kind)
		{
		case ECSSound::Weapon:
			Attenuation = &Settings->WeaponAttenuation;
			Concurrency = &Settings->WeaponConcurrency;
			break;
		case ECSSound::Footstep:
			Attenuation = &Settings->FootstepAttenuation;
			Concurrency = &Settings->FootstepConcurrency;
			break;
		case ECSSound::Impact:
			Attenuation = &Settings->ImpactAttenuation;
			Concurrency = &Settings->ImpactConcurrency;
			break;
		case ECSSound::Explosion:
			Attenuation = &Settings->ExplosionAttenuation;
			Concurrency = &Settings->ExplosionConcurrency;
			break;
		case ECSSound::World:
			break;
		}
		// A kind without its own asset falls back to the world attenuation.
		USoundAttenuation* AttenuationAsset = Attenuation->LoadSynchronous();
		if (!AttenuationAsset)
		{
			AttenuationAsset = Settings->WorldAttenuation.LoadSynchronous();
		}
		USoundConcurrency* ConcurrencyAsset = Concurrency ? Concurrency->LoadSynchronous() : nullptr;
		UGameplayStatics::PlaySoundAtLocation(WorldContext, Asset, Location, FRotator::ZeroRotator,
			Volume, Pitch, 0.f, AttenuationAsset, ConcurrencyAsset);
	}

	EPhysicalSurface SurfaceBelow(const UObject* WorldContext, const FVector& Location, const AActor* Ignored)
	{
		UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		if (!World)
		{
			return SurfaceType_Default;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CSSurfaceBelow), false, Ignored);
		Params.bReturnPhysicalMaterial = true;
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Location + FVector(0.f, 0.f, 20.f), Location - FVector(0.f, 0.f, 60.f),
			ECC_Visibility, Params))
		{
			return SurfaceType_Default;
		}
		return UPhysicalMaterial::DetermineSurfaceType(Hit.PhysMaterial.Get());
	}

	const TArray<TSoftObjectPtr<USoundBase>>& FootstepsFor(EPhysicalSurface Surface)
	{
		const UCSAudioSettings* Settings = UCSAudioSettings::Get();
		const TArray<TSoftObjectPtr<USoundBase>>* Set = &Settings->Footsteps;
		if (Surface == CSSurface::Metal)
		{
			Set = &Settings->FootstepsMetal;
		}
		else if (Surface == CSSurface::Wood)
		{
			Set = &Settings->FootstepsWood;
		}
		else if (Surface == CSSurface::Dirt)
		{
			Set = &Settings->FootstepsDirt;
		}
		return Set->Num() > 0 ? *Set : Settings->Footsteps;
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

	UAudioComponent* Spawn2D(const UObject* WorldContext, const TSoftObjectPtr<USoundBase>& Sound, float Volume, float Pitch)
	{
		USoundBase* Asset = Resolve(Sound);
		if (!Asset || !CanPlay(WorldContext))
		{
			return nullptr;
		}
		return UGameplayStatics::SpawnSound2D(WorldContext, Asset, Volume, Pitch);
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
		USoundClass* Music = Settings->MusicClass.LoadSynchronous();
		USoundClass* Effects = Settings->EffectsClass.LoadSynchronous();

		// Volume is set on the sound classes themselves (every generated sound
		// belongs to one of them); the audio device re-reads class properties
		// every frame. A SoundMix class override was tried first and dropped: on
		// a class the device had not registered it logs "RecursiveApplyAdjuster
		// failed" every frame, tens of thousands of lines a minute.
		if (Music)
		{
			Music->Properties.Volume = FMath::Clamp(MusicVolume, 0.f, 1.f);
		}
		if (Effects)
		{
			Effects->Properties.Volume = FMath::Clamp(EffectsVolume, 0.f, 1.f);
		}
	}
}
