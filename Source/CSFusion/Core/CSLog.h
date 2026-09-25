// Copyright (c) 2026 CS-Fusion. All Rights Reserved.
//
// Log categories. Never log Epic access tokens, login tokens (JWT), client
// secrets, the service key or anything else that grants access - not even in
// Development builds. Log ids and outcomes instead.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/** General gameplay log. */
CSFUSION_API DECLARE_LOG_CATEGORY_EXTERN(LogCS, Log, All);

/** Networking / Photon Fusion session log. */
CSFUSION_API DECLARE_LOG_CATEGORY_EXTERN(LogCSNet, Log, All);

/**
 * Authentication (Epic sign-in, backend sessions) and, until the Phase 2
 * split into LogCSSecurity, authority arbitration and anti-cheat decisions.
 */
CSFUSION_API DECLARE_LOG_CATEGORY_EXTERN(LogCSAuth, Log, All);

/** Combat: weapons, damage, death. */
CSFUSION_API DECLARE_LOG_CATEGORY_EXTERN(LogCSCombat, Log, All);

/** Inventory, items, pickups and loot. */
CSFUSION_API DECLARE_LOG_CATEGORY_EXTERN(LogCSInventory, Log, All);

/** Bots and AI. */
CSFUSION_API DECLARE_LOG_CATEGORY_EXTERN(LogCSAI, Log, All);

// --- v2.0 ---------------------------------------------------------------------

/** Session, rooms, replication and RPC traffic (successor of LogCSNet). */
CSFUSION_API DECLARE_LOG_CATEGORY_EXTERN(LogCSNetwork, Log, All);

/** Administration: admin panel calls and their results. */
CSFUSION_API DECLARE_LOG_CATEGORY_EXTERN(LogCSAdmin, Log, All);

/** Anti-cheat: rejected requests, strikes, suspensions, suspicious reports. */
CSFUSION_API DECLARE_LOG_CATEGORY_EXTERN(LogCSSecurity, Log, All);

/** Weapon handling: fire, recoil, spread, reload decisions. */
CSFUSION_API DECLARE_LOG_CATEGORY_EXTERN(LogCSWeapon, Log, All);

/** Frame times, memory and other performance samples. */
CSFUSION_API DECLARE_LOG_CATEGORY_EXTERN(LogCSPerformance, Log, All);
