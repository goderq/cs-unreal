// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// v2.0 phase 7 (AUDIT C8): the lookups every system makes many times a frame -
// the match director, a player's pawn, a player's inventory - used to walk the
// world's actors on every call (about 120 call sites, several inside loops over
// players). This remembers the last answer per world and checks it before
// using it: the cached actor must still exist, not be on its way out, and
// still belong to that player; otherwise the walk runs once and the answer is
// remembered again. Nothing else changes - a miss behaves exactly as before.
//
// It also preloads, asynchronously when a game world starts, what gameplay
// code otherwise loads with LoadSynchronous the first time it is needed - in
// the middle of a fight: the item registry, the weapon definitions, their
// meshes, models, sounds and the character animations (see PreloadFrom).

#pragma once

#include "CoreMinimal.h"
#include "EngineUtils.h"
#include "Subsystems/WorldSubsystem.h"
#include "CSWorldCache.generated.h"

struct FStreamableHandle;
class ACSCharacter;
class ACSMatchDirector;
class ACSPlayerInventory;

UCLASS()
class CSFUSION_API UCSWorldCache : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UCSWorldCache* Get(const UWorld* World) { return World ? World->GetSubsystem<UCSWorldCache>() : nullptr; }

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Soft references queued or loaded by the preload so far (tests, log). */
	int32 GetPreloadedCount() const { return Preloaded.Num(); }

	TWeakObjectPtr<ACSMatchDirector> Director;
	TMap<int32, TWeakObjectPtr<ACSCharacter>> Pawns;
	TMap<int32, TWeakObjectPtr<ACSPlayerInventory>> Inventories;

	/** The actor of T whose GetId(Actor) == Id: the remembered one if it still matches, else the first found. */
	template <typename T, typename FGetId>
	static T* FindOwned(UWorld* World, TMap<int32, TWeakObjectPtr<T>>* Cache, int32 Id, FGetId GetId)
	{
		if (Cache)
		{
			if (const TWeakObjectPtr<T>* Hit = Cache->Find(Id))
			{
				T* Actor = Hit->Get();
				if (Actor && !Actor->IsActorBeingDestroyed() && GetId(Actor) == Id)
				{
					return Actor;
				}
				Cache->Remove(Id);
			}
		}
		for (TActorIterator<T> It(World); It; ++It)
		{
			if (!It->IsActorBeingDestroyed() && GetId(*It) == Id)
			{
				if (Cache)
				{
					Cache->Add(Id, *It);
				}
				return *It;
			}
		}
		return nullptr;
	}

private:
	/**
	 * Loads every soft reference of these objects in the background; objects
	 * of this game's own classes that come back are searched the same way,
	 * down to MaxPreloadDepth. Maps (soft UWorld references) are skipped.
	 */
	void PreloadFrom(const TArray<const UObject*>& Objects, int32 Depth);

	static constexpr int32 MaxPreloadDepth = 3;
	TSet<FSoftObjectPath> Preloaded;
	/** Held for the world's lifetime so the preloaded assets are not collected. */
	TArray<TSharedPtr<FStreamableHandle>> PreloadHandles;
};
