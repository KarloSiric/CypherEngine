//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Defines_Tests.cpp
//  Purpose: Tests Tier0 compiler-neutral macro contracts.
//  Details: These checks protect token helpers, native array counting, safe bit
//           construction, binary-size scaling, branch hints, and type controls.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Defines.h"

#include <type_traits>

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

#define CYPHER_TEST_STRING_TOKEN CypherToken

struct no_copy_move_t {
    no_copy_move_t() = default;
    CYPHER_NO_COPY_MOVE( no_copy_move_t );
};

struct offset_probe_t {
    u8 first;
    u32 second;
};

} // namespace

TEST_CASE( "Defines expands string and token helpers", "[CypherCommon][Tier0][Defines]" )
{
    STATIC_REQUIRE( sizeof( CYPHER_STRINGIFY( CYPHER_TEST_STRING_TOKEN ) ) == sizeof( "CypherToken" ) );

    constexpr i32 CYPHER_JOIN( joinedValue, 7 ) = 17;
    STATIC_REQUIRE( joinedValue7 == 17 );
}

TEST_CASE( "Defines counts only native array extents", "[CypherCommon][Tier0][Defines]" )
{
    constexpr u32 values[5] = {};
    constexpr u8 matrix[3][4] = {};

    STATIC_REQUIRE( CYPHER_ARRAY_COUNT( values ) == 5u );
    STATIC_REQUIRE( CYPHER_ARRAY_COUNT( matrix ) == 3u );
    STATIC_REQUIRE( CYPHER_ARRAY_COUNT( matrix[0] ) == 4u );
    STATIC_REQUIRE( defines_detail::ArrayCount( values ) == 5u );
}

TEST_CASE( "Defines builds bit masks without invalid shifts", "[CypherCommon][Tier0][Defines]" )
{
    STATIC_REQUIRE( CYPHER_BIT32( 0u ) == 1u );
    STATIC_REQUIRE( CYPHER_BIT32( 31u ) == 0x80000000u );
    STATIC_REQUIRE( CYPHER_BIT32( 32u ) == 0u );

    STATIC_REQUIRE( CYPHER_BIT64( 0u ) == 1ull );
    STATIC_REQUIRE( CYPHER_BIT64( 63u ) == 0x8000000000000000ull );
    STATIC_REQUIRE( CYPHER_BIT64( 64u ) == 0ull );
}

TEST_CASE( "Defines binary-size macros use IEC scaling", "[CypherCommon][Tier0][Defines]" )
{
    STATIC_REQUIRE( CYPHER_KIB( 2u ) == 2048ull );
    STATIC_REQUIRE( CYPHER_MIB( 2u ) == 2097152ull );
    STATIC_REQUIRE( CYPHER_GIB( 2u ) == 2147483648ull );
    STATIC_REQUIRE( CYPHER_TIB( 2u ) == 2199023255552ull );

    STATIC_REQUIRE( CYPHER_KB( 1u ) == CYPHER_KIB( 1u ) );
    STATIC_REQUIRE( CYPHER_MB( 1u ) == CYPHER_MIB( 1u ) );
    STATIC_REQUIRE( CYPHER_GB( 1u ) == CYPHER_GIB( 1u ) );
}

TEST_CASE( "Defines branch hints evaluate expressions once", "[CypherCommon][Tier0][Defines]" )
{
    i32 nEvaluationCount = 0;

    REQUIRE( CYPHER_LIKELY( ++nEvaluationCount == 1 ) );
    REQUIRE( nEvaluationCount == 1 );
    REQUIRE_FALSE( CYPHER_UNLIKELY( ++nEvaluationCount == 1 ) );
    REQUIRE( nEvaluationCount == 2 );
}

TEST_CASE( "Defines type and layout controls preserve contracts", "[CypherCommon][Tier0][Defines]" )
{
    STATIC_REQUIRE_FALSE( std::is_copy_constructible_v<no_copy_move_t> );
    STATIC_REQUIRE_FALSE( std::is_copy_assignable_v<no_copy_move_t> );
    STATIC_REQUIRE_FALSE( std::is_move_constructible_v<no_copy_move_t> );
    STATIC_REQUIRE_FALSE( std::is_move_assignable_v<no_copy_move_t> );
    STATIC_REQUIRE( CYPHER_ALIGNOF( u64 ) == alignof( u64 ) );
    STATIC_REQUIRE( CYPHER_OFFSETOF( offset_probe_t, second ) == offsetof( offset_probe_t, second ) );
}
