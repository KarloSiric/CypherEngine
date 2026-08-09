//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_FixedArray_Tests.cpp
//  Purpose: Tests compile-time fixed-extent array access.
//  Details: Protects count, const access, bounds checks, iteration endpoints,
//           spans, fill, and the zero-extent contract.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_FixedArray.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_fixedArrayAssertCount = 0u;

assert_action_t CaptureFixedArrayAssert( const assert_info_t & ) noexcept
{
    ++g_fixedArrayAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "FixedArray exposes stable contiguous element access",
           "[CypherCommon][Tier1][FixedArray]" )
{
    fixed_array_t<u32, 4u> array{ { 2u, 3u, 5u, 7u } };
    REQUIRE( FixedArray_Count( array ) == 4u );
    REQUIRE_FALSE( FixedArray_IsEmpty( array ) );
    REQUIRE( FixedArray_Data( &array ) == array.data );
    REQUIRE( *FixedArray_At( &array, 2u ) == 5u );
    REQUIRE( FixedArray_Front( &array ) == array.data );
    REQUIRE( FixedArray_Back( &array ) == array.data + 3u );
    REQUIRE( FixedArray_Begin( &array ) == array.data );
    REQUIRE( FixedArray_End( &array ) == array.data + 4u );

    const span_t<u32> span = FixedArray_Span( &array );
    REQUIRE( span.pData == array.data );
    REQUIRE( span.nCount == 4u );
}

TEST_CASE( "FixedArray const operations never expose mutable elements",
           "[CypherCommon][Tier1][FixedArray]" )
{
    const fixed_array_t<u32, 2u> array{ { 11u, 13u } };
    STATIC_REQUIRE( is_same_v<
        decltype( FixedArray_Data( &array ) ),
        const u32 *> );
    STATIC_REQUIRE( is_same_v<
        decltype( FixedArray_Span( &array ) ),
        span_t<const u32>> );
    REQUIRE( *FixedArray_Back( &array ) == 13u );
}

TEST_CASE( "FixedArray fill assigns every logical element",
           "[CypherCommon][Tier1][FixedArray]" )
{
    fixed_array_t<u32, 8u> array{};
    FixedArray_Fill( &array, 0xA5u );
    for ( u32 value : array.data ) {
        REQUIRE( value == 0xA5u );
    }
}

TEST_CASE( "FixedArray zero extent does not expose dummy storage",
           "[CypherCommon][Tier1][FixedArray]" )
{
    fixed_array_t<u32, 0u> array{};
    REQUIRE( FixedArray_Count( array ) == 0u );
    REQUIRE( FixedArray_IsEmpty( array ) );
    REQUIRE( FixedArray_Data( &array ) == nullptr );
    REQUIRE( FixedArray_Begin( &array ) == nullptr );
    REQUIRE( FixedArray_End( &array ) == nullptr );
    REQUIRE( FixedArray_Back( &array ) == nullptr );
    REQUIRE( FixedArray_Span( &array ).nCount == 0u );
}

TEST_CASE( "FixedArray invalid object and index assertions fail safely",
           "[CypherCommon][Tier1][FixedArray]" )
{
    g_fixedArrayAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureFixedArrayAssert );

    fixed_array_t<u32, 2u> array{};
    REQUIRE( FixedArray_At( &array, 2u ) == nullptr );
    REQUIRE( FixedArray_Data(
                 static_cast<fixed_array_t<u32, 2u> *>( nullptr ) ) == nullptr );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_fixedArrayAssertCount ==
        2u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
