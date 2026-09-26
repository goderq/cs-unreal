// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Core/CSWorldCache.h"

#include "Animation/CSAnimationSettings.h"
#include "Audio/CSAudioSettings.h"
#include "Core/CSLog.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Items/CSItemSettings.h"
#include "Misc/CoreMisc.h"
#include "UObject/PropertyIterator.h"
#include "UObject/UnrealType.h"
#include "Weapons/CSWeaponPresentation.h"

void UCSWorldCache::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.IsGameWorld() || IsRunningCommandlet())
	{
		return;
	}
	PreloadFrom({ GetDefault<UCSItemSettings>(), GetDefault<UCSWeaponPresentationSettings>(),
		GetDefault<UCSAudioSettings>(), GetDefault<UCSAnimationSettings>() }, 0);
}

void UCSWorldCache::Deinitialize()
{
	for (const TSharedPtr<FStreamableHandle>& Handle : PreloadHandles)
	{
		if (Handle.IsValid())
		{
			Handle->CancelHandle();
		}
	}
	PreloadHandles.Reset();
	Super::Deinitialize();
}

void UCSWorldCache::PreloadFrom(const TArray<const UObject*>& Objects, int32 Depth)
{
	TArray<FSoftObjectPath> Paths;
	for (const UObject* Object : Objects)
	{
		if (!Object)
		{
			continue;
		}
		for (TPropertyValueIterator<FSoftObjectProperty> It(Object->GetClass(), Object); It; ++It)
		{
			const FSoftObjectProperty* Property = It.Key();
			if (Property->PropertyClass && Property->PropertyClass->IsChildOf(UWorld::StaticClass()))
			{
				continue;   // a map is loaded by travelling to it, never in the background
			}
			const FSoftObjectPath Path = static_cast<const FSoftObjectPtr*>(It.Value())->ToSoftObjectPath();
			if (Path.IsValid() && !Preloaded.Contains(Path))
			{
				Preloaded.Add(Path);
				Paths.Add(Path);
			}
		}
	}
	if (Paths.IsEmpty())
	{
		return;
	}

	const double Started = FPlatformTime::Seconds();
	TSharedPtr<FStreamableHandle> Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,
		FStreamableDelegate::CreateWeakLambda(this, [this, Paths, Depth, Started]()
		{
			UE_LOG(LogCS, Log, TEXT("Preload: %d asset(s) at depth %d in %.2f s."), Paths.Num(), Depth, FPlatformTime::Seconds() - Started);
			if (Depth + 1 >= MaxPreloadDepth)
			{
				return;
			}
			// Only this game's data assets lead further (weapon -> meshes, sounds);
			// engine assets such as materials are loaded with their own dependencies.
			static const FName OwnPackage(TEXT("/Script/CSFusion"));
			TArray<const UObject*> Next;
			for (const FSoftObjectPath& Path : Paths)
			{
				const UObject* Loaded = Path.ResolveObject();
				if (Loaded && Loaded->GetClass()->GetOutermost()->GetFName() == OwnPackage)
				{
					Next.Add(Loaded);
				}
			}
			PreloadFrom(Next, Depth + 1);
		}), FStreamableManager::AsyncLoadHighPriority);
	if (Handle.IsValid())
	{
		PreloadHandles.Add(Handle);
	}
}
