//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_ChecksumCRC64_Tests.cpp
//  Purpose: Tests CRC-64/ECMA-182 checksums.
//  Details: Pins the ECMA check value and verifies streaming and malformed-range
//           behavior independently from the CRC-32 implementation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_ChecksumCRC64.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_crc64AssertCount = 0u;

assert_action_t CaptureCRC64Assert( const assert_info_t & ) noexcept
{
    ++g_crc64AssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "CRC-64 matches the ECMA-182 check vector",
           "[CypherCommon][Tier1][ChecksumCRC64]" )
{
    const char text[] = "123456789";
    REQUIRE(
        ChecksumCRC64_Data( BinaryBlock_FromData( text, sizeof( text ) - 1u ) ) ==
        0x6C40DF5F0B497347ull );
    REQUIRE( ChecksumCRC64_Data( {} ) == 0u );
}

TEST_CASE( "CRC-64 streaming is independent of chunk boundaries",
           "[CypherCommon][Tier1][ChecksumCRC64]" )
{
    const byte data[]{ 0x00u, 0x80u, 0xFFu, 1u, 2u, 3u, 4u, 5u };
    crc64_t state = CY_CRC64_INITIAL;
    state = ChecksumCRC64_Update( state, { data, 1u } );
    state = ChecksumCRC64_Update( state, { data + 1u, 6u } );
    state = ChecksumCRC64_Update( state, { data + 7u, 1u } );

    REQUIRE( ChecksumCRC64_Finalize( state ) == ChecksumCRC64_Data( { data, 8u } ) );
    REQUIRE( ChecksumCRC64_Update( state, {} ) == state );
}

TEST_CASE( "CRC-64 rejects malformed binary ranges without changing state",
           "[CypherCommon][Tier1][ChecksumCRC64]" )
{
    g_crc64AssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureCRC64Assert );

    const crc64_t state = 0x0123456789ABCDEFull;
    REQUIRE( ChecksumCRC64_Update( state, { nullptr, 1u } ) == state );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_crc64AssertCount == static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
