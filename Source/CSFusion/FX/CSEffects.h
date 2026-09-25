// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Weapon effects: muzzle flash and smoke, shells, tracer, impacts by surface,
// explosions. Cosmetic and local; every peer calls these itself in response to
// the replicated shot confirmation, so nothing here goes over the network.

#pragma once

#include "CoreMinimal.h"
#include "Chaos/ChaosEngineInterface.h"

class AActor;
class UNiagaraComponent;
class UNiagaraSystem;
class UWorld;

namespace CSEffects
{
	/**
	 * Starts a Niagara effect from Niagara's component pool (returned to the
	 * pool when it ends). Null on a dedicated server or if the asset is missing.
	 * The project's systems throw particles along +X of Rotation.
	 */
	UNiagaraComponent* SpawnSystem(UWorld* World, const TSoftObjectPtr<UNiagaraSystem>& System, const FVector& Location, const FRotator& Rotation);

	/** Physical surface at an impact point (short trace into the surface along -Normal). */
	EPhysicalSurface SurfaceAt(UWorld* World, const FVector& Location, const FVector& Normal);

	/** A spent case thrown out of the ejection port, behind and right of the muzzle. */
	void ShellEject(UWorld* World, const FTransform& Muzzle);

	/** Grenade: fireball, rolling smoke, debris and sparks (the light flash is separate). */
	void Explosion(UWorld* World, const FVector& Location);

	/** Flashbang: white sparks and haze (the light flash is separate). */
	void FlashbangBurst(UWorld* World, const FVector& Location);

	/**
	 * Flash and light at a muzzle. In first person the flash is visible only
	 * to Owner, in third person to everyone except Owner.
	 */
	void MuzzleFlash(UWorld* World, const FTransform& Muzzle, float Scale, const AActor* Owner, bool bFirstPerson);

	/** Brief bright streak from Start to End. */
	void Tracer(UWorld* World, const FVector& Start, const FVector& End, const FLinearColor& Color);

	/**
	 * Impact at Location. Surfaces get particles by their physical material
	 * (sparks off metal, splinters off wood, clods off dirt, dust and chips off
	 * stone), a bullet hole and a ricochet pitched to the surface; players get
	 * blood and a body-hit sound.
	 */
	void Impact(UWorld* World, const FVector& Location, const FVector& Normal, bool bHitPlayer);
}
