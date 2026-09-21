// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Weapon effects: muzzle flash, tracer, impact (puff + bullet hole decal +
// sound). Cosmetic and local; every peer calls these itself in response to
// the replicated shot confirmation, so nothing here goes over the network.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;

namespace CSEffects
{
	/**
	 * Flash and light at a muzzle. In first person the flash is visible only
	 * to Owner, in third person to everyone except Owner.
	 */
	void MuzzleFlash(UWorld* World, const FTransform& Muzzle, float Scale, const AActor* Owner, bool bFirstPerson);

	/** Brief bright streak from Start to End. */
	void Tracer(UWorld* World, const FVector& Start, const FVector& End, const FLinearColor& Color);

	/**
	 * Impact at Location. Surfaces get dust, sparks, a bullet hole and a
	 * ricochet sound; players get a red puff and a body-hit sound.
	 */
	void Impact(UWorld* World, const FVector& Location, const FVector& Normal, bool bHitPlayer);
}
