//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier2/CypherCommon_Settings.h
//  Purpose: Declares typed user and machine settings.
//  Details: Settings decode into an owning value type with deterministic compiled
//           defaults. No returned field borrows storage from the source document.
//           Decoding is tolerant (ADR 0009): each value is checked on its own,
//           an invalid one falls back to its default with a warning, and
//           unknown sections and members are ignored so they survive in the
//           file.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//  - Made decoding tolerant and added V2 on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER2_SETTINGS_H
#define CYPHER_COMMON_TIER2_SETTINGS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_SettingsDocument.h"
#include "CypherCommon_SettingsSchema.h"

namespace cypher::common
{

inline constexpr u32 CY_SETTINGS_DEFAULT_DISPLAY_WIDTH = 1280u; // Missing-file default.
inline constexpr u32 CY_SETTINGS_DEFAULT_DISPLAY_HEIGHT = 720u; // Missing-file default.

enum class settings_display_mode_t : u8 {
    WINDOWED = 0u, // Decorated resizable OS window.
    BORDERLESS,    // Desktop-sized borderless window.
    FULLSCREEN     // Exclusive fullscreen request.
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
    u32 nDisplayWidth{ CY_SETTINGS_DEFAULT_DISPLAY_WIDTH }; // Requested pixel width.
    u32 nDisplayHeight{ CY_SETTINGS_DEFAULT_DISPLAY_HEIGHT }; // Requested pixel height.
    settings_display_mode_t displayMode{ settings_display_mode_t::WINDOWED }; // Window mode.
    bool_t bVSync{ CY_TRUE }; // Synchronize presentation when the backend supports it.
};

enum class cypher_settings_status_t : u8 {
    OK = 0u,          // Settings decoded; invalid values fell back with warnings.
    INVALID_ARGUMENT,// Document, diagnostics, or output argument is invalid.
    INVALID_DOCUMENT,// Wrong schema identity or a root that is not an object.
    INTERNAL_ERROR   // Reserved; no longer produced.
};

struct cypher_settings_decode_result_t {
    cypher_settings_status_t status{ cypher_settings_status_t::OK }; // Decode result.
    schema_validation_result_t validation{}; // Header errors and per-value warnings.
};

// Returns the deterministic settings used when no local settings file exists.
CYPHER_NODISCARD CYPHER_COMMON_API
cypher_settings_t CypherSettings_Defaults() noexcept;

// Identity for settings stores: reads cypher.settings V1-V2, writes V2.
CYPHER_NODISCARD CYPHER_COMMON_API
settings_document_identity_t CypherSettings_Identity() noexcept;

// Descriptors of the engine's display section, in declaration order.
CYPHER_NODISCARD CYPHER_COMMON_API
const setting_descriptor_t *CypherSettings_DisplayDescriptors( usize *pCountOut ) noexcept;

// Applies every valid display value over the compiled defaults. An invalid
// value is reported as a WARNING diagnostic and keeps its default; only a
// wrong header or non-object root fails, and then pSettingsOut is unchanged.
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
