// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Single compatibility header for the Photon Fusion 3 Unreal SDK.
//
// Include this INSTEAD of any Fusion header. It gives the rest of the codebase
// one stable surface whether or not the SDK is installed:
//
//   * With the SDK    (CS_WITH_FUSION == 1): pulls in the real Fusion headers
//                     so FUSION_BODY() / SEND_FUSIONRPC() are parsed by the
//                     PhotonFusionUbtPlugin UHT exporter.
//   * Without the SDK (CS_WITH_FUSION == 0): defines both macros as no-ops so
//                     the project still compiles and runs offline.
//
// IMPORTANT (UE 5.8): every .cpp that implements a class carrying FUSION_BODY()
// must add, directly under its own header include:
//
//     #include UE_INLINE_GENERATED_CPP_BY_NAME(MyClass.fusion)
//
// Wrap that line in CS_FUSION_INLINE_GENERATED_CPP(MyClass) from this header so
// it disappears cleanly in offline builds.

#pragma once

#include "CoreMinimal.h"

#ifndef CS_WITH_FUSION
	#define CS_WITH_FUSION 0
#endif

#if CS_WITH_FUSION

	#include "FusionMacros.h"
	#include "FusionOnlineSubsystem.h"
	#include "FusionActorComponent.h"

	/**
	 * RPC target tokens.
	 *
	 * The Fusion 3 preview docs spell these two ways: the Blueprint-facing
	 * names (TargetMasterClient, ...) and the C++ enum (EFusionRPCTarget::
	 * SendToMasterClient, ...). SEND_FUSIONRPC is a *sentinel* keyword parsed
	 * textually by the UBT plugin, so the exact token matters. These aliases
	 * keep every call site in the project on one spelling - if the SDK you
	 * unpacked uses the other one, change it here only.
	 *
	 * Verify against Plugins/PhotonFusion/Source/PhotonFusion/Public/FusionMacros.h
	 * after installing the SDK.
	 */
	#define CS_RPC_TO_MASTER        EFusionRPCTarget::SendToMasterClient
	#define CS_RPC_TO_OWNER         EFusionRPCTarget::SendToObjectOwner
	#define CS_RPC_TO_ALL           EFusionRPCTarget::SendToAllClients
	#define CS_RPC_TO_EVERYONE_ELSE EFusionRPCTarget::SendToEveryoneElse

	#define CS_FUSION_INLINE_GENERATED_CPP(ClassName) \
		UE_INLINE_GENERATED_CPP_BY_NAME(ClassName.fusion)

#else // !CS_WITH_FUSION

	// The UBT plugin is what gives these meaning; offline they vanish.
	#ifndef FUSION_BODY
		#define FUSION_BODY(...)
	#endif
	#ifndef SEND_FUSIONRPC
		#define SEND_FUSIONRPC(...)
	#endif

	#define CS_RPC_TO_MASTER        0
	#define CS_RPC_TO_OWNER         0
	#define CS_RPC_TO_ALL           0
	#define CS_RPC_TO_EVERYONE_ELSE 0

	// Expands to a harmless self-include-free token in offline builds.
	#define CS_FUSION_INLINE_GENERATED_CPP(ClassName) "Core/CSFusionCompatNoop.h"

#endif // CS_WITH_FUSION
