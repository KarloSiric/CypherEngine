//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier2/CypherCommon_Tier2_Settings_Tests.cpp
//  Purpose: Tests the cypher.settings schema and typed settings decoder.
//  Details: Covers compiled defaults, partial overrides, all display modes, strict
//           validation, diagnostic paths, and transactional output behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_KeyValueParser.h"
#include "CypherCommon_Settings.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

key_value_document_t *ParseSettings( const char *pSource )
{
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    REQUIRE( pDocument != nullptr );
    const key_value_parse_result_t result = KeyValue_ParseText(
        StringView_FromCString( pSource ),
        {},
        pDocument );
    REQUIRE( result.status == key_value_parse_status_t::OK );
    return pDocument;
}

} // namespace

TEST_CASE( "Tier2 settings use deterministic defaults",
           "[CypherCommon][Tier2][Settings]" )
{
    const cypher_settings_t defaults = CypherSettings_Defaults();
    REQUIRE( defaults.nDisplayWidth == CY_SETTINGS_DEFAULT_DISPLAY_WIDTH );
    REQUIRE( defaults.nDisplayHeight == CY_SETTINGS_DEFAULT_DISPLAY_HEIGHT );
    REQUIRE( defaults.displayMode == settings_display_mode_t::WINDOWED );
    REQUIRE( defaults.bVSync == CY_TRUE );

    key_value_document_t *pDocument = ParseSettings(
        "@cykv 1\n@schema \"cypher.settings\" 1\n{}" );
    cypher_settings_t settings{};
    const cypher_settings_decode_result_t result = CypherSettings_Decode(
        pDocument,
        {},
        nullptr,
        0u,
        &settings );

    REQUIRE( CypherSettings_DecodeSucceeded( result ) );
    REQUIRE( settings.nDisplayWidth == defaults.nDisplayWidth );
    REQUIRE( settings.nDisplayHeight == defaults.nDisplayHeight );
    REQUIRE( settings.displayMode == defaults.displayMode );
    REQUIRE( settings.bVSync == defaults.bVSync );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Tier2 settings apply optional display overrides",
           "[CypherCommon][Tier2][Settings]" )
{
    key_value_document_t *pDocument = ParseSettings( R"cykv(@cykv 1
@schema "cypher.settings" 1
{
    display = {
        width = 2560
        height = 1440
        mode = "borderless"
        vsync = false
    }
}
)cykv" );
    cypher_settings_t settings{};
    const cypher_settings_decode_result_t result = CypherSettings_Decode(
        pDocument,
        {},
        nullptr,
        0u,
        &settings );

    REQUIRE( CypherSettings_DecodeSucceeded( result ) );
    REQUIRE( settings.nDisplayWidth == 2560u );
    REQUIRE( settings.nDisplayHeight == 1440u );
    REQUIRE( settings.displayMode == settings_display_mode_t::BORDERLESS );
    REQUIRE( settings.bVSync == CY_FALSE );
    REQUIRE( StringView_Equals(
        StringView_FromCString(
            CypherSettings_DisplayModeName( settings.displayMode ) ),
        StringView_FromCString( "borderless" ) ) );
    KeyValue_DestroyDocument( pDocument );

    pDocument = ParseSettings(
        "@cykv 1\n@schema \"cypher.settings\" 1\n"
        "{ display = { width = 1600 } }" );
    settings = {};
    const cypher_settings_decode_result_t partialResult =
        CypherSettings_Decode(
            pDocument,
            {},
            nullptr,
            0u,
            &settings );
    REQUIRE( CypherSettings_DecodeSucceeded( partialResult ) );
    REQUIRE( settings.nDisplayWidth == 1600u );
    REQUIRE( settings.nDisplayHeight == CY_SETTINGS_DEFAULT_DISPLAY_HEIGHT );
    REQUIRE( settings.displayMode == settings_display_mode_t::WINDOWED );
    REQUIRE( settings.bVSync == CY_TRUE );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Tier2 settings reject invalid values without partial output",
           "[CypherCommon][Tier2][Settings][Transaction]" )
{
    key_value_document_t *pDocument = ParseSettings(
        "@cykv 1\n@schema \"cypher.settings\" 1\n"
        "{ display = { width = 100 mode = \"exclusive\" mystery = true } }" );
    cypher_settings_t settings{};
    settings.nDisplayWidth = 800u;
    settings.nDisplayHeight = 600u;
    settings.displayMode = settings_display_mode_t::FULLSCREEN;
    settings.bVSync = CY_FALSE;

    schema_diagnostic_t diagnostics[4]{};
    const cypher_settings_decode_result_t result = CypherSettings_Decode(
        pDocument,
        {},
        diagnostics,
        sizeof( diagnostics ) / sizeof( diagnostics[0] ),
        &settings );

    REQUIRE( result.status == cypher_settings_status_t::INVALID_DOCUMENT );
    REQUIRE( result.validation.nErrors == 3u );
    REQUIRE( settings.nDisplayWidth == 800u );
    REQUIRE( settings.nDisplayHeight == 600u );
    REQUIRE( settings.displayMode == settings_display_mode_t::FULLSCREEN );
    REQUIRE( settings.bVSync == CY_FALSE );
    REQUIRE( StringView_Equals(
        StringView_FromCString( CypherSettings_StatusName( result.status ) ),
        StringView_FromCString( "INVALID_DOCUMENT" ) ) );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Tier2 settings validate their descriptor and arguments",
           "[CypherCommon][Tier2][Settings]" )
{
    REQUIRE( Schema_CheckDescriptor( SettingsSchema_V1() ) ==
             schema_descriptor_status_t::OK );

    cypher_settings_t settings{};
    const cypher_settings_decode_result_t result = CypherSettings_Decode(
        nullptr,
        {},
        nullptr,
        0u,
        &settings );
    REQUIRE( result.status == cypher_settings_status_t::INVALID_ARGUMENT );
    REQUIRE( result.validation.status ==
             schema_validation_status_t::INVALID_ARGUMENT );
}
