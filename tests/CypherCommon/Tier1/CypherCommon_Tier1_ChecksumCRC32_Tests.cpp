//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_ChecksumCRC32_Tests.cpp
//  Purpose: Tests CRC-32/ISO-HDLC checksums.
//  Details: Pins the standard check value and verifies chunk-independent streaming
//           plus safe handling of malformed borrowed ranges.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_ChecksumCRC32.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_crc32AssertCount = 0u;

assert_action_t CaptureCRC32Assert( const assert_info_t & ) noexcept
{
    ++g_crc32AssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "CRC-32 matches the ISO-HDLC check vector",
           "[CypherCommon][Tier1][ChecksumCRC32]" )
{
    const char text[] = "123456789";
    REQUIRE(
        ChecksumCRC32_Data( BinaryBlock_FromData( text, sizeof( text ) - 1u ) ) ==
        0xCBF43926u );
    REQUIRE( ChecksumCRC32_Data( {} ) == 0u );
}

TEST_CASE( "CRC-32 streaming is independent of chunk boundaries",
           "[CypherCommon][Tier1][ChecksumCRC32]" )
{
    const byte data[]{ 0x00u, 0x80u, 0xFFu, 1u, 2u, 3u, 4u, 5u };
    crc32_t state = CY_CRC32_INITIAL;
    state = ChecksumCRC32_Update( state, { data, 3u } );
    state = ChecksumCRC32_Update( state, { data + 3u, 2u } );
    state = ChecksumCRC32_Update( state, { data + 5u, 3u } );

    REQUIRE( ChecksumCRC32_Finalize( state ) == ChecksumCRC32_Data( { data, 8u } ) );
    REQUIRE( ChecksumCRC32_Update( state, {} ) == state );
}

TEST_CASE( "CRC-32 rejects malformed binary ranges without changing state",
           "[CypherCommon][Tier1][ChecksumCRC32]" )
{
    g_crc32AssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureCRC32Assert );

    const crc32_t state = 0x12345678u;
    REQUIRE( ChecksumCRC32_Update( state, { nullptr, 1u } ) == state );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_crc32AssertCount == static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
