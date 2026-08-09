//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_CommandLine_Tests.cpp
//  Purpose: Tests allocation-free process command-line queries.
//  Details: Covers switch forms, separate and inline values, empty values, exact
//           matching, terminators, borrowed arguments, and invalid input handling.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_CommandLine.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_commandLineAssertCount = 0u;

assert_action_t CaptureCommandLineAssert( const assert_info_t & ) noexcept
{
    ++g_commandLineAssertCount;
    return assert_action_t::Continue;
}

void RequireViewEquals( string_view_t value, const char *pExpected )
{
    REQUIRE( StringView_Equals(
        value,
        StringView_FromCString( pExpected ) ) );
}

} // namespace

TEST_CASE( "CommandLine exposes borrowed argv without allocation",
           "[CypherCommon][Tier1][CommandLine]" )
{
    const char *arguments[]{
        "CypherEngine",
        "--renderer=opengl",
        "asset.cymap"
    };
    command_line_t commandLine{};
    REQUIRE( CommandLine_Init( &commandLine, 3, arguments ) );
    REQUIRE( CommandLine_IsValid( &commandLine ) );
    REQUIRE( CommandLine_Count( &commandLine ) == 3 );
    RequireViewEquals( CommandLine_Program( &commandLine ), "CypherEngine" );
    RequireViewEquals(
        CommandLine_Argument( &commandLine, 2 ),
        "asset.cymap" );
    REQUIRE_FALSE( CommandLine_IsSwitchArgument( &commandLine, 0 ) );
    REQUIRE( CommandLine_IsSwitchArgument( &commandLine, 1 ) );
    REQUIRE_FALSE( CommandLine_IsSwitchArgument( &commandLine, 2 ) );

    command_line_t empty{};
    REQUIRE( CommandLine_Init( &empty, 0, nullptr ) );
    REQUIRE( StringView_IsEmpty( CommandLine_Program( &empty ) ) );
}

TEST_CASE( "CommandLine parses deterministic switch and value forms",
           "[CypherCommon][Tier1][CommandLine]" )
{
    const char *arguments[]{
        "CypherEngine",
        "--renderer=opengl",
        "-map",
        "facility",
        "--empty=",
        "-flag",
        "--threshold:-1",
        "--",
        "--ignored=value"
    };
    command_line_t commandLine{};
    REQUIRE( CommandLine_Init( &commandLine, 9, arguments ) );

    command_line_switch_t found{};
    REQUIRE( CommandLine_FindSwitchInfo(
        &commandLine,
        StringView_FromCString( "renderer" ),
        &found ) );
    REQUIRE( found.iArgument == 1 );
    RequireViewEquals( found.name, "renderer" );
    RequireViewEquals( found.value, "opengl" );
    REQUIRE( found.bHasValue );

    REQUIRE( CommandLine_FindSwitchInfo(
        &commandLine,
        StringView_FromCString( "--map" ),
        &found ) );
    REQUIRE( found.iArgument == 2 );
    RequireViewEquals( found.value, "facility" );
    REQUIRE( found.bHasValue );

    REQUIRE( CommandLine_FindSwitchInfo(
        &commandLine,
        StringView_FromCString( "empty" ),
        &found ) );
    REQUIRE( found.bHasValue );
    REQUIRE( StringView_IsEmpty( found.value ) );

    REQUIRE( CommandLine_FindSwitchInfo(
        &commandLine,
        StringView_FromCString( "flag" ),
        &found ) );
    REQUIRE_FALSE( found.bHasValue );
    REQUIRE( StringView_IsEmpty( found.value ) );

    REQUIRE( CommandLine_FindSwitchInfo(
        &commandLine,
        StringView_FromCString( "threshold" ),
        &found ) );
    RequireViewEquals( found.value, "-1" );
    REQUIRE_FALSE( CommandLine_HasSwitch(
        &commandLine,
        StringView_FromCString( "ignored" ) ) );
}

TEST_CASE( "CommandLine lookup is exact case-selectable and first-match wins",
           "[CypherCommon][Tier1][CommandLine]" )
{
    const char *arguments[]{
        "tool",
        "--Renderer=first",
        "--renderer-debug",
        "--renderer=second",
        "/legacy"
    };
    command_line_t commandLine{};
    REQUIRE( CommandLine_Init( &commandLine, 5, arguments ) );

    REQUIRE( CommandLine_FindSwitch(
        &commandLine,
        StringView_FromCString( "renderer" ) ) == 1 );
    RequireViewEquals(
        CommandLine_SwitchValue(
            &commandLine,
            StringView_FromCString( "renderer" ) ),
        "first" );
    REQUIRE( CommandLine_FindSwitch(
        &commandLine,
        StringView_FromCString( "renderer" ),
        CY_FALSE ) == 3 );
    REQUIRE_FALSE( CommandLine_HasSwitch(
        &commandLine,
        StringView_FromCString( "render" ) ) );
    REQUIRE_FALSE( CommandLine_HasSwitch(
        &commandLine,
        StringView_FromCString( "legacy" ) ) );
}

TEST_CASE( "CommandLine TrySwitchValue distinguishes absence and empty values",
           "[CypherCommon][Tier1][CommandLine]" )
{
    const char *arguments[]{ "tool", "--empty=", "--flag" };
    command_line_t commandLine{};
    REQUIRE( CommandLine_Init( &commandLine, 3, arguments ) );

    string_view_t value{};
    bool_t bHasValue = CY_FALSE;
    REQUIRE( CommandLine_TrySwitchValue(
        &commandLine,
        StringView_FromCString( "empty" ),
        &value,
        &bHasValue ) );
    REQUIRE( bHasValue );
    REQUIRE( StringView_IsEmpty( value ) );

    REQUIRE( CommandLine_TrySwitchValue(
        &commandLine,
        StringView_FromCString( "flag" ),
        &value,
        &bHasValue ) );
    REQUIRE_FALSE( bHasValue );
    REQUIRE_FALSE( CommandLine_TrySwitchValue(
        &commandLine,
        StringView_FromCString( "missing" ),
        &value,
        &bHasValue ) );
}

TEST_CASE( "CommandLine invalid calls assert and fail without partial state",
           "[CypherCommon][Tier1][CommandLine]" )
{
    g_commandLineAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureCommandLineAssert );

    REQUIRE_FALSE( CommandLine_Init( nullptr, 0, nullptr ) );
    command_line_t commandLine{};
    REQUIRE_FALSE( CommandLine_Init( &commandLine, -1, nullptr ) );
    REQUIRE_FALSE( CommandLine_Init( &commandLine, 1, nullptr ) );
    const char *invalidArguments[]{ "tool", nullptr };
    REQUIRE_FALSE( CommandLine_Init( &commandLine, 2, invalidArguments ) );
    REQUIRE( CommandLine_Count( &commandLine ) == 0 );

    const char *validArguments[]{ "tool", "--flag" };
    REQUIRE( CommandLine_Init( &commandLine, 2, validArguments ) );
    REQUIRE( StringView_IsEmpty(
        CommandLine_Argument( &commandLine, 2 ) ) );
    command_line_switch_t found{};
    REQUIRE_FALSE( CommandLine_FindSwitchInfo(
        &commandLine,
        {},
        &found ) );
    string_view_t value{};
    REQUIRE_FALSE( CommandLine_TrySwitchValue(
        &commandLine,
        StringView_FromCString( "flag" ),
        &value,
        nullptr ) );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_commandLineAssertCount ==
        7u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
