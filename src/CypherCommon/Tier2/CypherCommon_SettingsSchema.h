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
//  - Added the open V2 schema on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Settings Schema Contract

The settings schema defines accepted keys, types, ranges, and defaults independently of any UI.
Unknown or incompatible values are diagnosed at their original source location.
================
*/

#ifndef CYPHER_COMMON_TIER2_SETTINGSSCHEMA_H
#define CYPHER_COMMON_TIER2_SETTINGSSCHEMA_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Schema.h"

namespace cypher::common
{

inline constexpr u32 CY_SETTINGS_SCHEMA_VERSION = 2u; // cypher.settings generation written.
inline constexpr u32 CY_SETTINGS_SCHEMA_OLDEST_VERSION = 1u; // Oldest generation still read.
inline constexpr i64 CY_SETTINGS_DISPLAY_WIDTH_MIN = 320; // Inclusive pixels.
inline constexpr i64 CY_SETTINGS_DISPLAY_WIDTH_MAX = 16384; // Inclusive pixels.
inline constexpr i64 CY_SETTINGS_DISPLAY_HEIGHT_MIN = 200; // Inclusive pixels.
inline constexpr i64 CY_SETTINGS_DISPLAY_HEIGHT_MAX = 16384; // Inclusive pixels.

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const schema_descriptor_t *SettingsSchema_V1() noexcept;

// V2 (ADR 0009): an open root of section objects. Unknown sections and
// members are allowed so a file shared by many modules, plugins, and newer
// builds never fails as a whole; typed reads validate each known value.
CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const schema_descriptor_t *SettingsSchema_V2() noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER2_SETTINGSSCHEMA_H
