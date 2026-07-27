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
    REQUIRE( Cy_Bit32( 0u ) == 0x00000001u );
    REQUIRE( Cy_Bit32( 31u ) == 0x80000000u );
    REQUIRE( Cy_Bit32( 32u ) == 0u );
    REQUIRE( Cy_Bit64( 0u ) == 0x0000000000000001ull );
    REQUIRE( Cy_Bit64( 63u ) == 0x8000000000000000ull );
    REQUIRE( Cy_Bit64( 64u ) == 0ull );

    REQUIRE( Cy_LowMask32( 0u ) == 0u );
    REQUIRE( Cy_LowMask32( 1u ) == 0x00000001u );
    REQUIRE( Cy_LowMask32( 8u ) == 0x000000FFu );
    REQUIRE( Cy_LowMask32( 32u ) == CY_U32_MAX );
    REQUIRE( Cy_LowMask32( 40u ) == CY_U32_MAX );

    REQUIRE( Cy_LowMask64( 0u ) == 0ull );
    REQUIRE( Cy_LowMask64( 8u ) == 0x00000000000000FFull );
    REQUIRE( Cy_LowMask64( 64u ) == CY_U64_MAX );
    REQUIRE( Cy_LowMask64( 80u ) == CY_U64_MAX );
}

TEST_CASE( "Bits flag and single-bit helpers mutate bitfields predictably", "[CypherCommon][Tier0][Bits]" )
{
    u32 nValue = 0u;

    nValue = Cy_SetFlags( nValue, 0x03u );
    REQUIRE( nValue == 0x03u );
    REQUIRE( Cy_HasAnyFlags( nValue, 0x02u ) );
    REQUIRE( Cy_HasAllFlags( nValue, 0x03u ) );
    REQUIRE_FALSE( Cy_HasAllFlags( nValue, 0x07u ) );

    nValue = Cy_ClearFlags( nValue, 0x01u );
    REQUIRE( nValue == 0x02u );

    nValue = Cy_ToggleFlags( nValue, 0x06u );
    REQUIRE( nValue == 0x04u );

    REQUIRE_FALSE( Cy_TestBit32( nValue, 1u ) );
    REQUIRE( Cy_TestBit32( nValue, 2u ) );
    REQUIRE( Cy_SetBit32( nValue, 5u ) == 0x24u );
    REQUIRE( Cy_ClearBit32( 0x24u, 2u ) == 0x20u );

    REQUIRE( Cy_TestBit64( Cy_SetBit64( 0ull, 40u ), 40u ) );
    REQUIRE( Cy_ClearBit64( Cy_Bit64( 40u ), 40u ) == 0ull );
    REQUIRE_FALSE( Cy_TestBit32( nValue, 32u ) );
    REQUIRE( Cy_SetBit32( nValue, 32u ) == nValue );
    REQUIRE( Cy_ClearBit64( CY_U64_MAX, 64u ) == CY_U64_MAX );

    u64 nValue64 = 0ull;
    nValue64 = Cy_SetFlags( nValue64, 0x100000000ull );
    REQUIRE( Cy_HasAllFlags( nValue64, 0x100000000ull ) );
    REQUIRE( Cy_ClearFlags( nValue64, 0x100000000ull ) == 0ull );
}

TEST_CASE( "Bits population count and zero counts match C++20 semantics", "[CypherCommon][Tier0][Bits]" )
{
    REQUIRE( Cy_PopCount32( 0u ) == 0 );
    REQUIRE( Cy_PopCount32( 0x0Fu ) == 4 );
    REQUIRE( Cy_PopCount64( 0xFF00FF00FF00FF00ull ) == 32 );

    REQUIRE( Cy_CountLeadingZeros32( 0u ) == 32 );
    REQUIRE( Cy_CountLeadingZeros32( 1u ) == 31 );
    REQUIRE( Cy_CountLeadingZeros64( 0ull ) == 64 );
    REQUIRE( Cy_CountLeadingZeros64( 1ull ) == 63 );

    REQUIRE( Cy_CountTrailingZeros32( 0u ) == 32 );
    REQUIRE( Cy_CountTrailingZeros32( 0x10u ) == 4 );
    REQUIRE( Cy_CountTrailingZeros64( 0ull ) == 64 );
    REQUIRE( Cy_CountTrailingZeros64( 0x100000000ull ) == 32 );
}

TEST_CASE( "Bits find helpers return bit indices or negative sentinel for zero", "[CypherCommon][Tier0][Bits]" )
{
    REQUIRE( Cy_FindLowestSetBit32( 0u ) == -1 );
    REQUIRE( Cy_FindLowestSetBit32( 0x10u ) == 4 );
    REQUIRE( Cy_FindLowestSetBit64( 0ull ) == -1 );
    REQUIRE( Cy_FindLowestSetBit64( 0x100000000ull ) == 32 );

    REQUIRE( Cy_FindHighestSetBit32( 0u ) == -1 );
    REQUIRE( Cy_FindHighestSetBit32( 0x80000000u ) == 31 );
    REQUIRE( Cy_FindHighestSetBit64( 0ull ) == -1 );
    REQUIRE( Cy_FindHighestSetBit64( 0x8000000000000000ull ) == 63 );
}

TEST_CASE( "Bits power-of-two helpers round values for allocator and container sizing", "[CypherCommon][Tier0][Bits]" )
{
    REQUIRE( Cy_NextPowerOfTwo32( 0u ) == 1u );
    REQUIRE( Cy_NextPowerOfTwo32( 1u ) == 1u );
    REQUIRE( Cy_NextPowerOfTwo32( 2u ) == 2u );
    REQUIRE( Cy_NextPowerOfTwo32( 3u ) == 4u );
    REQUIRE( Cy_NextPowerOfTwo32( 255u ) == 256u );
    REQUIRE( Cy_NextPowerOfTwo32( 256u ) == 256u );
    REQUIRE( Cy_NextPowerOfTwo32( 0x80000000u ) == 0x80000000u );
    REQUIRE( Cy_NextPowerOfTwo32( 0x80000001u ) == 0u );

    REQUIRE( Cy_NextPowerOfTwo64( 0ull ) == 1ull );
    REQUIRE( Cy_NextPowerOfTwo64( 3ull ) == 4ull );
    REQUIRE( Cy_NextPowerOfTwo64( 0x100000001ull ) == 0x200000000ull );
    REQUIRE( Cy_NextPowerOfTwo64( 0x8000000000000001ull ) == 0ull );

    u32 nRounded32 = 0u;
    REQUIRE( Cy_NextPowerOfTwo32Checked( 257u, nRounded32 ) );
    REQUIRE( nRounded32 == 512u );
    REQUIRE_FALSE( Cy_NextPowerOfTwo32Checked( 0x80000001u, nRounded32 ) );
    REQUIRE( nRounded32 == 0u );

    u64 nRounded64 = 0ull;
    REQUIRE_FALSE( Cy_NextPowerOfTwo64Checked( 0x8000000000000001ull, nRounded64 ) );
    REQUIRE( nRounded64 == 0ull );

    REQUIRE( Cy_PreviousPowerOfTwo32( 0u ) == 0u );
    REQUIRE( Cy_PreviousPowerOfTwo32( 1u ) == 1u );
    REQUIRE( Cy_PreviousPowerOfTwo32( 3u ) == 2u );
    REQUIRE( Cy_PreviousPowerOfTwo32( 255u ) == 128u );
    REQUIRE( Cy_PreviousPowerOfTwo32( 256u ) == 256u );

    REQUIRE( Cy_PreviousPowerOfTwo64( 0ull ) == 0ull );
    REQUIRE( Cy_PreviousPowerOfTwo64( 3ull ) == 2ull );
    REQUIRE( Cy_PreviousPowerOfTwo64( 0x100000001ull ) == 0x100000000ull );
}

TEST_CASE( "Bits rotate helpers wrap values around fixed-width integers", "[CypherCommon][Tier0][Bits]" )
{
    REQUIRE( Cy_RotateLeft32( 1u, 4 ) == 16u );
    REQUIRE( Cy_RotateRight32( 16u, 4 ) == 1u );
    REQUIRE( Cy_RotateLeft32( 0x80000000u, 1 ) == 1u );
    REQUIRE( Cy_RotateRight32( 1u, 1 ) == 0x80000000u );

    REQUIRE( Cy_RotateLeft64( 1ull, 8 ) == 256ull );
    REQUIRE( Cy_RotateRight64( 256ull, 8 ) == 1ull );
    REQUIRE( Cy_RotateLeft64( 0x8000000000000000ull, 1 ) == 1ull );
    REQUIRE( Cy_RotateRight64( 1ull, 1 ) == 0x8000000000000000ull );
}

TEST_CASE( "Bits range helpers clamp fields without invalid shifts", "[CypherCommon][Tier0][Bits]" )
{
    STATIC_REQUIRE( Cy_BitRangeMask32( 4u, 8u ) == 0x00000FF0u );
    STATIC_REQUIRE( Cy_BitRangeMask32( 28u, 8u ) == 0xF0000000u );
    STATIC_REQUIRE( Cy_BitRangeMask32( 32u, 1u ) == 0u );
    STATIC_REQUIRE( Cy_BitRangeMask64( 60u, 8u ) == 0xF000000000000000ull );

    STATIC_REQUIRE( Cy_ExtractBits32( 0x00000AB0u, 4u, 8u ) == 0xABu );
    STATIC_REQUIRE( Cy_ExtractBits32( CY_U32_MAX, 32u, 1u ) == 0u );
    STATIC_REQUIRE( Cy_ExtractBits64( 0x0F00000000000000ull, 56u, 4u ) == 0x0Full );

    STATIC_REQUIRE( Cy_ReplaceBits32( 0xFFFF0000u, 0xABu, 4u, 8u ) == 0xFFFF0AB0u );
    STATIC_REQUIRE( Cy_ReplaceBits32( 0x12345678u, 0xFFu, 32u, 8u ) == 0x12345678u );
    STATIC_REQUIRE(
        Cy_ReplaceBits64( 0ull, 0xFull, 60u, 8u ) == 0xF000000000000000ull );
}
