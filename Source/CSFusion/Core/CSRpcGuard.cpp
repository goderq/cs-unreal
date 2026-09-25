// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#include "Core/CSRpcGuard.h"

#include "Combat/CSMatchDirector.h"
#include "Core/CSAuthority.h"
#include "Core/CSLog.h"
#include "FusionRpcOrigin.h"
#include "GameFramework/Actor.h"

namespace
{
	bool Refuse(const UObject* Context, const TCHAR* RpcName, const FString& Why)
	{
		const int32 From = FFusionRpcOrigin::Current;
		UE_LOG(LogCSSecurity, Warning, TEXT("RPC %s on %s refused: sent by player %d, %s."),
			RpcName, Context ? *Context->GetName() : TEXT("?"), From, *Why);
		// Only the authority keeps strikes; everyone else just drops the event.
		if (ACSMatchDirector* Director = ACSMatchDirector::Get(Context))
		{
			Director->ReportViolation(From, ECSCheatReason::ForgedRpc, /*Weight*/ 3.f,
				FString::Printf(TEXT("%s sent to an object that is not theirs"), RpcName));
		}
		return false;
	}
}

namespace CSRpcGuard
{
	int32 Sender()
	{
		return FFusionRpcOrigin::bInRpc ? FFusionRpcOrigin::Current : 0;
	}

	bool InRpc()
	{
		return FFusionRpcOrigin::bInRpc;
	}

	bool FromOwner(const AActor* Actor, const TCHAR* RpcName)
	{
		// Offline, bots and the authority's own calls reach handlers directly.
		if (!FFusionRpcOrigin::bInRpc)
		{
			return true;
		}
		const int32 From = FFusionRpcOrigin::Current;
		const int32 Owner = UCSAuthority::GetOwningPlayerId(Actor);
		if (From != 0 && From == Owner)
		{
			UE_LOG(LogCSSecurity, VeryVerbose, TEXT("RPC %s on %s from player %d (owner) accepted."), RpcName, *GetNameSafe(Actor), From);
			return true;
		}
		return Refuse(Actor, RpcName, FString::Printf(TEXT("the object belongs to player %d"), Owner));
	}

	bool FromMasterClient(const UObject* Context, const TCHAR* RpcName)
	{
		if (!FFusionRpcOrigin::bInRpc)
		{
			return true;
		}
		const int32 From = FFusionRpcOrigin::Current;
		const int32 Master = FFusionRpcOrigin::MasterClient;
		if (From != 0 && From == Master)
		{
			UE_LOG(LogCSSecurity, VeryVerbose, TEXT("RPC %s on %s from the Master Client %d accepted."), RpcName, *GetNameSafe(Context), From);
			return true;
		}
		return Refuse(Context, RpcName, FString::Printf(TEXT("only the Master Client (%d) sends it"), Master));
	}

#if !UE_BUILD_SHIPPING
	void DebugForgeNextSender(int32 PlayerId)
	{
		FFusionRpcOrigin::DebugForgeNext = PlayerId;
	}
#endif
}
