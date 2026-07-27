//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Align_Tests.cpp
//  Purpose: Tests Tier0 Align Tests behavior.
//  Details: This test file guards expected behavior for the corresponding runtime
//           module. It should prefer focused edge cases over broad demonstrations.
//
//  History:
//  - Created by Karlo Siric on 2026-07-03
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Align.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "Align power-of-two checks reject zero and non power-of-two values", "[CypherCommon][Tier0][Align]" )
{
    STATIC_REQUIRE_FALSE( Cy_AlignIsPowerOfTwo( 0u ) );
    STATIC_REQUIRE( Cy_AlignIsPowerOfTwo( 1u ) );
    STATIC_REQUIRE( Cy_AlignIsPowerOfTwo( 2u ) );
    STATIC_REQUIRE_FALSE( Cy_AlignIsPowerOfTwo( 3u ) );
    STATIC_REQUIRE( Cy_AlignIsPowerOfTwo( 64u ) );
    STATIC_REQUIRE_FALSE( Cy_AlignIsPowerOfTwo( 96u ) );
}

TEST_CASE( "Align value helpers round power-of-two alignments up and down", "[CypherCommon][Tier0][Align]" )
{
    REQUIRE( Cy_AlignUp( 0u, 8u ) == 0u );
    REQUIRE( Cy_AlignUp( 1u, 8u ) == 8u );
    REQUIRE( Cy_AlignUp( 8u, 8u ) == 8u );
    REQUIRE( Cy_AlignUp( 13u, 8u ) == 16u );
    REQUIRE( Cy_AlignUp( 17u, 8u ) == 24u );

    REQUIRE( Cy_AlignDown( 0u, 8u ) == 0u );
    REQUIRE( Cy_AlignDown( 1u, 8u ) == 0u );
    REQUIRE( Cy_AlignDown( 8u, 8u ) == 8u );
    REQUIRE( Cy_AlignDown( 17u, 8u ) == 16u );

    REQUIRE( Cy_AlignIsAligned( 0u, 8u ) );
    REQUIRE( Cy_AlignIsAligned( 16u, 8u ) );
    REQUIRE_FALSE( Cy_AlignIsAligned( 17u, 8u ) );
    REQUIRE_FALSE( Cy_AlignIsAligned( 0u, 0u ) );
    REQUIRE_FALSE( Cy_AlignIsAligned( 16u, 3u ) );
}

TEST_CASE( "Align padding reports bytes needed to reach the next boundary", "[CypherCommon][Tier0][Align]" )
{
    REQUIRE( Cy_AlignPadding( 0u, 8u ) == 0u );
    REQUIRE( Cy_AlignPadding( 1u, 8u ) == 7u );
    REQUIRE( Cy_AlignPadding( 8u, 8u ) == 0u );
    REQUIRE( Cy_AlignPadding( 13u, 8u ) == 3u );
    REQUIRE( Cy_AlignPadding( 17u, 8u ) == 7u );
}

TEST_CASE( "Align checked helper rejects invalid alignment and overflow", "[CypherCommon][Tier0][Align]" )
{
    usize nOutValue = 0u;

    REQUIRE( Cy_AlignUpChecked( 13u, 8u, nOutValue ) );
    REQUIRE( nOutValue == 16u );

    REQUIRE_FALSE( Cy_AlignUpChecked( 13u, 0u, nOutValue ) );
    REQUIRE( nOutValue == 0u );

    REQUIRE_FALSE( Cy_AlignUpChecked( 13u, 3u, nOutValue ) );
    REQUIRE( nOutValue == 0u );

    REQUIRE_FALSE( Cy_AlignUpChecked( CY_INVALID_SIZE - 3u, 8u, nOutValue ) );
    REQUIRE( nOutValue == 0u );

    REQUIRE( Cy_AlignUpChecked( CY_INVALID_SIZE, 1u, nOutValue ) );
    REQUIRE( nOutValue == CY_INVALID_SIZE );

    REQUIRE( Cy_AlignDownChecked( 13u, 8u, nOutValue ) );
    REQUIRE( nOutValue == 8u );
    REQUIRE_FALSE( Cy_AlignDownChecked( 13u, 3u, nOutValue ) );
    REQUIRE( nOutValue == 0u );

    usize nPadding = 0u;
    REQUIRE( Cy_AlignPaddingChecked( 13u, 8u, nPadding ) );
    REQUIRE( nPadding == 3u );
    REQUIRE_FALSE( Cy_AlignPaddingChecked( CY_INVALID_SIZE, 8u, nPadding ) );
    REQUIRE( nPadding == 0u );
}

TEST_CASE( "Align pointer helpers round pointer addresses and report padding", "[CypherCommon][Tier0][Align]" )
{
    alignas( 64 ) u8 nBytes[128] = {};
    void *pOffset = nBytes + 13u;
    const void *pConstOffset = nBytes + 17u;

    REQUIRE( Cy_AlignIsPointerAligned( nBytes, 64u ) );
    REQUIRE_FALSE( Cy_AlignIsPointerAligned( pOffset, 16u ) );

    REQUIRE( Cy_AlignPointerUp( pOffset, 16u ) == nBytes + 16u );
    REQUIRE( Cy_AlignPointerDown( pOffset, 16u ) == nBytes );
    REQUIRE( Cy_AlignPointerUp( pConstOffset, 16u ) == nBytes + 32u );
    REQUIRE( Cy_AlignPointerDown( pConstOffset, 16u ) == nBytes + 16u );

    REQUIRE( Cy_AlignPointerPadding( pOffset, 16u ) == 3u );

    uintptr nOutAddress = 0u;
    REQUIRE( Cy_AlignPointerUpChecked( pOffset, 16u, nOutAddress ) );
    REQUIRE( nOutAddress == reinterpret_cast<uintptr>( nBytes + 16u ) );
}
