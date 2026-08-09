//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_ByteWriter_Tests.cpp
//  Purpose: Tests bounds-checked binary writer behavior.
//  Details: These tests protect exact wire order, no-partial-write guarantees,
//           high-water tracking, patch-style seeking, overlap, reset, and sticky state.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ByteWriter.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "ByteWriter emits exact little and big endian bytes",
           "[CypherCommon][Tier1][ByteWriter]" )
{
    byte storage[16]{};
    byte_writer_t writer{};

    SECTION( "little endian" ) {
        REQUIRE( ByteWriter_Init(
            &writer,
            Span_FromArray( storage ),
            data_byte_order_t::LITTLE ) );
        REQUIRE( ByteWriter_WriteU16( &writer, 0x1234u ) );
        REQUIRE( ByteWriter_WriteU32( &writer, 0x89ABCDEFu ) );

        const byte expected[]{ 0x34u, 0x12u, 0xEFu, 0xCDu, 0xABu, 0x89u };
        REQUIRE( ByteWriter_BytesWritten( &writer ) == sizeof( expected ) );
        for ( usize iByte = 0u; iByte < sizeof( expected ); ++iByte ) {
            REQUIRE( storage[iByte] == expected[iByte] );
        }
    }

    SECTION( "big endian" ) {
        REQUIRE( ByteWriter_Init(
            &writer,
            Span_FromArray( storage ),
            data_byte_order_t::BIG ) );
        REQUIRE( ByteWriter_WriteU16( &writer, 0x1234u ) );
        REQUIRE( ByteWriter_WriteU32( &writer, 0x89ABCDEFu ) );

        const byte expected[]{ 0x12u, 0x34u, 0x89u, 0xABu, 0xCDu, 0xEFu };
        REQUIRE( ByteWriter_BytesWritten( &writer ) == sizeof( expected ) );
        for ( usize iByte = 0u; iByte < sizeof( expected ); ++iByte ) {
            REQUIRE( storage[iByte] == expected[iByte] );
        }
    }
}

TEST_CASE( "ByteWriter never partially writes and failure remains sticky",
           "[CypherCommon][Tier1][ByteWriter]" )
{
    byte storage[]{ 0xA5u, 0xA5u, 0xA5u };
    byte_writer_t writer{};
    REQUIRE( ByteWriter_Init( &writer, Span_FromArray( storage ) ) );

    REQUIRE_FALSE( ByteWriter_WriteU32( &writer, 0x12345678u ) );
    REQUIRE( ByteWriter_Status( &writer ) == byte_cursor_status_t::OUT_OF_BOUNDS );
    REQUIRE( ByteWriter_Offset( &writer ) == 0u );
    REQUIRE( ByteWriter_BytesWritten( &writer ) == 0u );
    REQUIRE( storage[0] == 0xA5u );
    REQUIRE( storage[1] == 0xA5u );
    REQUIRE( storage[2] == 0xA5u );

    REQUIRE_FALSE( ByteWriter_WriteU8( &writer, 0x11u ) );
    REQUIRE( storage[0] == 0xA5u );

    ByteWriter_ClearStatus( &writer );
    REQUIRE( ByteWriter_WriteU8( &writer, 0x11u ) );
    REQUIRE( storage[0] == 0x11u );
}

TEST_CASE( "ByteWriter seek patches initialized bytes and preserves high water",
           "[CypherCommon][Tier1][ByteWriter]" )
{
    byte storage[8]{};
    byte_writer_t writer{};
    REQUIRE( ByteWriter_Init( &writer, Span_FromArray( storage ) ) );
    REQUIRE( ByteWriter_WriteU32( &writer, 0x11223344u ) );
    REQUIRE( ByteWriter_BytesWritten( &writer ) == 4u );

    REQUIRE( ByteWriter_Seek( &writer, 1u ) );
    REQUIRE( ByteWriter_WriteU16( &writer, 0xAABBu ) );
    REQUIRE( ByteWriter_Offset( &writer ) == 3u );
    REQUIRE( ByteWriter_BytesWritten( &writer ) == 4u );
    REQUIRE( storage[0] == 0x44u );
    REQUIRE( storage[1] == 0xBBu );
    REQUIRE( storage[2] == 0xAAu );
    REQUIRE( storage[3] == 0x11u );

    REQUIRE_FALSE( ByteWriter_Seek( &writer, 5u ) );
    REQUIRE( ByteWriter_Offset( &writer ) == 3u );
    REQUIRE( ByteWriter_Status( &writer ) == byte_cursor_status_t::OUT_OF_BOUNDS );

    ByteWriter_Reset( &writer );
    REQUIRE( ByteWriter_Status( &writer ) == byte_cursor_status_t::OK );
    REQUIRE( ByteWriter_Offset( &writer ) == 0u );
    REQUIRE( ByteWriter_BytesWritten( &writer ) == 0u );
}

TEST_CASE( "ByteWriter supports overlapping source and destination ranges",
           "[CypherCommon][Tier1][ByteWriter]" )
{
    byte storage[8]{ 'a', 'b', 'c', 'd', 0u, 0u, 0u, 0u };
    byte_writer_t writer{};
    REQUIRE( ByteWriter_Init( &writer, Span_FromArray( storage ) ) );

    REQUIRE( ByteWriter_WriteString(
        &writer,
        { reinterpret_cast<const char *>( storage + 1u ), 3u },
        CY_TRUE ) );
    REQUIRE( ByteWriter_BytesWritten( &writer ) == 4u );
    REQUIRE( storage[0] == 'b' );
    REQUIRE( storage[1] == 'c' );
    REQUIRE( storage[2] == 'd' );
    REQUIRE( storage[3] == 0u );

    const binary_block_t block = ByteWriter_Block( &writer );
    REQUIRE( block.pData == storage );
    REQUIRE( block.cbSize == 4u );
}

