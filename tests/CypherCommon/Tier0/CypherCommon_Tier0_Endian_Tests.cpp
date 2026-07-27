//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Endian_Tests.cpp
//  Purpose: Tests Tier0 Endian Tests behavior.
//  Details: This test file guards expected behavior for the corresponding runtime
//           module. It should prefer focused edge cases over broad demonstrations.
//
//  History:
//  - Created by Karlo Siric on 2026-07-03
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Endian.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>

using namespace cypher::common;

TEST_CASE( "Endian byte swaps reverse integer byte order", "[CypherCommon][Tier0][Endian]" )
{
    REQUIRE( Cy_ByteSwap16( 0x1122u ) == 0x2211u );
    REQUIRE( Cy_ByteSwap32( 0x11223344u ) == 0x44332211u );
    REQUIRE( Cy_ByteSwap64( 0x1122334455667788ull ) == 0x8877665544332211ull );
}

TEST_CASE( "Endian host-to-little helpers follow platform byte order", "[CypherCommon][Tier0][Endian]" )
{
#if CYPHER_ENDIAN_LITTLE
    REQUIRE( Cy_HostToLittle16( 0x1122u ) == 0x1122u );
    REQUIRE( Cy_HostToLittle32( 0x11223344u ) == 0x11223344u );
    REQUIRE( Cy_HostToLittle64( 0x1122334455667788ull ) == 0x1122334455667788ull );
#else
    REQUIRE( Cy_HostToLittle16( 0x1122u ) == 0x2211u );
    REQUIRE( Cy_HostToLittle32( 0x11223344u ) == 0x44332211u );
    REQUIRE( Cy_HostToLittle64( 0x1122334455667788ull ) == 0x8877665544332211ull );
#endif
}

TEST_CASE( "Endian host-to-big helpers follow platform byte order", "[CypherCommon][Tier0][Endian]" )
{
#if CYPHER_ENDIAN_BIG
    REQUIRE( Cy_HostToBig16( 0x1122u ) == 0x1122u );
    REQUIRE( Cy_HostToBig32( 0x11223344u ) == 0x11223344u );
    REQUIRE( Cy_HostToBig64( 0x1122334455667788ull ) == 0x1122334455667788ull );
#else
    REQUIRE( Cy_HostToBig16( 0x1122u ) == 0x2211u );
    REQUIRE( Cy_HostToBig32( 0x11223344u ) == 0x44332211u );
    REQUIRE( Cy_HostToBig64( 0x1122334455667788ull ) == 0x8877665544332211ull );
#endif
}

TEST_CASE( "Endian little-to-host and big-to-host mirror host conversion helpers", "[CypherCommon][Tier0][Endian]" )
{
    REQUIRE( Cy_LittleToHost16( 0x1122u ) == Cy_HostToLittle16( 0x1122u ) );
    REQUIRE( Cy_LittleToHost32( 0x11223344u ) == Cy_HostToLittle32( 0x11223344u ) );
    REQUIRE( Cy_LittleToHost64( 0x1122334455667788ull ) == Cy_HostToLittle64( 0x1122334455667788ull ) );

    REQUIRE( Cy_BigToHost16( 0x1122u ) == Cy_HostToBig16( 0x1122u ) );
    REQUIRE( Cy_BigToHost32( 0x11223344u ) == Cy_HostToBig32( 0x11223344u ) );
    REQUIRE( Cy_BigToHost64( 0x1122334455667788ull ) == Cy_HostToBig64( 0x1122334455667788ull ) );
}

TEST_CASE( "Endian FourCC packs ASCII characters into little-endian integer layout", "[CypherCommon][Tier0][Endian]" )
{
    REQUIRE( Cy_MakeFourCC( 'C', 'Y', 'P', 'K' ) == 0x4B505943u );
    REQUIRE( Cy_MakeFourCC( 'B', 'S', 'P', '0' ) == 0x30505342u );
    REQUIRE( Cy_MakeFourCC( 'P', 'A', 'K', '\0' ) == 0x004B4150u );
}

TEST_CASE( "Endian FourCC helpers extract and compare packed characters", "[CypherCommon][Tier0][Endian]" )
{
    constexpr u32 nFourCC = Cy_MakeFourCC( 'C', 'Y', 'P', 'K' );

    STATIC_REQUIRE( Cy_FourCCChar0( nFourCC ) == 'C' );
    STATIC_REQUIRE( Cy_FourCCChar1( nFourCC ) == 'Y' );
    STATIC_REQUIRE( Cy_FourCCChar2( nFourCC ) == 'P' );
    STATIC_REQUIRE( Cy_FourCCChar3( nFourCC ) == 'K' );
    STATIC_REQUIRE( Cy_FourCCChar( nFourCC, 0u ) == 'C' );
    STATIC_REQUIRE( Cy_FourCCChar( nFourCC, 3u ) == 'K' );
    STATIC_REQUIRE( Cy_FourCCChar( nFourCC, 4u ) == '\0' );

    STATIC_REQUIRE( Cy_FourCCMatches( nFourCC, 'C', 'Y', 'P', 'K' ) );
    STATIC_REQUIRE_FALSE( Cy_FourCCMatches( nFourCC, 'K', 'P', 'Y', 'C' ) );
}

TEST_CASE( "Endian integer reads and writes support unaligned buffers", "[CypherCommon][Tier0][Endian]" )
{
    std::array<u8, 10u> bytes{};

    Cy_WriteLittle16( bytes.data() + 1u, 0x1122u );
    Cy_WriteLittle32( bytes.data() + 3u, 0x33445566u );
    REQUIRE( bytes[1u] == 0x22u );
    REQUIRE( bytes[2u] == 0x11u );
    REQUIRE( bytes[3u] == 0x66u );
    REQUIRE( bytes[4u] == 0x55u );
    REQUIRE( bytes[5u] == 0x44u );
    REQUIRE( bytes[6u] == 0x33u );
    REQUIRE( Cy_ReadLittle16( bytes.data() + 1u ) == 0x1122u );
    REQUIRE( Cy_ReadLittle32( bytes.data() + 3u ) == 0x33445566u );

    Cy_WriteBig64( bytes.data() + 1u, 0x1122334455667788ull );
    REQUIRE( bytes[1u] == 0x11u );
    REQUIRE( bytes[2u] == 0x22u );
    REQUIRE( bytes[3u] == 0x33u );
    REQUIRE( bytes[4u] == 0x44u );
    REQUIRE( bytes[5u] == 0x55u );
    REQUIRE( bytes[6u] == 0x66u );
    REQUIRE( bytes[7u] == 0x77u );
    REQUIRE( bytes[8u] == 0x88u );
    REQUIRE( Cy_ReadBig64( bytes.data() + 1u ) == 0x1122334455667788ull );
}

TEST_CASE( "Endian floating-point reads and writes preserve exact bits", "[CypherCommon][Tier0][Endian]" )
{
    std::array<u8, 20u> bytes{};
    constexpr f32 flValue32 = -123.5f;
    constexpr f64 flValue64 = 1.0 / 3.0;

    STATIC_REQUIRE( std::bit_cast<u32>( Cy_ByteSwapF32( Cy_ByteSwapF32( flValue32 ) ) ) ==
                    std::bit_cast<u32>( flValue32 ) );
    STATIC_REQUIRE( std::bit_cast<u64>( Cy_ByteSwapF64( Cy_ByteSwapF64( flValue64 ) ) ) ==
                    std::bit_cast<u64>( flValue64 ) );

    Cy_WriteLittleF32( bytes.data() + 1u, flValue32 );
    Cy_WriteLittleF64( bytes.data() + 5u, flValue64 );
    REQUIRE( std::bit_cast<u32>( Cy_ReadLittleF32( bytes.data() + 1u ) ) ==
             std::bit_cast<u32>( flValue32 ) );
    REQUIRE( std::bit_cast<u64>( Cy_ReadLittleF64( bytes.data() + 5u ) ) ==
             std::bit_cast<u64>( flValue64 ) );

    Cy_WriteBigF32( bytes.data() + 1u, flValue32 );
    Cy_WriteBigF64( bytes.data() + 5u, flValue64 );
    REQUIRE( std::bit_cast<u32>( Cy_ReadBigF32( bytes.data() + 1u ) ) ==
             std::bit_cast<u32>( flValue32 ) );
    REQUIRE( std::bit_cast<u64>( Cy_ReadBigF64( bytes.data() + 5u ) ) ==
             std::bit_cast<u64>( flValue64 ) );
}

TEST_CASE( "Endian FourCC has a stable serialized byte layout", "[CypherCommon][Tier0][Endian]" )
{
    std::array<u8, 4u> bytes{};
    Cy_WriteLittle32( bytes.data(), Cy_MakeFourCC( 'C', 'Y', 'P', 'K' ) );

    REQUIRE( bytes == std::array<u8, 4u>{ 'C', 'Y', 'P', 'K' } );
    REQUIRE( Cy_FourCCMatches( Cy_ReadLittle32( bytes.data() ), 'C', 'Y', 'P', 'K' ) );
}
