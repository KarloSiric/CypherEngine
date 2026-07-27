//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_SourceLocation_Tests.cpp
//  Purpose: Tests Tier0 source-location capture and formatting.
//  Details: These checks protect caller capture, record validation, size queries,
//           bounded formatting, truncation behavior, and null-field handling.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_SourceLocation.h"

#include <cstring>

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "SourceLocation captures the calling source line", "[CypherCommon][Tier0][SourceLocation]" )
{
    const u32 nExpectedLine = static_cast<u32>( __LINE__ + 1 );
    const source_location_t location = CY_SOURCE_LOCATION;

    REQUIRE( Cy_SourceLocation_IsValid( location ) );
    REQUIRE( location.line == nExpectedLine );
    REQUIRE( location.pFile != nullptr );
    REQUIRE( location.pFile[0] != '\0' );
    REQUIRE( location.pFunction != nullptr );
    REQUIRE( location.pFunction[0] != '\0' );
}

TEST_CASE( "SourceLocation formats complete records", "[CypherCommon][Tier0][SourceLocation]" )
{
    const source_location_t location{ "file.cpp", "TestFunction", 42u, 7u };
    char szLocation[128] = {};

    const usize cchRequired = Cy_SourceLocation_Format(
        location,
        szLocation,
        CYPHER_ARRAY_COUNT( szLocation ) );

    REQUIRE( std::strcmp( szLocation, "file.cpp:42:7:TestFunction" ) == 0 );
    REQUIRE( cchRequired == std::strlen( szLocation ) );
}

TEST_CASE( "SourceLocation supports size queries and bounded truncation", "[CypherCommon][Tier0][SourceLocation]" )
{
    const source_location_t location{ "long_file_name.cpp", "LongFunctionName", 123u, 0u };
    const usize cchRequired = Cy_SourceLocation_Format( location, nullptr, 0u );
    REQUIRE( Cy_SourceLocation_Format( location, nullptr, 32u ) == cchRequired );

    char szSmall[8] = {};
    const usize cchTruncatedRequired = Cy_SourceLocation_Format(
        location,
        szSmall,
        CYPHER_ARRAY_COUNT( szSmall ) );

    REQUIRE( cchRequired == cchTruncatedRequired );
    REQUIRE( cchRequired >= CYPHER_ARRAY_COUNT( szSmall ) );
    REQUIRE( szSmall[CYPHER_ARRAY_COUNT( szSmall ) - 1u] == '\0' );
}

TEST_CASE( "SourceLocation handles empty records deterministically", "[CypherCommon][Tier0][SourceLocation]" )
{
    const source_location_t location{};
    char szLocation[64] = {};

    REQUIRE_FALSE( Cy_SourceLocation_IsValid( location ) );
    REQUIRE( Cy_SourceLocation_Format(
                 location,
                 szLocation,
                 CYPHER_ARRAY_COUNT( szLocation ) ) == std::strlen( szLocation ) );
    REQUIRE( std::strcmp( szLocation, "<unknown>:0:<unknown>" ) == 0 );
}
