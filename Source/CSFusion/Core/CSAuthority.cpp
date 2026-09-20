// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Core/CSAuthority.h"

#include "Core/CSFusionCompat.h"
#include "Core/CSLog.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"

UFusionOnlineSubsystem* UCSAuthority::GetFusion(const UObject* WorldContextObject)
{
#if CS_WITH_FUSION
	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World)
	{
		return nullptr;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UFusionOnlineSubsystem>() : nullptr;
#else
	return nullptr;
#endif
}

ECSAuthorityBackend UCSAuthority::GetBackend(const UObject* WorldContextObject)
{
#if CS_WITH_FUSION
	if (UFusionOnlineSubsystem* Fusion = GetFusion(WorldContextObject))
	{
		if (Fusion->IsInRoom())
		{
			return ECSAuthorityBackend::FusionMaster;
		}
	}
#endif

	// Stage 8 hook: a real dedicated-server target reports itself here.
	if (const UWorld* World = GEngine
			? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
			: nullptr)
	{
		if (World->GetNetMode() == NM_DedicatedServer || World->GetNetMode() == NM_Client)
		{
			return ECSAuthorityBackend::DedicatedServer;
		}
	}

	return ECSAuthorityBackend::Offline;
}

bool UCSAuthority::IsGameAuthority(const UObject* WorldContextObject)
{
	switch (GetBackend(WorldContextObject))
	{
	case ECSAuthorityBackend::FusionMaster:
	{
#if CS_WITH_FUSION
		UFusionOnlineSubsystem* Fusion = GetFusion(WorldContextObject);
		return Fusion && Fusion->IsMasterClient();
#else
		return false;
#endif
	}

	case ECSAuthorityBackend::DedicatedServer:
	{
		const UWorld* World = GEngine
			? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
			: nullptr;
		return World && (World->GetNetMode() != NM_Client);
	}

	case ECSAuthorityBackend::Offline:
	default:
		// Single process, nobody to disagree with.
		return true;
	}
}

bool UCSAuthority::CanWrite(const AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return false;
	}

#if CS_WITH_FUSION
	if (UFusionOnlineSubsystem* Fusion = GetFusion(Actor))
	{
		if (Fusion->IsInRoom())
		{
			// CanModify() is the authoritative answer while a session runs.
			// AActor::HasAuthority() agrees once the object handshake is done,
			// but reads true on every peer before that, so prefer CanModify.
			return Fusion->CanModify(const_cast<AActor*>(Actor));
		}
	}
#endif

	return Actor->HasAuthority();
}

bool UCSAuthority::IsSessionActive(const UObject* WorldContextObject)
{
#if CS_WITH_FUSION
	UFusionOnlineSubsystem* Fusion = GetFusion(WorldContextObject);
	return Fusion && Fusion->IsInRoom();
#else
	return false;
#endif
}

int32 UCSAuthority::GetLocalPlayerId(const UObject* WorldContextObject)
{
#if CS_WITH_FUSION
	if (UFusionOnlineSubsystem* Fusion = GetFusion(WorldContextObject))
	{
		return Fusion->GetLocalPlayerId();
	}
#endif
	return 0;
}

int32 UCSAuthority::GetRoomPlayerCount(const UObject* WorldContextObject)
{
#if CS_WITH_FUSION
	if (UFusionOnlineSubsystem* Fusion = GetFusion(WorldContextObject))
	{
		return Fusion->PlayerCount();
	}
#endif
	return 1;
}

int32 UCSAuthority::GetRttMs(const UObject* WorldContextObject)
{
#if CS_WITH_FUSION
	if (UFusionOnlineSubsystem* Fusion = GetFusion(WorldContextObject))
	{
		return Fusion->GetRtt();
	}
#endif
	return 0;
}

double UCSAuthority::GetNetworkTimeSeconds(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World)
	{
		return 0.0;
	}

	// Fusion patches its own network time into AGameStateBase, so
	// GetServerWorldTimeSeconds() is room-synchronised while a session runs
	// and falls back to local world time offline.
	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}

	return World->GetTimeSeconds();
}
