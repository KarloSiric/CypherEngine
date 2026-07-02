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

using namespace cypher::common;

TEST_CASE( "Endian byte swaps reverse integer byte order", "[CypherCommon][Tier0][Endian]" )
{
    REQUIRE( ByteSwap16( 0x1122u ) == 0x2211u );
    REQUIRE( ByteSwap32( 0x11223344u ) == 0x44332211u );
    REQUIRE( ByteSwap64( 0x1122334455667788ull ) == 0x8877665544332211ull );
}

TEST_CASE( "Endian host-to-little helpers follow platform byte order", "[CypherCommon][Tier0][Endian]" )
{
#if CYPHER_ENDIAN_LITTLE
    REQUIRE( HostToLittle16( 0x1122u ) == 0x1122u );
    REQUIRE( HostToLittle32( 0x11223344u ) == 0x11223344u );
    REQUIRE( HostToLittle64( 0x1122334455667788ull ) == 0x1122334455667788ull );
#else
    REQUIRE( HostToLittle16( 0x1122u ) == 0x2211u );
    REQUIRE( HostToLittle32( 0x11223344u ) == 0x44332211u );
    REQUIRE( HostToLittle64( 0x1122334455667788ull ) == 0x8877665544332211ull );
#endif
}

TEST_CASE( "Endian host-to-big helpers follow platform byte order", "[CypherCommon][Tier0][Endian]" )
{
#if CYPHER_ENDIAN_BIG
    REQUIRE( HostToBig16( 0x1122u ) == 0x1122u );
    REQUIRE( HostToBig32( 0x11223344u ) == 0x11223344u );
    REQUIRE( HostToBig64( 0x1122334455667788ull ) == 0x1122334455667788ull );
#else
    REQUIRE( HostToBig16( 0x1122u ) == 0x2211u );
    REQUIRE( HostToBig32( 0x11223344u ) == 0x44332211u );
    REQUIRE( HostToBig64( 0x1122334455667788ull ) == 0x8877665544332211ull );
#endif
}

TEST_CASE( "Endian little-to-host and big-to-host mirror host conversion helpers", "[CypherCommon][Tier0][Endian]" )
{
    REQUIRE( LittleToHost16( 0x1122u ) == HostToLittle16( 0x1122u ) );
    REQUIRE( LittleToHost32( 0x11223344u ) == HostToLittle32( 0x11223344u ) );
    REQUIRE( LittleToHost64( 0x1122334455667788ull ) == HostToLittle64( 0x1122334455667788ull ) );

    REQUIRE( BigToHost16( 0x1122u ) == HostToBig16( 0x1122u ) );
    REQUIRE( BigToHost32( 0x11223344u ) == HostToBig32( 0x11223344u ) );
    REQUIRE( BigToHost64( 0x1122334455667788ull ) == HostToBig64( 0x1122334455667788ull ) );
}

TEST_CASE( "Endian FourCC packs ASCII characters into little-endian integer layout", "[CypherCommon][Tier0][Endian]" )
{
    REQUIRE( MakeFourCC( 'C', 'Y', 'P', 'K' ) == 0x4B505943u );
    REQUIRE( MakeFourCC( 'B', 'S', 'P', '0' ) == 0x30505342u );
    REQUIRE( MakeFourCC( 'P', 'A', 'K', '\0' ) == 0x004B4150u );
}

TEST_CASE( "Endian FourCC helpers extract and compare packed characters", "[CypherCommon][Tier0][Endian]" )
{
    constexpr u32 nFourCC = MakeFourCC( 'C', 'Y', 'P', 'K' );

    STATIC_REQUIRE( FourCCChar0( nFourCC ) == 'C' );
    STATIC_REQUIRE( FourCCChar1( nFourCC ) == 'Y' );
    STATIC_REQUIRE( FourCCChar2( nFourCC ) == 'P' );
    STATIC_REQUIRE( FourCCChar3( nFourCC ) == 'K' );
    STATIC_REQUIRE( FourCCChar( nFourCC, 0u ) == 'C' );
    STATIC_REQUIRE( FourCCChar( nFourCC, 3u ) == 'K' );
    STATIC_REQUIRE( FourCCChar( nFourCC, 4u ) == '\0' );

    STATIC_REQUIRE( FourCCMatches( nFourCC, 'C', 'Y', 'P', 'K' ) );
    STATIC_REQUIRE_FALSE( FourCCMatches( nFourCC, 'K', 'P', 'Y', 'C' ) );
}
