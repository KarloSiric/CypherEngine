//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Bits_Tests.cpp
//  Purpose: Tests Tier0 Bits Tests behavior.
//  Details: This test file guards expected behavior for the corresponding runtime
//           module. It should prefer focused edge cases over broad demonstrations.
//
//  History:
//  - Created by Karlo Siric on 2026-07-03
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Bits.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "Bits single-bit and low-mask helpers build expected masks", "[CypherCommon][Tier0][Bits]" )
{
    REQUIRE( Bit32( 0u ) == 0x00000001u );
    REQUIRE( Bit32( 31u ) == 0x80000000u );
    REQUIRE( Bit64( 0u ) == 0x0000000000000001ull );
    REQUIRE( Bit64( 63u ) == 0x8000000000000000ull );

    REQUIRE( LowMask32( 0u ) == 0u );
    REQUIRE( LowMask32( 1u ) == 0x00000001u );
    REQUIRE( LowMask32( 8u ) == 0x000000FFu );
    REQUIRE( LowMask32( 32u ) == CY_U32_MAX );
    REQUIRE( LowMask32( 40u ) == CY_U32_MAX );

    REQUIRE( LowMask64( 0u ) == 0ull );
    REQUIRE( LowMask64( 8u ) == 0x00000000000000FFull );
    REQUIRE( LowMask64( 64u ) == CY_U64_MAX );
    REQUIRE( LowMask64( 80u ) == CY_U64_MAX );
}

TEST_CASE( "Bits flag and single-bit helpers mutate bitfields predictably", "[CypherCommon][Tier0][Bits]" )
{
    u32 nValue = 0u;

    nValue = SetFlags( nValue, 0x03u );
    REQUIRE( nValue == 0x03u );
    REQUIRE( HasAnyFlags( nValue, 0x02u ) );
    REQUIRE( HasAllFlags( nValue, 0x03u ) );
    REQUIRE_FALSE( HasAllFlags( nValue, 0x07u ) );

    nValue = ClearFlags( nValue, 0x01u );
    REQUIRE( nValue == 0x02u );

    nValue = ToggleFlags( nValue, 0x06u );
    REQUIRE( nValue == 0x04u );

    REQUIRE_FALSE( TestBit32( nValue, 1u ) );
    REQUIRE( TestBit32( nValue, 2u ) );
    REQUIRE( SetBit32( nValue, 5u ) == 0x24u );
    REQUIRE( ClearBit32( 0x24u, 2u ) == 0x20u );

    REQUIRE( TestBit64( SetBit64( 0ull, 40u ), 40u ) );
    REQUIRE( ClearBit64( Bit64( 40u ), 40u ) == 0ull );
}

TEST_CASE( "Bits population count and zero counts match C++20 semantics", "[CypherCommon][Tier0][Bits]" )
{
    REQUIRE( PopCount32( 0u ) == 0 );
    REQUIRE( PopCount32( 0x0Fu ) == 4 );
    REQUIRE( PopCount64( 0xFF00FF00FF00FF00ull ) == 32 );

    REQUIRE( CountLeadingZeros32( 0u ) == 32 );
    REQUIRE( CountLeadingZeros32( 1u ) == 31 );
    REQUIRE( CountLeadingZeros64( 0ull ) == 64 );
    REQUIRE( CountLeadingZeros64( 1ull ) == 63 );

    REQUIRE( CountTrailingZeros32( 0u ) == 32 );
    REQUIRE( CountTrailingZeros32( 0x10u ) == 4 );
    REQUIRE( CountTrailingZeros64( 0ull ) == 64 );
    REQUIRE( CountTrailingZeros64( 0x100000000ull ) == 32 );
}

TEST_CASE( "Bits find helpers return bit indices or negative sentinel for zero", "[CypherCommon][Tier0][Bits]" )
{
    REQUIRE( FindLowestSetBit32( 0u ) == -1 );
    REQUIRE( FindLowestSetBit32( 0x10u ) == 4 );
    REQUIRE( FindLowestSetBit64( 0ull ) == -1 );
    REQUIRE( FindLowestSetBit64( 0x100000000ull ) == 32 );

    REQUIRE( FindHighestSetBit32( 0u ) == -1 );
    REQUIRE( FindHighestSetBit32( 0x80000000u ) == 31 );
    REQUIRE( FindHighestSetBit64( 0ull ) == -1 );
    REQUIRE( FindHighestSetBit64( 0x8000000000000000ull ) == 63 );
}

TEST_CASE( "Bits power-of-two helpers round values for allocator and container sizing", "[CypherCommon][Tier0][Bits]" )
{
    REQUIRE( NextPowerOfTwo32( 0u ) == 1u );
    REQUIRE( NextPowerOfTwo32( 1u ) == 1u );
    REQUIRE( NextPowerOfTwo32( 2u ) == 2u );
    REQUIRE( NextPowerOfTwo32( 3u ) == 4u );
    REQUIRE( NextPowerOfTwo32( 255u ) == 256u );
    REQUIRE( NextPowerOfTwo32( 256u ) == 256u );

    REQUIRE( NextPowerOfTwo64( 0ull ) == 1ull );
    REQUIRE( NextPowerOfTwo64( 3ull ) == 4ull );
    REQUIRE( NextPowerOfTwo64( 0x100000001ull ) == 0x200000000ull );

    REQUIRE( PreviousPowerOfTwo32( 0u ) == 0u );
    REQUIRE( PreviousPowerOfTwo32( 1u ) == 1u );
    REQUIRE( PreviousPowerOfTwo32( 3u ) == 2u );
    REQUIRE( PreviousPowerOfTwo32( 255u ) == 128u );
    REQUIRE( PreviousPowerOfTwo32( 256u ) == 256u );

    REQUIRE( PreviousPowerOfTwo64( 0ull ) == 0ull );
    REQUIRE( PreviousPowerOfTwo64( 3ull ) == 2ull );
    REQUIRE( PreviousPowerOfTwo64( 0x100000001ull ) == 0x100000000ull );
}

TEST_CASE( "Bits rotate helpers wrap values around fixed-width integers", "[CypherCommon][Tier0][Bits]" )
{
    REQUIRE( RotateLeft32( 1u, 4 ) == 16u );
    REQUIRE( RotateRight32( 16u, 4 ) == 1u );
    REQUIRE( RotateLeft32( 0x80000000u, 1 ) == 1u );
    REQUIRE( RotateRight32( 1u, 1 ) == 0x80000000u );

    REQUIRE( RotateLeft64( 1ull, 8 ) == 256ull );
    REQUIRE( RotateRight64( 256ull, 8 ) == 1ull );
    REQUIRE( RotateLeft64( 0x8000000000000000ull, 1 ) == 1ull );
    REQUIRE( RotateRight64( 1ull, 1 ) == 0x8000000000000000ull );
}
