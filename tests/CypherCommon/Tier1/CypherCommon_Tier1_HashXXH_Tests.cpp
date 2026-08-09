//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_HashXXH_Tests.cpp
//  Purpose: Tests the stable xxHash adapter.
//  Details: Pins upstream 0.8.3 sanity vectors, 128-bit word ordering, seeding,
//           binary input behavior, and malformed borrowed ranges.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_HashXXH.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_hashXXHAssertCount = 0u;

assert_action_t CaptureHashXXHAssert( const assert_info_t & ) noexcept
{
    ++g_hashXXHAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "xxHash adapter matches upstream empty-input sanity vectors",
           "[CypherCommon][Tier1][HashXXH]" )
{
    REQUIRE( HashXXH32_Data( {} ) == 0x02CC5D05u );
    REQUIRE( HashXXH64_Data( {} ) == 0xEF46DB3751D8E999ull );
    REQUIRE( HashXXH3_64_Data( {} ) == 0x2D06800538D394C2ull );

    const hash128_t hash = HashXXH3_128_Data( {} );
    REQUIRE( hash.low == 0x6001C324468D497Full );
    REQUIRE( hash.high == 0x99AA06D3014798D8ull );
}

TEST_CASE( "xxHash adapter is deterministic and seed-sensitive for binary data",
           "[CypherCommon][Tier1][HashXXH]" )
{
    const byte bytes[]{ 0x00u, 0x7Fu, 0x80u, 0xFFu, 'c', 'y' };
    const binary_block_t data{ bytes, sizeof( bytes ) };

    REQUIRE( HashXXH32_Data( data ) == HashXXH32_Data( data ) );
    REQUIRE( HashXXH64_Data( data ) == HashXXH64_Data( data ) );
    REQUIRE( HashXXH3_64_Data( data ) == HashXXH3_64_Data( data ) );
    REQUIRE( Hash128_Equals(
        HashXXH3_128_Data( data ),
        HashXXH3_128_Data( data ) ) );

    REQUIRE( HashXXH32_Data( data, 1u ) != HashXXH32_Data( data, 2u ) );
    REQUIRE( HashXXH64_Data( data, 1u ) != HashXXH64_Data( data, 2u ) );
    REQUIRE( HashXXH3_64_Data( data, 1u ) != HashXXH3_64_Data( data, 2u ) );
    REQUIRE_FALSE( Hash128_Equals(
        HashXXH3_128_Data( data, 1u ),
        HashXXH3_128_Data( data, 2u ) ) );
}

TEST_CASE( "xxHash adapter rejects malformed borrowed ranges",
           "[CypherCommon][Tier1][HashXXH]" )
{
    g_hashXXHAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureHashXXHAssert );

    const binary_block_t invalid{ nullptr, 1u };
    REQUIRE( HashXXH32_Data( invalid ) == 0u );
    REQUIRE( HashXXH64_Data( invalid ) == 0u );
    REQUIRE( HashXXH3_64_Data( invalid ) == 0u );
    REQUIRE( Hash128_Equals( HashXXH3_128_Data( invalid ), {} ) );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_hashXXHAssertCount ==
        4u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
