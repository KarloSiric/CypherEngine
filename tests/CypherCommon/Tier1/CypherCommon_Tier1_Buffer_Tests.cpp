//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Buffer_Tests.cpp
//  Purpose: Tests bounded writes into caller-owned byte storage.
//  Details: Protects transactional capacity failure, overlapping append, zero fill,
//           logical resizing, writable ranges, assignment, and empty storage.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_Buffer.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_bufferAssertCount = 0u;

assert_action_t CaptureBufferAssert( const assert_info_t & ) noexcept
{
    ++g_bufferAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "Buffer initializes over caller storage and exposes state",
           "[CypherCommon][Tier1][Buffer]" )
{
    byte storage[32]{};
    buffer_t buffer{};
    REQUIRE( Buffer_Init( &buffer, { storage, sizeof( storage ) } ) );
    REQUIRE( Buffer_IsValid( &buffer ) );
    REQUIRE( Buffer_IsEmpty( &buffer ) );
    REQUIRE( Buffer_Data( &buffer ) == storage );
    REQUIRE( Buffer_Size( &buffer ) == 0u );
    REQUIRE( Buffer_Capacity( &buffer ) == sizeof( storage ) );
    REQUIRE( Buffer_Remaining( &buffer ) == sizeof( storage ) );

    const byte_span_t remaining = Buffer_RemainingSpan( &buffer );
    REQUIRE( remaining.pData == storage );
    REQUIRE( remaining.nCount == sizeof( storage ) );
}

TEST_CASE( "Buffer append and assignment preserve byte order",
           "[CypherCommon][Tier1][Buffer]" )
{
    byte storage[16]{};
    buffer_t buffer{};
    REQUIRE( Buffer_Init( &buffer, { storage, sizeof( storage ) } ) );

    const byte first[] = { 1u, 2u, 3u, 4u };
    const byte second[] = { 5u, 6u };
    REQUIRE( Buffer_Append( &buffer, first, sizeof( first ) ) );
    REQUIRE( Buffer_AppendBlock(
        &buffer,
        BinaryBlock_FromData( second, sizeof( second ) ) ) );
    REQUIRE( Buffer_AppendByte( &buffer, 7u ) );
    REQUIRE( Buffer_Size( &buffer ) == 7u );

    for ( usize iByte = 0u; iByte < 7u; ++iByte ) {
        REQUIRE( storage[iByte] == static_cast<byte>( iByte + 1u ) );
    }

    const byte replacement[] = { 9u, 8u, 7u };
    REQUIRE( Buffer_Assign(
        &buffer,
        BinaryBlock_FromData( replacement, sizeof( replacement ) ) ) );
    REQUIRE( Buffer_Size( &buffer ) == sizeof( replacement ) );
    REQUIRE( Cy_MemEqual( storage, replacement, sizeof( replacement ) ) );
}

TEST_CASE( "Buffer capacity failure leaves size and bytes unchanged",
           "[CypherCommon][Tier1][Buffer]" )
{
    byte storage[8]{};
    buffer_t buffer{};
    REQUIRE( Buffer_Init( &buffer, { storage, sizeof( storage ) } ) );
    const byte initial[] = { 1u, 2u, 3u, 4u, 5u, 6u };
    REQUIRE( Buffer_Append( &buffer, initial, sizeof( initial ) ) );

    byte before[sizeof( storage )]{};
    Cy_MemCopy( before, storage, sizeof( storage ) );
    const byte overflow[] = { 7u, 8u, 9u };
    REQUIRE_FALSE( Buffer_Append( &buffer, overflow, sizeof( overflow ) ) );
    REQUIRE_FALSE( Buffer_AppendZero( &buffer, 3u ) );
    REQUIRE_FALSE( Buffer_Resize( &buffer, 9u ) );
    REQUIRE( Buffer_AppendUninitialized( &buffer, 3u ).pData == nullptr );

    REQUIRE( Buffer_Size( &buffer ) == sizeof( initial ) );
    REQUIRE( Cy_MemEqual( storage, before, sizeof( storage ) ) );
}

TEST_CASE( "Buffer supports overlapping append and explicit uninitialized writes",
           "[CypherCommon][Tier1][Buffer]" )
{
    byte storage[16]{};
    buffer_t buffer{};
    REQUIRE( Buffer_Init( &buffer, { storage, sizeof( storage ) } ) );
    const byte initial[] = { 1u, 2u, 3u, 4u };
    REQUIRE( Buffer_Append( &buffer, initial, sizeof( initial ) ) );
    REQUIRE( Buffer_Append( &buffer, storage + 1u, 3u ) );

    const byte expected[] = { 1u, 2u, 3u, 4u, 2u, 3u, 4u };
    REQUIRE( Cy_MemEqual( storage, expected, sizeof( expected ) ) );

    byte_span_t appended = Buffer_AppendUninitialized( &buffer, 2u );
    REQUIRE( appended.pData == storage + 7u );
    REQUIRE( appended.nCount == 2u );
    appended.pData[0] = 8u;
    appended.pData[1] = 9u;

    const byte_span_t logical = Buffer_WritableSpan( &buffer );
    REQUIRE( logical.nCount == 9u );
    REQUIRE( logical.pData[8] == 9u );
}

TEST_CASE( "Buffer zero append, resize, clear, and block views agree",
           "[CypherCommon][Tier1][Buffer]" )
{
    byte storage[16];
    Cy_MemSet( storage, 0xCCu, sizeof( storage ) );
    buffer_t buffer{};
    REQUIRE( Buffer_Init( &buffer, { storage, sizeof( storage ) } ) );
    REQUIRE( Buffer_AppendByte( &buffer, 3u ) );
    REQUIRE( Buffer_AppendZero( &buffer, 4u ) );
    REQUIRE( Cy_MemIsZero( storage + 1u, 4u ) );

    REQUIRE( Buffer_Resize( &buffer, 3u ) );
    const binary_block_t block = Buffer_Block( &buffer );
    REQUIRE( block.pData == storage );
    REQUIRE( block.cbSize == 3u );

    Buffer_Clear( &buffer );
    REQUIRE( Buffer_IsEmpty( &buffer ) );
    REQUIRE( storage[0] == 3u );
}

TEST_CASE( "Buffer accepts canonical empty storage and rejects invalid input",
           "[CypherCommon][Tier1][Buffer]" )
{
    buffer_t empty{};
    REQUIRE( Buffer_Init( &empty, {} ) );
    REQUIRE( Buffer_IsValid( &empty ) );
    REQUIRE( Buffer_Append( &empty, nullptr, 0u ) );
    REQUIRE( Buffer_AppendZero( &empty, 0u ) );

    g_bufferAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureBufferAssert );

    buffer_t buffer{};
    byte storage[4]{};
    REQUIRE( Buffer_Init( &buffer, { storage, sizeof( storage ) } ) );
    REQUIRE_FALSE( Buffer_Append( &buffer, nullptr, 1u ) );
    REQUIRE_FALSE( Buffer_Init( &buffer, { nullptr, 1u } ) );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_bufferAssertCount ==
        2u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
