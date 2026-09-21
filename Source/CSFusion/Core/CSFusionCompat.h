// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Single include point for the Photon Fusion 3 Unreal SDK.
//
// Include this instead of the individual Fusion headers, so that an SDK
// reshuffle during the 3.0 preview is a one-file fix.
//
// ---------------------------------------------------------------------------
// NEVER wrap a reflected declaration in a project-specific #if.
//
// UnrealHeaderTool understands only a fixed set of preprocessor conditions:
// CPP, !CPP, 0, 1, WITH_EDITOR, WITH_EDITORONLY_DATA, WITH_ENGINE,
// WITH_COREUOBJECT, WITH_HOT_RELOAD, WITH_VERSE_VM, WITH_VERSE_BPVM,
// WITH_TESTS. Anything else becomes UhtCompilerDirective.Unrecognized and
// UhtHeaderFileParser.IncludeCurrentCompilerDirective() skips the block, so
// UHT and PhotonFusionUbtPlugin never see what is inside it. A UPROPERTY put
// there is not GC-tracked, a UFUNCTION cannot be bound with AddDynamic, and a
// SEND_FUSIONRPC never gets a generated body.
//
// This is why the SDK is a hard dependency (see CSFusion.Build.cs) and why
// CS_WITH_FUSION is always 1. #if CS_WITH_FUSION is still legal in .cpp files,
// which UHT does not parse, but there is currently no reason to use it.
// ---------------------------------------------------------------------------
//
// RPC TARGETS - read before writing a SEND_FUSIONRPC.
//
// Write ONE of these four bare tokens, and nothing else:
//
//     SEND_FUSIONRPC(TargetMasterClient)    -> the current Master Client
//     SEND_FUSIONRPC(TargetObjectOwner)     -> the resolved owner of the actor
//     SEND_FUSIONRPC(TargetAllClients)      -> everyone, including the caller
//     SEND_FUSIONRPC(TargetEveryoneElse)    -> everyone except the caller
//
// These are not C++ expressions. FusionMacros.h defines SEND_FUSIONRPC(...)
// as `;`, so the compiler never sees the argument. The token is consumed by
// PhotonFusionUbtPlugin, which registers each name as a UHT function
// specifier (FusionUhtFunctionSpecifiers.cs) and reads it out of the source
// text before the preprocessor runs. A #define alias therefore cannot work,
// and EFusionRPCTarget::SendToMasterClient is the output spelling the
// generator emits, not the input spelling.
//
// ---------------------------------------------------------------------------
// Per-class checklist for a Fusion RPC
//
//   Header:
//     #include "MyClass.fusion.h"     // BEFORE MyClass.generated.h
//     #include "MyClass.generated.h"
//     ...
//     GENERATED_BODY()
//     FUSION_BODY();
//     SEND_FUSIONRPC(TargetMasterClient)
//     void RpcDoThing(int32 Arg);     // body is generated - never define it
//     void RpcDoThing_Receive(int32 Arg);   // you implement this
//
//   Source (required from UE 5.8 - generated .gen.cpp files are no longer
//   auto-scanned):
//     #include "MyClass.h"
//     #include UE_INLINE_GENERATED_CPP_BY_NAME(MyClass.fusion)
// ---------------------------------------------------------------------------

#pragma once

#include "CoreMinimal.h"

#ifndef CS_WITH_FUSION
	#define CS_WITH_FUSION 1
#endif

#include "FusionMacros.h"
#include "FusionHelpers.h"
#include "FusionOnlineSubsystem.h"
#include "FusionActorComponent.h"
#include "FusionRealtimeClient.h"
