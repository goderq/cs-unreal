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
	#include "FusionHelpers.h"
	#include "FusionOnlineSubsystem.h"
	#include "FusionActorComponent.h"

	/**
	 * RPC TARGETS - read this before writing a SEND_FUSIONRPC.
	 *
	 * Write ONE of these four bare tokens, and nothing else:
	 *
	 *     SEND_FUSIONRPC(TargetMasterClient)    -> the current Master Client
	 *     SEND_FUSIONRPC(TargetObjectOwner)     -> the resolved owner of the actor
	 *     SEND_FUSIONRPC(TargetAllClients)      -> everyone, including the caller
	 *     SEND_FUSIONRPC(TargetEveryoneElse)    -> everyone except the caller
	 *
	 * These are NOT C++ expressions. FusionMacros.h defines SEND_FUSIONRPC(...)
	 * as `;`, so the compiler never sees the argument at all. The token is
	 * consumed by PhotonFusionUbtPlugin, which registers each name as a UHT
	 * *function specifier* (FusionUhtFunctionSpecifiers.cs) and reads it
	 * straight out of the source text before the preprocessor runs.
	 *
	 * Consequences:
	 *   - A #define alias will NOT work. UHT would see the alias name and fail
	 *     to match any specifier.
	 *   - EFusionRPCTarget::SendToMasterClient will NOT work here either. That
	 *     enum is what the generator EMITS into the .fusion.gen.cpp; it is not
	 *     the input spelling.
	 *
	 * The docs show both spellings in different places. The source is the
	 * authority, and the source says: bare specifier tokens.
	 */

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

	// Expands to a harmless self-include-free token in offline builds.
	#define CS_FUSION_INLINE_GENERATED_CPP(ClassName) "Core/CSFusionCompatNoop.h"

#endif // CS_WITH_FUSION
