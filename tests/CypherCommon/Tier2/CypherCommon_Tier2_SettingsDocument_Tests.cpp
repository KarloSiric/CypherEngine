//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier2/CypherCommon_Tier2_SettingsDocument_Tests.cpp
//  Purpose: Contract tests for the settings-family document store.
//  Details: Covers the ADR 0009 guarantees: failed loads keep the previous
//           settings, unknown members survive a save, invalid values fall
//           back alone, and override writes keep scopes sparse.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_SettingsDocument.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace cypher::common;

namespace
{

constexpr settings_document_identity_t kIdentity{ { "cypher.test_settings", 20u }, 1u, 2u };

std::string Text( const text_buffer_t &buffer )
{
    return std::string( TextBuffer_Data( &buffer ), TextBuffer_Length( &buffer ) );
}

string_view_t View( const char *pText )
{
    return StringView_FromCString( pText );
}

setting_descriptor_t IntegerSetting( const char *pPath, i64 nDefault, i64 nMin, i64 nMax )
{
    setting_descriptor_t descriptor{};
    descriptor.pPath = pPath;
    descriptor.type = setting_type_t::INTEGER;
    descriptor.nDefault = nDefault;
    descriptor.nMin = nMin;
    descriptor.nMax = nMax;
    return descriptor;
}

setting_value_t IntegerValue( i64 nValue )
{
    setting_value_t value{};
    value.type = setting_type_t::INTEGER;
    value.nValue = nValue;
    return value;
}

} // namespace

TEST_CASE( "Settings paths split dotted text and accept verbatim segments",
           "[CypherCommon][Tier2][SettingsDocument]" )
{
    settings_path_t path{};
    REQUIRE( SettingsPath_Parse( View( "editor.viewport.grid_size" ), &path ) );
    REQUIRE( path.nSegments == 3u );
    REQUIRE( StringView_Equals( path.segments[1], View( "viewport" ) ) );

    for ( const char *pBad : { "", ".a", "a.", "a..b", "a.b.c.d.e.f.g.h.i" } ) {
        CAPTURE( pBad );
        REQUIRE_FALSE( SettingsPath_Parse( View( pBad ), &path ) );
    }

    REQUIRE( SettingsPath_Parse( View( "colors" ), &path ) );
    REQUIRE( SettingsPath_Append( &path, View( "ui.background" ) ) );
    REQUIRE( path.nSegments == 2u );
    REQUIRE( StringView_Equals( path.segments[1], View( "ui.background" ) ) );
}

TEST_CASE( "Settings documents write the current version and keep unknown members",
           "[CypherCommon][Tier2][SettingsDocument]" )
{
    settings_document_t store{};
    REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), kIdentity ) ==
             settings_document_status_t::OK );

    // A version-1 file from an older build, holding a section this build does
    // not know about.
    const char *pSource =
        "@cykv 1\n@schema \"cypher.test_settings\" 1\n"
        "{\n"
        "    editor = { grid = 8 future_flag = true }\n"
        "    other_tool = { recent = [ \"a\", \"b\" ] }\n"
        "}\n";
    const settings_document_load_result_t loaded = SettingsDocument_Load( &store, View( pSource ) );
    REQUIRE( loaded.status == settings_document_status_t::OK );
    REQUIRE( loaded.nDeclaredVersion == 1u );
    REQUIRE( store.nLoadedVersion == 1u );

    const setting_descriptor_t grid = IntegerSetting( "editor.grid", 16, 1, 1024 );
    REQUIRE( Setting_Write( &store, grid, IntegerValue( 32 ) ) == settings_document_status_t::OK );

    text_buffer_t text{};
    REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) );
    REQUIRE( SettingsDocument_Write( &store, &text ) == settings_document_status_t::OK );
    const std::string written = Text( text );
    REQUIRE( written.starts_with( "@cykv 1\n@schema \"cypher.test_settings\" 2\n" ) );
    REQUIRE( written.find( "future_flag" ) != std::string::npos );
    REQUIRE( written.find( "other_tool" ) != std::string::npos );
    REQUIRE( written.find( "\"b\"" ) != std::string::npos );

    // The written file loads back to the same values.
    settings_document_t again{};
    REQUIRE( SettingsDocument_Init( &again, Allocator_GetSystem(), kIdentity ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &again, TextBuffer_View( &text ) ).status == settings_document_status_t::OK );
    setting_value_t value{};
    REQUIRE( Setting_Read( SettingsDocument_Root( &again ), grid, &value, nullptr ) == setting_read_status_t::VALUE );
    REQUIRE( value.nValue == 32 );
}

TEST_CASE( "A failed settings load never replaces the settings in use",
           "[CypherCommon][Tier2][SettingsDocument]" )
{
    settings_document_t store{};
    REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), kIdentity ) == settings_document_status_t::OK );
    const setting_descriptor_t grid = IntegerSetting( "editor.grid", 16, 1, 1024 );
    REQUIRE( Setting_Write( &store, grid, IntegerValue( 64 ) ) == settings_document_status_t::OK );

    struct case_t {
        const char *pText;
        settings_document_status_t expected;
    };
    const case_t cases[]{
        { "@cykv 1\n@schema \"cypher.test_settings\" 1\n{ editor = { grid = ", settings_document_status_t::PARSE_FAILED },
        { "@cykv 1\n@schema \"cypher.project\" 1\n{}", settings_document_status_t::SCHEMA_MISMATCH },
        { "@cykv 1\n@schema \"cypher.test_settings\" 3\n{}", settings_document_status_t::UNSUPPORTED_VERSION },
        // CYKV documents have object roots, so an array root is a parse error.
        { "@cykv 1\n@schema \"cypher.test_settings\" 2\n[ 1, 2 ]", settings_document_status_t::PARSE_FAILED },
    };
    for ( const case_t &c : cases ) {
        CAPTURE( c.pText );
        REQUIRE( SettingsDocument_Load( &store, View( c.pText ) ).status == c.expected );
        setting_value_t value{};
        REQUIRE( Setting_Read( SettingsDocument_Root( &store ), grid, &value, nullptr ) == setting_read_status_t::VALUE );
        REQUIRE( value.nValue == 64 );
    }
}

TEST_CASE( "Typed reads report absent and invalid values without failing",
           "[CypherCommon][Tier2][SettingsDocument]" )
{
    settings_document_t store{};
    REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), kIdentity ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &store, View(
        "@cykv 1\n@schema \"cypher.test_settings\" 2\n"
        "{ view = { fov = 70 speed = \"fast\" mode = \"ortho\" tint = \"#FF8000\" bad_tint = \"orange\" wide = 5000 } }" ) ).status ==
             settings_document_status_t::OK );
    const key_value_t *pRoot = SettingsDocument_Root( &store );

    setting_descriptor_t fov{};
    fov.pPath = "view.fov";
    fov.type = setting_type_t::REAL;
    fov.flMin = 30.0;
    fov.flMax = 120.0;
    setting_value_t value{};
    REQUIRE( Setting_Read( pRoot, fov, &value, nullptr ) == setting_read_status_t::VALUE );
    REQUIRE( value.flValue == 70.0 ); // Integers are accepted for reals.

    setting_descriptor_t speed = fov;
    speed.pPath = "view.speed";
    setting_problem_code_t problem = setting_problem_code_t::NONE;
    REQUIRE( Setting_Read( pRoot, speed, &value, &problem ) == setting_read_status_t::INVALID );
    REQUIRE( problem == setting_problem_code_t::WRONG_TYPE );

    static const char *const kModes[]{ "perspective", "ortho" };
    setting_descriptor_t mode{};
    mode.pPath = "view.mode";
    mode.type = setting_type_t::ENUM;
    mode.ppEnumValues = kModes;
    mode.nEnumValues = 2u;
    REQUIRE( Setting_Read( pRoot, mode, &value, nullptr ) == setting_read_status_t::VALUE );
    REQUIRE( value.text.pData == kModes[1] ); // Borrows the descriptor, not the document.

    setting_descriptor_t tint{};
    tint.pPath = "view.tint";
    tint.type = setting_type_t::COLOR;
    REQUIRE( Setting_Read( pRoot, tint, &value, nullptr ) == setting_read_status_t::VALUE );
    REQUIRE( value.rgba == 0xFF8000FFu );
    tint.pPath = "view.bad_tint";
    REQUIRE( Setting_Read( pRoot, tint, &value, &problem ) == setting_read_status_t::INVALID );
    REQUIRE( problem == setting_problem_code_t::BAD_COLOR );

    const setting_descriptor_t wide = IntegerSetting( "view.wide", 10, 1, 100 );
    REQUIRE( Setting_Read( pRoot, wide, &value, &problem ) == setting_read_status_t::INVALID );
    REQUIRE( problem == setting_problem_code_t::OUT_OF_RANGE );

    const setting_descriptor_t missing = IntegerSetting( "view.nothing.here", 10, 1, 100 );
    REQUIRE( Setting_Read( pRoot, missing, &value, nullptr ) == setting_read_status_t::ABSENT );

    // A scalar in the middle of a path is a bad path for that value only.
    const setting_descriptor_t through = IntegerSetting( "view.fov.inner", 10, 1, 100 );
    REQUIRE( Setting_Read( pRoot, through, &value, &problem ) == setting_read_status_t::INVALID );
    REQUIRE( problem == setting_problem_code_t::BAD_PATH );
}

TEST_CASE( "Resolution walks scopes, skipping invalid values, then defaults",
           "[CypherCommon][Tier2][SettingsDocument]" )
{
    settings_document_t workspace{};
    settings_document_t project{};
    settings_document_t user{};
    for ( settings_document_t *pStore : { &workspace, &project, &user } ) {
        REQUIRE( SettingsDocument_Init( pStore, Allocator_GetSystem(), kIdentity ) == settings_document_status_t::OK );
    }
    REQUIRE( SettingsDocument_Load( &workspace, View( "@cykv 1\n@schema \"cypher.test_settings\" 2\n{ editor = { grid = 0 } }" ) ).status == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &user, View( "@cykv 1\n@schema \"cypher.test_settings\" 2\n{ editor = { grid = 4 } }" ) ).status == settings_document_status_t::OK );

    const setting_descriptor_t grid = IntegerSetting( "editor.grid", 16, 1, 1024 );
    const key_value_t *scopes[]{
        SettingsDocument_Root( &workspace ), SettingsDocument_Root( &project ), nullptr, SettingsDocument_Root( &user )
    };
    setting_problem_t problems[2]{};
    const setting_resolution_t resolved = Setting_Resolve( scopes, 4u, grid, problems, 2u );
    REQUIRE( resolved.value.nValue == 4 );
    REQUIRE( resolved.iScope == 3u );
    REQUIRE( resolved.nProblemsRequired == 1u );
    REQUIRE( problems[0].iScope == 0u );
    REQUIRE( problems[0].code == setting_problem_code_t::OUT_OF_RANGE );
    REQUIRE( problems[0].pDescriptor == &grid );

    const key_value_t *none[]{ SettingsDocument_Root( &project ) };
    const setting_resolution_t fallback = Setting_Resolve( none, 1u, grid, nullptr, 0u );
    REQUIRE( fallback.value.nValue == 16 );
    REQUIRE( fallback.iScope == CY_INVALID_SIZE );
}

TEST_CASE( "Override writes keep a scope sparse and prune emptied objects",
           "[CypherCommon][Tier2][SettingsDocument]" )
{
    settings_document_t store{};
    REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), kIdentity ) == settings_document_status_t::OK );
    const setting_descriptor_t grid = IntegerSetting( "editor.viewport.grid", 16, 1, 1024 );

    REQUIRE( Setting_WriteOverride( &store, grid, IntegerValue( 8 ), IntegerValue( 16 ) ) == settings_document_status_t::OK );
    REQUIRE( KeyValue_Find( SettingsDocument_Root( &store ), View( "editor" ) ) != nullptr );

    // Setting it back to the inherited value removes the entry and the empty
    // "viewport" and "editor" objects it lived in.
    REQUIRE( Setting_WriteOverride( &store, grid, IntegerValue( 16 ), IntegerValue( 16 ) ) == settings_document_status_t::OK );
    REQUIRE( KeyValue_ChildCount( SettingsDocument_Root( &store ) ) == 0u );

    // Invalid values are refused without touching the document.
    REQUIRE( Setting_Write( &store, grid, IntegerValue( 0 ) ) == settings_document_status_t::INVALID_ARGUMENT );
    REQUIRE( KeyValue_ChildCount( SettingsDocument_Root( &store ) ) == 0u );
}

TEST_CASE( "Writes never destroy a user's value that blocks a path",
           "[CypherCommon][Tier2][SettingsDocument]" )
{
    settings_document_t store{};
    REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), kIdentity ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &store, View( "@cykv 1\n@schema \"cypher.test_settings\" 2\n{ editor = 5 }" ) ).status == settings_document_status_t::OK );
    const setting_descriptor_t grid = IntegerSetting( "editor.grid", 16, 1, 1024 );
    REQUIRE( Setting_Write( &store, grid, IntegerValue( 8 ) ) == settings_document_status_t::PATH_BLOCKED );
    i64 nValue = 0;
    REQUIRE( KeyValue_GetI64( KeyValue_Find( SettingsDocument_Root( &store ), View( "editor" ) ), &nValue ) );
    REQUIRE( nValue == 5 );
}

TEST_CASE( "Setting colours parse both forms and format canonically",
           "[CypherCommon][Tier2][SettingsDocument]" )
{
    u32 rgba = 0u;
    REQUIRE( SettingColor_Parse( View( "#1a2B3c" ), &rgba ) );
    REQUIRE( rgba == 0x1A2B3CFFu );
    REQUIRE( SettingColor_Parse( View( "#1a2b3c80" ), &rgba ) );
    REQUIRE( rgba == 0x1A2B3C80u );
    for ( const char *pBad : { "", "1a2b3c", "#1a2b3", "#1a2b3cz0", "#12345678901" } ) {
        CAPTURE( pBad );
        REQUIRE_FALSE( SettingColor_Parse( View( pBad ), &rgba ) );
    }
    char text[10]{};
    REQUIRE( SettingColor_Format( 0x1A2B3CFFu, text ) == 7u );
    REQUIRE( std::string( text ) == "#1a2b3c" );
    REQUIRE( SettingColor_Format( 0x1A2B3C80u, text ) == 9u );
    REQUIRE( std::string( text ) == "#1a2b3c80" );
}
