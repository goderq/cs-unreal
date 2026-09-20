// Copyright (c) 2026 CS-Fusion. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/** General gameplay log. */
CSFUSION_API DECLARE_LOG_CATEGORY_EXTERN(LogCS, Log, All);

/** Networking / Photon Fusion session log. */
CSFUSION_API DECLARE_LOG_CATEGORY_EXTERN(LogCSNet, Log, All);

/** Authority arbitration + anti-cheat rejections. */
CSFUSION_API DECLARE_LOG_CATEGORY_EXTERN(LogCSAuth, Log, All);

/** Combat: weapons, damage, death. */
CSFUSION_API DECLARE_LOG_CATEGORY_EXTERN(LogCSCombat, Log, All);

/** Inventory, items, pickups and loot. */
CSFUSION_API DECLARE_LOG_CATEGORY_EXTERN(LogCSInventory, Log, All);

/** Bots and AI. */
CSFUSION_API DECLARE_LOG_CATEGORY_EXTERN(LogCSAI, Log, All);
