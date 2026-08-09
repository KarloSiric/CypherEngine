//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Range_Tests.cpp
//  Purpose: Tests overflow-aware index, byte, and value ranges.
//  Details: Protects half-open containment, empty boundaries, intersection,
//           endpoint overflow rejection, and inclusive value clamping.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_Range.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace cypher::common;

namespace
{

u32 g_rangeAssertCount = 0u;

assert_action_t CaptureRangeAssert( const assert_info_t & ) noexcept
{
    ++g_rangeAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "IndexRange uses valid half-open offset and count semantics",
           "[CypherCommon][Tier1][Range]" )
{
    const index_range_t range{ 10u, 5u };
    REQUIRE( IndexRange_IsValid( range ) );
    REQUIRE( IndexRange_End( range ) == 15u );
    REQUIRE_FALSE( IndexRange_Contains( range, 9u ) );
    REQUIRE( IndexRange_Contains( range, 10u ) );
    REQUIRE( IndexRange_Contains( range, 14u ) );
    REQUIRE_FALSE( IndexRange_Contains( range, 15u ) );

    const index_range_t maximumEnd{ CY_USIZE_MAX - 4u, 4u };
    REQUIRE( IndexRange_IsValid( maximumEnd ) );
    REQUIRE( IndexRange_End( maximumEnd ) == CY_USIZE_MAX );
}

TEST_CASE( "IndexRange containment includes valid empty boundaries",
           "[CypherCommon][Tier1][Range]" )
{
    const index_range_t outer{ 10u, 5u };
    REQUIRE( IndexRange_ContainsRange( outer, { 10u, 5u } ) );
    REQUIRE( IndexRange_ContainsRange( outer, { 12u, 2u } ) );
    REQUIRE( IndexRange_ContainsRange( outer, { 10u, 0u } ) );
    REQUIRE( IndexRange_ContainsRange( outer, { 15u, 0u } ) );
    REQUIRE_FALSE( IndexRange_ContainsRange( outer, { 9u, 0u } ) );
    REQUIRE_FALSE( IndexRange_ContainsRange( outer, { 14u, 2u } ) );

    const index_range_t empty{ 15u, 0u };
    REQUIRE( IndexRange_ContainsRange( empty, empty ) );
    REQUIRE_FALSE( IndexRange_Contains( empty, 15u ) );
}

TEST_CASE( "IndexRange intersection handles overlap, touching, and disjoint input",
           "[CypherCommon][Tier1][Range]" )
{
    const index_range_t overlap =
        IndexRange_Intersection( { 4u, 8u }, { 9u, 8u } );
    REQUIRE( overlap.iFirst == 9u );
    REQUIRE( overlap.nCount == 3u );

    const index_range_t touching =
        IndexRange_Intersection( { 4u, 5u }, { 9u, 3u } );
    REQUIRE( touching.iFirst == 9u );
    REQUIRE( touching.nCount == 0u );

    const index_range_t disjoint =
        IndexRange_Intersection( { 1u, 2u }, { 8u, 2u } );
    REQUIRE( disjoint.iFirst == 8u );
    REQUIRE( disjoint.nCount == 0u );
}

TEST_CASE( "IndexRange rejects overflowing endpoint arithmetic",
           "[CypherCommon][Tier1][Range]" )
{
    const index_range_t invalid{ CY_USIZE_MAX - 2u, 4u };
    const index_range_t valid{ 0u, 4u };
    REQUIRE_FALSE( IndexRange_IsValid( invalid ) );

    g_rangeAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureRangeAssert );

    REQUIRE( IndexRange_End( invalid ) == CY_INVALID_SIZE );
    REQUIRE_FALSE( IndexRange_Contains( invalid, CY_USIZE_MAX ) );
    REQUIRE_FALSE( IndexRange_ContainsRange( invalid, valid ) );
    const index_range_t intersection = IndexRange_Intersection( valid, invalid );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE( intersection.iFirst == 0u );
    REQUIRE( intersection.nCount == 0u );
    REQUIRE(
        g_rangeAssertCount ==
        4u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}

TEST_CASE( "ByteRange mirrors IndexRange semantics for serialized storage",
           "[CypherCommon][Tier1][Range]" )
{
    const byte_range_t outer{ 64u, 128u };
    REQUIRE( ByteRange_IsValid( outer ) );
    REQUIRE( ByteRange_End( outer ) == 192u );
    REQUIRE( ByteRange_ContainsOffset( outer, 64u ) );
    REQUIRE( ByteRange_ContainsOffset( outer, 191u ) );
    REQUIRE_FALSE( ByteRange_ContainsOffset( outer, 192u ) );
    REQUIRE( ByteRange_ContainsRange( outer, { 96u, 32u } ) );
    REQUIRE( ByteRange_ContainsRange( outer, { 192u, 0u } ) );
    REQUIRE_FALSE( ByteRange_ContainsRange( outer, { 160u, 64u } ) );

    const byte_range_t overlap =
        ByteRange_Intersection( outer, { 128u, 128u } );
    REQUIRE( overlap.iOffset == 128u );
    REQUIRE( overlap.cbSize == 64u );
}

TEST_CASE( "ByteRange rejects overflowing endpoint arithmetic",
           "[CypherCommon][Tier1][Range]" )
{
    const byte_range_t invalid{ CY_USIZE_MAX - 1u, 8u };
    const byte_range_t valid{ 0u, 8u };
    REQUIRE_FALSE( ByteRange_IsValid( invalid ) );

    g_rangeAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureRangeAssert );

    REQUIRE( ByteRange_End( invalid ) == CY_INVALID_SIZE );
    REQUIRE_FALSE( ByteRange_ContainsOffset( invalid, 0u ) );
    REQUIRE_FALSE( ByteRange_ContainsRange( valid, invalid ) );
    const byte_range_t intersection = ByteRange_Intersection( invalid, valid );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE( intersection.iOffset == 0u );
    REQUIRE( intersection.cbSize == 0u );
    REQUIRE(
        g_rangeAssertCount ==
        4u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}

TEST_CASE( "ValueRange provides constexpr ordered containment and clamping",
           "[CypherCommon][Tier1][Range]" )
{
    constexpr value_range_t<i32> range{ -10, 20 };
    STATIC_REQUIRE( ValueRange_IsOrdered( range ) );
    STATIC_REQUIRE( ValueRange_Contains( range, -10 ) );
    STATIC_REQUIRE( ValueRange_Contains( range, 20 ) );
    STATIC_REQUIRE_FALSE( ValueRange_Contains( range, 21 ) );
    STATIC_REQUIRE( ValueRange_Clamp( range, -50 ) == -10 );
    STATIC_REQUIRE( ValueRange_Clamp( range, 5 ) == 5 );
    STATIC_REQUIRE( ValueRange_Clamp( range, 50 ) == 20 );
}

TEST_CASE( "ValueRange rejects reversed and unordered floating endpoints",
           "[CypherCommon][Tier1][Range]" )
{
    const value_range_t<i32> reversed{ 10, -10 };
    const f64 nan = std::numeric_limits<f64>::quiet_NaN();
    const value_range_t<f64> unordered{ nan, 1.0 };
    REQUIRE_FALSE( ValueRange_IsOrdered( reversed ) );
    REQUIRE_FALSE( ValueRange_IsOrdered( unordered ) );

    g_rangeAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureRangeAssert );

    REQUIRE_FALSE( ValueRange_Contains( reversed, 0 ) );
    REQUIRE( ValueRange_Clamp( reversed, 5 ) == 5 );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_rangeAssertCount ==
        2u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
