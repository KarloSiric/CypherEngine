//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier2/CypherCommon_Settings.cpp
//  Purpose: Implements typed decoding for Cypher user and machine settings.
//  Details: Decoding starts from compiled defaults and applies each valid value.
//           An invalid value keeps its default and is reported as a warning, so
//           one mistake in a hand-edited file never discards the rest of it.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//  - Rebuilt on setting descriptors for tolerant V1/V2 decoding on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Settings.h"

#include "CypherCommon_StringView.h"

namespace cypher::common
{

namespace
{

template <usize nExtent>
CYPHER_NODISCARD constexpr string_view_t SettingsText(
    const char ( &text )[nExtent] ) noexcept
{
    // Static field names become borrowed views without their trailing NUL.
    static_assert( nExtent > 0u );
    return { text, nExtent - 1u };
}

enum display_setting_t : usize {
    DISPLAY_WIDTH = 0u,
    DISPLAY_HEIGHT,
    DISPLAY_MODE,
    DISPLAY_VSYNC,
    DISPLAY_SETTING_COUNT
};

inline constexpr const char *g_displayModeNames[]{ "windowed", "borderless", "fullscreen" };

CYPHER_NODISCARD constexpr setting_descriptor_t IntegerSetting(
    const char *pPath, i64 nDefault, i64 nMin, i64 nMax, const char *pLabel ) noexcept
{
    setting_descriptor_t descriptor{};
    descriptor.pPath = pPath;
    descriptor.type = setting_type_t::INTEGER;
    descriptor.nDefault = nDefault;
    descriptor.nMin = nMin;
    descriptor.nMax = nMax;
    descriptor.pLabel = pLabel;
    descriptor.pPage = "Engine/Display";
    return descriptor;
}

CYPHER_NODISCARD constexpr setting_descriptor_t DisplayModeSetting() noexcept
{
    setting_descriptor_t descriptor{};
    descriptor.pPath = "display.mode";
    descriptor.type = setting_type_t::ENUM;
    descriptor.pDefaultText = "windowed";
    descriptor.ppEnumValues = g_displayModeNames;
    descriptor.nEnumValues = sizeof( g_displayModeNames ) / sizeof( g_displayModeNames[0] );
    descriptor.pLabel = "Window mode";
    descriptor.pPage = "Engine/Display";
    return descriptor;
}

CYPHER_NODISCARD constexpr setting_descriptor_t VSyncSetting() noexcept
{
    setting_descriptor_t descriptor{};
    descriptor.pPath = "display.vsync";
    descriptor.type = setting_type_t::BOOL;
    descriptor.bDefault = CY_TRUE;
    descriptor.pLabel = "Vertical sync";
    descriptor.pPage = "Engine/Display";
    return descriptor;
}

// Defaults here must match the cypher_settings_t member initializers; the
// settings tests check both against each other.
inline constexpr setting_descriptor_t g_displaySettings[DISPLAY_SETTING_COUNT]{
    IntegerSetting( "display.width", CY_SETTINGS_DEFAULT_DISPLAY_WIDTH,
                    CY_SETTINGS_DISPLAY_WIDTH_MIN, CY_SETTINGS_DISPLAY_WIDTH_MAX, "Width" ),
    IntegerSetting( "display.height", CY_SETTINGS_DEFAULT_DISPLAY_HEIGHT,
                    CY_SETTINGS_DISPLAY_HEIGHT_MIN, CY_SETTINGS_DISPLAY_HEIGHT_MAX, "Height" ),
    DisplayModeSetting(),
    VSyncSetting()
};

void EmitDiagnostic(
    cypher_settings_decode_result_t &result,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity,
    schema_diagnostic_code_t code,
    schema_diagnostic_severity_t severity,
    const char *pPath ) noexcept
{
    ++result.validation.nDiagnosticsRequired;
    if ( severity == schema_diagnostic_severity_t::ERROR ) {
        ++result.validation.nErrors;
    } else {
        ++result.validation.nWarnings;
    }
    if ( result.validation.nDiagnosticsWritten >= nDiagnosticCapacity ) {
        result.validation.bDiagnosticsTruncated = CY_TRUE;
        return;
    }
    schema_diagnostic_t &diagnostic = pDiagnostics[result.validation.nDiagnosticsWritten++];
    diagnostic = {};
    diagnostic.code = code;
    diagnostic.severity = severity;
    usize iChar = 0u;
    for ( ; pPath[iChar] != '\0' && iChar + 1u < CY_SCHEMA_MAX_PATH; ++iChar ) {
        diagnostic.path[iChar] = pPath[iChar];
    }
    diagnostic.path[iChar] = '\0';
}

CYPHER_NODISCARD schema_diagnostic_code_t DiagnosticForProblem( setting_problem_code_t problem ) noexcept
{
    switch ( problem ) {
        case setting_problem_code_t::OUT_OF_RANGE: return schema_diagnostic_code_t::I64_RANGE;
        case setting_problem_code_t::UNKNOWN_ENUM: return schema_diagnostic_code_t::STRING_VALUE;
        case setting_problem_code_t::TEXT_TOO_LONG: return schema_diagnostic_code_t::STRING_LENGTH;
        default: return schema_diagnostic_code_t::TYPE_MISMATCH;
    }
}

} // namespace

cypher_settings_t CypherSettings_Defaults() noexcept
{
    // Default member initializers are the single authoritative fallback set.
    return {};
}

settings_document_identity_t CypherSettings_Identity() noexcept
{
    return { SettingsText( "cypher.settings" ), CY_SETTINGS_SCHEMA_OLDEST_VERSION, CY_SETTINGS_SCHEMA_VERSION };
}

const setting_descriptor_t *CypherSettings_DisplayDescriptors( usize *pCountOut ) noexcept
{
    if ( pCountOut != nullptr ) {
        *pCountOut = DISPLAY_SETTING_COUNT;
    }
    return g_displaySettings;
}

cypher_settings_decode_result_t CypherSettings_Decode(
    const key_value_document_t *pDocument,
    const schema_validation_options_t &options,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity,
    cypher_settings_t *pSettingsOut ) noexcept
{
    ( void )options; // Values are checked one by one; no structural pass is needed.
    cypher_settings_decode_result_t result{};
    if ( pDocument == nullptr || pSettingsOut == nullptr ||
         ( pDiagnostics == nullptr && nDiagnosticCapacity != 0u ) ) {
        result.status = cypher_settings_status_t::INVALID_ARGUMENT;
        result.validation.status = schema_validation_status_t::INVALID_ARGUMENT;
        return result;
    }

    // Identity is the only whole-document failure: another schema is not a
    // settings file at all, so nothing in it can be trusted.
    const key_value_document_header_t header = KeyValue_DocumentHeader( pDocument );
    const settings_document_identity_t identity = CypherSettings_Identity();
    if ( header.nLanguageVersion != CYKV_LANGUAGE_VERSION ) {
        EmitDiagnostic( result, pDiagnostics, nDiagnosticCapacity,
                        schema_diagnostic_code_t::LANGUAGE_VERSION_MISMATCH,
                        schema_diagnostic_severity_t::ERROR, "" );
    } else if ( !StringView_Equals( header.schemaId, identity.schemaId ) ) {
        EmitDiagnostic( result, pDiagnostics, nDiagnosticCapacity,
                        schema_diagnostic_code_t::SCHEMA_ID_MISMATCH,
                        schema_diagnostic_severity_t::ERROR, "" );
    } else if ( header.nSchemaVersion < identity.nOldestVersion ||
                header.nSchemaVersion > identity.nCurrentVersion ) {
        EmitDiagnostic( result, pDiagnostics, nDiagnosticCapacity,
                        schema_diagnostic_code_t::SCHEMA_VERSION_MISMATCH,
                        schema_diagnostic_severity_t::ERROR, "" );
    } else if ( KeyValue_Type( KeyValue_Root( pDocument ) ) != key_value_type_t::OBJECT ) {
        EmitDiagnostic( result, pDiagnostics, nDiagnosticCapacity,
                        schema_diagnostic_code_t::TYPE_MISMATCH,
                        schema_diagnostic_severity_t::ERROR, "" );
    }
    if ( result.validation.nErrors != 0u ) {
        result.status = cypher_settings_status_t::INVALID_DOCUMENT;
        result.validation.status = schema_validation_status_t::INVALID_DOCUMENT;
        return result;
    }

    constexpr const char *kDiagnosticPaths[DISPLAY_SETTING_COUNT]{
        "/display/width", "/display/height", "/display/mode", "/display/vsync"
    };
    const key_value_t *pRoot = KeyValue_Root( pDocument );
    setting_value_t values[DISPLAY_SETTING_COUNT]{};
    for ( usize iSetting = 0u; iSetting < DISPLAY_SETTING_COUNT; ++iSetting ) {
        setting_problem_code_t problem = setting_problem_code_t::NONE;
        const setting_read_status_t status = Setting_Read(
            pRoot, g_displaySettings[iSetting], &values[iSetting], &problem );
        if ( status != setting_read_status_t::VALUE ) {
            values[iSetting] = Setting_Default( g_displaySettings[iSetting] );
        }
        if ( status == setting_read_status_t::INVALID ) {
            EmitDiagnostic( result, pDiagnostics, nDiagnosticCapacity,
                            DiagnosticForProblem( problem ),
                            schema_diagnostic_severity_t::WARNING,
                            kDiagnosticPaths[iSetting] );
        }
    }

    cypher_settings_t settings = CypherSettings_Defaults();
    // Descriptor limits already bound width and height to positive u32 values.
    settings.nDisplayWidth = static_cast<u32>( values[DISPLAY_WIDTH].nValue );
    settings.nDisplayHeight = static_cast<u32>( values[DISPLAY_HEIGHT].nValue );
    settings.displayMode =
        StringView_Equals( values[DISPLAY_MODE].text, SettingsText( "borderless" ) ) ? settings_display_mode_t::BORDERLESS
      : StringView_Equals( values[DISPLAY_MODE].text, SettingsText( "fullscreen" ) ) ? settings_display_mode_t::FULLSCREEN
      : settings_display_mode_t::WINDOWED;
    settings.bVSync = values[DISPLAY_VSYNC].bValue;
    *pSettingsOut = settings;
    return result;
}

bool_t CypherSettings_DecodeSucceeded(
    const cypher_settings_decode_result_t &result ) noexcept
{
    return result.status == cypher_settings_status_t::OK;
}

const char *CypherSettings_DisplayModeName(
    settings_display_mode_t mode ) noexcept
{
    switch ( mode ) {
        case settings_display_mode_t::WINDOWED: return "windowed";
        case settings_display_mode_t::BORDERLESS: return "borderless";
        case settings_display_mode_t::FULLSCREEN: return "fullscreen";
    }
    return "unknown";
}

const char *CypherSettings_StatusName(
    cypher_settings_status_t status ) noexcept
{
    switch ( status ) {
        case cypher_settings_status_t::OK: return "OK";
        case cypher_settings_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case cypher_settings_status_t::INVALID_DOCUMENT: return "INVALID_DOCUMENT";
        case cypher_settings_status_t::INTERNAL_ERROR: return "INTERNAL_ERROR";
    }
    return "UNKNOWN";
}

} // namespace cypher::common
