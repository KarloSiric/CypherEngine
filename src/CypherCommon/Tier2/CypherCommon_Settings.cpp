//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier2/CypherCommon_Settings.cpp
//  Purpose: Implements typed decoding for Cypher user and machine settings.
//  Details: Decoding starts from compiled defaults, applies only validated fields,
//           and commits once complete so invalid local files cannot partially alter
//           the caller's active settings.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
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

CYPHER_NODISCARD bool_t ReadOptionalU32(
    const key_value_t *pObject,
    string_view_t name,
    u32 &valueOut ) noexcept
{
    const key_value_t *pValue = KeyValue_Find( pObject, name );
    if ( pValue == nullptr ) {
        return CY_TRUE;
    }

    // Schema validation has already constrained this signed value to the positive
    // display range, making the u32 conversion explicit and lossless.
    i64 nValue = 0;
    if ( !KeyValue_GetI64( pValue, &nValue ) ) {
        return CY_FALSE;
    }
    valueOut = static_cast<u32>( nValue );
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ReadOptionalBool(
    const key_value_t *pObject,
    string_view_t name,
    bool_t &valueOut ) noexcept
{
    const key_value_t *pValue = KeyValue_Find( pObject, name );
    return pValue == nullptr || KeyValue_GetBool( pValue, &valueOut );
}

CYPHER_NODISCARD bool_t ReadOptionalDisplayMode(
    const key_value_t *pDisplay,
    settings_display_mode_t &modeOut ) noexcept
{
    const key_value_t *pValue = KeyValue_Find(
        pDisplay,
        SettingsText( "mode" ) );
    if ( pValue == nullptr ) {
        return CY_TRUE;
    }

    string_view_t mode{};
    if ( !KeyValue_GetString( pValue, &mode ) ) {
        return CY_FALSE;
    }
    if ( StringView_Equals( mode, SettingsText( "windowed" ) ) ) {
        modeOut = settings_display_mode_t::WINDOWED;
        return CY_TRUE;
    }
    if ( StringView_Equals( mode, SettingsText( "borderless" ) ) ) {
        modeOut = settings_display_mode_t::BORDERLESS;
        return CY_TRUE;
    }
    if ( StringView_Equals( mode, SettingsText( "fullscreen" ) ) ) {
        modeOut = settings_display_mode_t::FULLSCREEN;
        return CY_TRUE;
    }
    return CY_FALSE;
}

} // namespace

cypher_settings_t CypherSettings_Defaults() noexcept
{
    // Default member initializers are the single authoritative fallback set.
    return {};
}

cypher_settings_decode_result_t CypherSettings_Decode(
    const key_value_document_t *pDocument,
    const schema_validation_options_t &options,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity,
    cypher_settings_t *pSettingsOut ) noexcept
{
    cypher_settings_decode_result_t result{};
    if ( pDocument == nullptr || pSettingsOut == nullptr ||
         ( pDiagnostics == nullptr && nDiagnosticCapacity != 0u ) ) {
        result.status = cypher_settings_status_t::INVALID_ARGUMENT;
        result.validation.status = schema_validation_status_t::INVALID_ARGUMENT;
        return result;
    }

    // Structural validation runs first so an extraction failure below indicates an
    // internal contract violation rather than malformed user input.
    result.validation = Schema_ValidateDocument(
        SettingsSchema_V1(),
        pDocument,
        options,
        pDiagnostics,
        nDiagnosticCapacity );
    if ( !Schema_ValidationSucceeded( result.validation ) ) {
        result.status = cypher_settings_status_t::INVALID_DOCUMENT;
        return result;
    }

    // Decode locally from defaults. Optional members replace only their matching
    // values, and a later failure cannot leak partially applied settings.
    cypher_settings_t settings = CypherSettings_Defaults();
    const key_value_t *pRoot = KeyValue_Root( pDocument );
    const key_value_t *pDisplay = KeyValue_Find(
        pRoot,
        SettingsText( "display" ) );
    if ( pDisplay != nullptr &&
         ( !ReadOptionalU32(
               pDisplay,
               SettingsText( "width" ),
               settings.nDisplayWidth ) ||
           !ReadOptionalU32(
               pDisplay,
               SettingsText( "height" ),
               settings.nDisplayHeight ) ||
           !ReadOptionalDisplayMode( pDisplay, settings.displayMode ) ||
           !ReadOptionalBool(
               pDisplay,
               SettingsText( "vsync" ),
               settings.bVSync ) ) ) {
        result.status = cypher_settings_status_t::INTERNAL_ERROR;
        return result;
    }

    // Single transaction commit point.
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
