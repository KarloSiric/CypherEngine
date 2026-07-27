//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_CommandLineBase_Tests.cpp
//  Purpose: Tests the allocation-free Tier0 command-line view.
//  Details: These tests validate option grammar, inline and separate values,
//           absolute paths, terminators, exact matching, bounds, and truncation.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CommandLineBase.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>

using namespace cypher::common;

TEST_CASE( "CommandLineBase parses deterministic switch forms", "[CypherCommon][Tier0][CommandLineBase]" )
{
    const char *args[] = {
        "-program-name-is-not-a-switch",
        "-root",
        "/tmp/cypher",
        "--map=arena",
        "-empty=",
        "-flag",
        "--",
        "-ignored"
    };

    command_line_base_t commandLine{};
    REQUIRE( Cy_CommandLineBaseSet( &commandLine, 8, args ) );
    REQUIRE( Cy_CommandLineBaseGetCount( &commandLine ) == 8u );
    REQUIRE( Cy_CommandLineBaseGetArg( &commandLine, 0u ) == args[0] );
    REQUIRE( Cy_CommandLineBaseGetArg( &commandLine, 8u ) == nullptr );

    REQUIRE( std::strcmp(
        Cy_CommandLineBaseFindValue( &commandLine, "root" ),
        "/tmp/cypher" ) == 0 );
    REQUIRE( std::strcmp(
        Cy_CommandLineBaseFindValue( &commandLine, "--map" ),
        "arena" ) == 0 );
    REQUIRE( std::strcmp(
        Cy_CommandLineBaseFindValue( &commandLine, "empty" ),
        "" ) == 0 );
    REQUIRE( std::strcmp(
        Cy_CommandLineBaseFindValue( &commandLine, "flag" ),
        "" ) == 0 );
    REQUIRE_FALSE( Cy_CommandLineBaseHasSwitch(
        &commandLine,
        "program-name-is-not-a-switch" ) );
    REQUIRE_FALSE( Cy_CommandLineBaseHasSwitch( &commandLine, "ignored" ) );
}

TEST_CASE( "CommandLineBase exact matching rejects aliases and slash switches", "[CypherCommon][Tier0][CommandLineBase]" )
{
    const char *args[] = {
        "tool",
        "--renderer=opengl",
        "--renderer-debug",
        "/legacy"
    };

    command_line_base_t commandLine{};
    REQUIRE( Cy_CommandLineBaseSet( &commandLine, 4, args ) );
    REQUIRE( std::strcmp(
        Cy_CommandLineBaseFindValue( &commandLine, "renderer" ),
        "opengl" ) == 0 );
    REQUIRE_FALSE( Cy_CommandLineBaseHasSwitch( &commandLine, "render" ) );
    REQUIRE_FALSE( Cy_CommandLineBaseHasSwitch( &commandLine, "legacy" ) );
}

TEST_CASE( "CommandLineBase reports invalid input and fixed-capacity truncation", "[CypherCommon][Tier0][CommandLineBase]" )
{
    command_line_base_t commandLine{};
    REQUIRE_FALSE( Cy_CommandLineBaseSet( nullptr, 0, nullptr ) );
    REQUIRE_FALSE( Cy_CommandLineBaseSet( &commandLine, -1, nullptr ) );
    REQUIRE( Cy_CommandLineBaseGetCount( &commandLine ) == 0u );
    REQUIRE_FALSE( Cy_CommandLineBaseSet( &commandLine, 1, nullptr ) );

    std::array<const char *, CY_COMMANDLINEBASE_MAX_ARGS + 1u> args = {};
    args.fill( "argument" );
    REQUIRE( Cy_CommandLineBaseSet(
        &commandLine,
        static_cast<i32>( args.size() ),
        args.data() ) );
    REQUIRE( commandLine.isTruncated );
    REQUIRE(
        commandLine.nArgCount == CY_COMMANDLINEBASE_MAX_ARGS );
}
