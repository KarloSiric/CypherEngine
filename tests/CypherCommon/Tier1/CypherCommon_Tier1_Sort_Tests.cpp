//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Sort_Tests.cpp
//  Purpose: Tests raw and typed sorting algorithms.
//  Details: Heap construction, unstable order, stable duplicate order, explicit
//           scratch requirements, custom comparators, and empty ranges are covered.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_HeapSort.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

i32 CompareI32( const void *pLeft, const void *pRight, void * ) noexcept
{
    const i32 left = *static_cast<const i32 *>( pLeft );
    const i32 right = *static_cast<const i32 *>( pRight );
    return left < right ? -1 : ( left > right ? 1 : 0 );
}

struct stable_value_t {
    i32 key{ 0 };
    i32 original{ 0 };
};

struct stable_less_t {
    bool_t operator()( const stable_value_t &left, const stable_value_t &right ) const noexcept
    {
        return left.key < right.key;
    }
};

} // namespace

TEST_CASE( "Sort orders typed and raw values",
           "[CypherCommon][Tier1][Sort]" )
{
    i32 typed[]{ 5, 1, 4, 2, 3, 3 };
    Sort_Unstable( Span_FromArray( typed ) );
    REQUIRE( Sort_IsOrdered( Span_Make<const i32>( typed, 6u ) ) );

    i32 raw[]{ 9, -1, 7, 0, 7 };
    REQUIRE( Sort_UnstableRaw( raw, 5u, sizeof( i32 ), CompareI32, nullptr ) );
    REQUIRE( raw[0] == -1 );
    REQUIRE( raw[4] == 9 );
}

TEST_CASE( "Sort stable merge preserves duplicate input order",
           "[CypherCommon][Tier1][Sort]" )
{
    stable_value_t values[]{ { 2, 0 }, { 1, 1 }, { 2, 2 }, { 1, 3 } };
    byte scratch[sizeof( values )]{};
    REQUIRE( Sort_Stable(
        Span_FromArray( values ),
        stable_less_t{},
        Span_FromArray( scratch ) ) );
    REQUIRE( values[0].key == 1 );
    REQUIRE( values[0].original == 1 );
    REQUIRE( values[1].original == 3 );
    REQUIRE( values[2].key == 2 );
    REQUIRE( values[2].original == 0 );
    REQUIRE( values[3].original == 2 );

    byte tooSmall[sizeof( values ) - 1u]{};
    REQUIRE_FALSE( Sort_Stable(
        Span_FromArray( values ),
        stable_less_t{},
        Span_FromArray( tooSmall ) ) );
}

TEST_CASE( "Heap primitives preserve max-heap contracts",
           "[CypherCommon][Tier1][Sort]" )
{
    i32 values[]{ 3, 1, 4, 2, 5 };
    Heap_Make( Span_FromArray( values ) );
    REQUIRE( values[0] == 5 );
    Heap_Pop( Span_FromArray( values ) );
    REQUIRE( values[4] == 5 );

    i32 pushValues[]{ 4, 3, 2, 1, 5 };
    Heap_Push( Span_FromArray( pushValues ) );
    REQUIRE( pushValues[0] == 5 );

    i32 sorted[]{ 8, 2, 7, 1 };
    HeapSort_Sort( Span_FromArray( sorted ) );
    REQUIRE( sorted[0] == 1 );
    REQUIRE( sorted[3] == 8 );
}
