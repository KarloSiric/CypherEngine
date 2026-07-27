//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Environment_Tests.cpp
//  Purpose: Tests Tier0 process environment access.
//  Details: These tests cover missing and empty values, size queries, bounded
//           output, UTF-8 values, explicit removal, and invalid variable names.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Environment.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>

using namespace cypher::common;

namespace
{

constexpr const char *TEST_NAME = "CYPHER_TIER0_ENVIRONMENT_TEST";

} // namespace

TEST_CASE( "Environment validates variable names and arguments", "[CypherCommon][Tier0][Environment]" )
{
    REQUIRE_FALSE( Cy_EnvironmentSet( nullptr, "value" ) );
    REQUIRE_FALSE( Cy_EnvironmentSet( "", "value" ) );
    REQUIRE_FALSE( Cy_EnvironmentSet( "BAD=NAME", "value" ) );
    REQUIRE_FALSE( Cy_EnvironmentSet( TEST_NAME, nullptr ) );
    REQUIRE_FALSE( Cy_EnvironmentUnset( nullptr ) );
    REQUIRE_FALSE( Cy_EnvironmentGet( "BAD=NAME", nullptr, 0u ).exists );
}

TEST_CASE( "Environment distinguishes missing empty and populated values", "[CypherCommon][Tier0][Environment]" )
{
    REQUIRE( Cy_EnvironmentUnset( TEST_NAME ) );
    REQUIRE_FALSE( Cy_EnvironmentHas( TEST_NAME ) );

    REQUIRE( Cy_EnvironmentSet( TEST_NAME, "" ) );
    const cy_environment_get_result_t empty =
        Cy_EnvironmentGet( TEST_NAME, nullptr, 0u );
    REQUIRE( empty.exists );
    REQUIRE( empty.cchRequired == 0u );
    REQUIRE( Cy_EnvironmentHas( TEST_NAME ) );

    constexpr const char *VALUE = "Cypher-\xC5\xBE";
    REQUIRE( Cy_EnvironmentSet( TEST_NAME, VALUE ) );

    const cy_environment_get_result_t probe =
        Cy_EnvironmentGet( TEST_NAME, nullptr, 0u );
    REQUIRE( probe.exists );
    REQUIRE( probe.cchRequired == std::strlen( VALUE ) );
    REQUIRE_FALSE( probe.isTruncated );

    char szValue[32] = {};
    const cy_environment_get_result_t read =
        Cy_EnvironmentGet( TEST_NAME, szValue, sizeof( szValue ) );
    REQUIRE( read.exists );
    REQUIRE_FALSE( read.isTruncated );
    REQUIRE( std::strcmp( szValue, VALUE ) == 0 );

    REQUIRE( Cy_EnvironmentUnset( TEST_NAME ) );
    REQUIRE_FALSE( Cy_EnvironmentHas( TEST_NAME ) );
}

TEST_CASE( "Environment reports bounded output truncation", "[CypherCommon][Tier0][Environment]" )
{
    REQUIRE( Cy_EnvironmentSet( TEST_NAME, "abcdefgh" ) );

    char szSmall[4] = { 'x', 'x', 'x', 'x' };
    const cy_environment_get_result_t result =
        Cy_EnvironmentGet( TEST_NAME, szSmall, sizeof( szSmall ) );
    REQUIRE( result.exists );
    REQUIRE( result.cchRequired == 8u );
    REQUIRE( result.isTruncated );
    REQUIRE( szSmall[sizeof( szSmall ) - 1u] == '\0' );

    REQUIRE( Cy_EnvironmentUnset( TEST_NAME ) );
}
