//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier2/CypherCommon_Settings.h
//  Purpose: Declares typed user and machine settings.
//  Details: Settings decode into an owning value type with deterministic compiled
//           defaults. No returned field borrows storage from the source document.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER2_SETTINGS_H
#define CYPHER_COMMON_TIER2_SETTINGS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_SettingsSchema.h"

namespace cypher::common
{

inline constexpr u32 CY_SETTINGS_DEFAULT_DISPLAY_WIDTH = 1280u;
inline constexpr u32 CY_SETTINGS_DEFAULT_DISPLAY_HEIGHT = 720u;

enum class settings_display_mode_t : u8 {
    WINDOWED = 0u,
    BORDERLESS,
    FULLSCREEN
};

/*
================
User Settings Value

Unlike project_manifest_view_t, this type owns all of its state and remains valid
after the parsed CYKV document is destroyed. Default member values are the canonical
fallback when the local settings file or individual optional fields are absent.
================
*/
struct cypher_settings_t {
    u32 nDisplayWidth{ CY_SETTINGS_DEFAULT_DISPLAY_WIDTH };
    u32 nDisplayHeight{ CY_SETTINGS_DEFAULT_DISPLAY_HEIGHT };
    settings_display_mode_t displayMode{ settings_display_mode_t::WINDOWED };
    bool_t bVSync{ CY_TRUE };
};

enum class cypher_settings_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    INVALID_DOCUMENT,
    INTERNAL_ERROR
};

struct cypher_settings_decode_result_t {
    cypher_settings_status_t status{ cypher_settings_status_t::OK };
    schema_validation_result_t validation{};
};

// Returns the deterministic settings used when no local settings file exists.
CYPHER_NODISCARD CYPHER_COMMON_API
cypher_settings_t CypherSettings_Defaults() noexcept;

// Validates and applies optional overrides transactionally to the compiled defaults.
// pSettingsOut is modified only when the entire operation succeeds.
CYPHER_NODISCARD CYPHER_COMMON_API
cypher_settings_decode_result_t CypherSettings_Decode(
    const key_value_document_t *pDocument,
    const schema_validation_options_t &options,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity,
    cypher_settings_t *pSettingsOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CypherSettings_DecodeSucceeded(
    const cypher_settings_decode_result_t &result ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *CypherSettings_DisplayModeName(
    settings_display_mode_t mode ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *CypherSettings_StatusName(
    cypher_settings_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER2_SETTINGS_H
