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

TEST_CASE( "Tier2 settings keep valid values when others are invalid",
           "[CypherCommon][Tier2][Settings]" )
{
    // ADR 0009: one bad value falls back alone; the rest of the file applies
    // and unknown members are ignored rather than failing the document.
    key_value_document_t *pDocument = ParseSettings(
        "@cykv 1\n@schema \"cypher.settings\" 1\n"
        "{ display = { width = 100 height = 900 mode = \"exclusive\" vsync = false mystery = true } }" );
    cypher_settings_t settings{};
    settings.nDisplayWidth = 800u;
    settings.displayMode = settings_display_mode_t::FULLSCREEN;

    schema_diagnostic_t diagnostics[4]{};
    const cypher_settings_decode_result_t result = CypherSettings_Decode(
        pDocument,
        {},
        diagnostics,
        sizeof( diagnostics ) / sizeof( diagnostics[0] ),
        &settings );

    REQUIRE( result.status == cypher_settings_status_t::OK );
    REQUIRE( result.validation.nErrors == 0u );
    REQUIRE( result.validation.nWarnings == 2u );
    REQUIRE( diagnostics[0].severity == schema_diagnostic_severity_t::WARNING );
    REQUIRE( diagnostics[0].code == schema_diagnostic_code_t::I64_RANGE );
    REQUIRE( StringView_Equals( StringView_FromCString( diagnostics[0].path ),
                                StringView_FromCString( "/display/width" ) ) );
    REQUIRE( diagnostics[1].code == schema_diagnostic_code_t::STRING_VALUE );
    REQUIRE( settings.nDisplayWidth == CY_SETTINGS_DEFAULT_DISPLAY_WIDTH );
    REQUIRE( settings.nDisplayHeight == 900u );
    REQUIRE( settings.displayMode == settings_display_mode_t::WINDOWED );
    REQUIRE( settings.bVSync == CY_FALSE );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Tier2 settings read V2 files with other modules' sections",
           "[CypherCommon][Tier2][Settings]" )
{
    key_value_document_t *pDocument = ParseSettings( R"cykv(@cykv 1
@schema "cypher.settings" 2
{
    display = { width = 1920 height = 1080 mode = "fullscreen" }
    editor = { viewport = { grid_size = 16 } }
    plugin_from_the_future = [ 1, 2, 3 ]
}
)cykv" );
    cypher_settings_t settings{};
    const cypher_settings_decode_result_t result =
        CypherSettings_Decode( pDocument, {}, nullptr, 0u, &settings );
    REQUIRE( result.status == cypher_settings_status_t::OK );
    REQUIRE( result.validation.nWarnings == 0u );
    REQUIRE( settings.nDisplayWidth == 1920u );
    REQUIRE( settings.displayMode == settings_display_mode_t::FULLSCREEN );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Tier2 settings reject another schema or an unknown version",
           "[CypherCommon][Tier2][Settings]" )
{
    for ( const char *pSource : {
              "@cykv 1\n@schema \"cypher.project\" 1\n{}",
              "@cykv 1\n@schema \"cypher.settings\" 3\n{}" } ) {
        key_value_document_t *pDocument = ParseSettings( pSource );
        cypher_settings_t settings{};
        settings.nDisplayWidth = 777u;
        schema_diagnostic_t diagnostic{};
        const cypher_settings_decode_result_t result =
            CypherSettings_Decode( pDocument, {}, &diagnostic, 1u, &settings );
        CAPTURE( pSource );
        REQUIRE( result.status == cypher_settings_status_t::INVALID_DOCUMENT );
        REQUIRE( diagnostic.severity == schema_diagnostic_severity_t::ERROR );
        REQUIRE( settings.nDisplayWidth == 777u );
        KeyValue_DestroyDocument( pDocument );
    }
}

TEST_CASE( "Tier2 display descriptors match the compiled defaults",
           "[CypherCommon][Tier2][Settings]" )
{
    usize nDescriptors = 0u;
    const setting_descriptor_t *pDescriptors = CypherSettings_DisplayDescriptors( &nDescriptors );
    REQUIRE( nDescriptors == 4u );
    const cypher_settings_t defaults = CypherSettings_Defaults();
    REQUIRE( pDescriptors[0].nDefault == static_cast<i64>( defaults.nDisplayWidth ) );
    REQUIRE( pDescriptors[1].nDefault == static_cast<i64>( defaults.nDisplayHeight ) );
    REQUIRE( StringView_Equals( StringView_FromCString( pDescriptors[2].pDefaultText ),
                                StringView_FromCString( CypherSettings_DisplayModeName( defaults.displayMode ) ) ) );
    REQUIRE( pDescriptors[3].bDefault == defaults.bVSync );
    REQUIRE( CypherSettings_Identity().nCurrentVersion == CY_SETTINGS_SCHEMA_VERSION );
}

TEST_CASE( "Tier2 settings validate their descriptor and arguments",
           "[CypherCommon][Tier2][Settings]" )
{
    REQUIRE( Schema_CheckDescriptor( SettingsSchema_V1() ) ==
             schema_descriptor_status_t::OK );
    REQUIRE( Schema_CheckDescriptor( SettingsSchema_V2() ) ==
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
