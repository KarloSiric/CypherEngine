//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier2/CypherCommon_SettingsSchema.h
//  Purpose: Declares the CYKV schema for user and machine settings.
//  Details: The settings contract covers writable local preferences rather than
//           durable project identity. Missing optional values are supplied by the
//           typed settings decoder.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER2_SETTINGSSCHEMA_H
#define CYPHER_COMMON_TIER2_SETTINGSSCHEMA_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Schema.h"

namespace cypher::common
{

inline constexpr u32 CY_SETTINGS_SCHEMA_VERSION = 1u;
inline constexpr i64 CY_SETTINGS_DISPLAY_WIDTH_MIN = 320;
inline constexpr i64 CY_SETTINGS_DISPLAY_WIDTH_MAX = 16384;
inline constexpr i64 CY_SETTINGS_DISPLAY_HEIGHT_MIN = 200;
inline constexpr i64 CY_SETTINGS_DISPLAY_HEIGHT_MAX = 16384;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const schema_descriptor_t *SettingsSchema_V1() noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER2_SETTINGSSCHEMA_H
