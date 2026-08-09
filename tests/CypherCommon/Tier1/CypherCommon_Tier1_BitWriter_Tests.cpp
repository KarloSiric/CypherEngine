//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_BitWriter_Tests.cpp
//  Purpose: Tests bounds-checked bitstream writing.
//  Details: These tests protect exact LSB/MSB wire layout, deterministic padding,
//           range checks, no-partial writes, alignment, seeking, and sticky status.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_BitWriter.h"

#include <catch2/generators/catch_generators.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "BitWriter emits exact LSB-first and MSB-first bytes",
           "[CypherCommon][Tier1][BitWriter]" )
{
    byte storage[]{ 0xFFu, 0xFFu };
    bit_writer_t writer{};

    SECTION( "least significant first" ) {
        REQUIRE( BitWriter_Init(
            &writer,
            Span_FromArray( storage ),
            bit_order_t::LEAST_SIGNIFICANT_FIRST ) );
        REQUIRE( BitWriter_WriteBits( &writer, 0b101u, 3u ) );
        REQUIRE( BitWriter_WriteBits( &writer, 0b11010u, 5u ) );
        REQUIRE( BitWriter_WriteBits( &writer, 0b1010u, 4u ) );
        REQUIRE( storage[0] == 0xD5u );
        REQUIRE( storage[1] == 0x0Au );
    }

    SECTION( "most significant first" ) {
        REQUIRE( BitWriter_Init(
            &writer,
            Span_FromArray( storage ),
            bit_order_t::MOST_SIGNIFICANT_FIRST ) );
        REQUIRE( BitWriter_WriteBits( &writer, 0b101u, 3u ) );
        REQUIRE( BitWriter_WriteBits( &writer, 0b11010u, 5u ) );
        REQUIRE( BitWriter_WriteBits( &writer, 0b1010u, 4u ) );
        REQUIRE( storage[0] == 0xBAu );
        REQUIRE( storage[1] == 0xA0u );
    }

    REQUIRE( BitWriter_IsValid( &writer ) );
    REQUIRE( BitWriter_Offset( &writer ) == 12u );
    REQUIRE( BitWriter_BitsWritten( &writer ) == 12u );
    REQUIRE( BitWriter_Capacity( &writer ) == 16u );
    REQUIRE( BitWriter_Remaining( &writer ) == 4u );
    REQUIRE( BitWriter_Block( &writer ).cbSize == 2u );
}

TEST_CASE( "BitWriter failure is transactional and sticky",
           "[CypherCommon][Tier1][BitWriter]" )
{
    byte storage[]{ 0xCCu };
    bit_writer_t writer{};
    REQUIRE( BitWriter_Init( &writer, Span_FromArray( storage ) ) );
    REQUIRE( BitWriter_WriteBits( &writer, 0x7Fu, 7u ) );
    REQUIRE( storage[0] == 0x7Fu );

    REQUIRE_FALSE( BitWriter_WriteBits( &writer, 0x3u, 2u ) );
    REQUIRE( BitWriter_Status( &writer ) == bit_cursor_status_t::OUT_OF_BOUNDS );
    REQUIRE( BitWriter_Offset( &writer ) == 7u );
    REQUIRE( BitWriter_BitsWritten( &writer ) == 7u );
    REQUIRE( storage[0] == 0x7Fu );

    REQUIRE_FALSE( BitWriter_WriteBool( &writer, CY_TRUE ) );
    BitWriter_ClearStatus( &writer );
    REQUIRE( BitWriter_WriteBool( &writer, CY_TRUE ) );
    REQUIRE( storage[0] == 0xFFu );
}

TEST_CASE( "BitWriter rejects values that do not fit their field",
           "[CypherCommon][Tier1][BitWriter]" )
{
    byte storage[2]{};
    bit_writer_t writer{};
    REQUIRE( BitWriter_Init( &writer, Span_FromArray( storage ) ) );

    REQUIRE_FALSE( BitWriter_WriteBits( &writer, 8u, 3u ) );
    REQUIRE( BitWriter_Status( &writer ) == bit_cursor_status_t::VALUE_OUT_OF_RANGE );
    REQUIRE( BitWriter_BitsWritten( &writer ) == 0u );
    REQUIRE( storage[0] == 0u );

    BitWriter_Reset( &writer );
    REQUIRE_FALSE( BitWriter_WriteSignedBits( &writer, -5, 3u ) );
    REQUIRE( BitWriter_Status( &writer ) == bit_cursor_status_t::VALUE_OUT_OF_RANGE );
    REQUIRE( BitWriter_BitsWritten( &writer ) == 0u );

    BitWriter_Reset( &writer );
    REQUIRE_FALSE( BitWriter_WriteBits( &writer, 0u, 0u ) );
    REQUIRE( BitWriter_Status( &writer ) == bit_cursor_status_t::INVALID_BIT_COUNT );
}

TEST_CASE( "BitWriter aligns and patches only initialized bits",
           "[CypherCommon][Tier1][BitWriter]" )
{
    const bit_order_t bitOrder = GENERATE(
        bit_order_t::LEAST_SIGNIFICANT_FIRST,
        bit_order_t::MOST_SIGNIFICANT_FIRST );
    byte storage[]{ 0xFFu, 0xFFu };
    bit_writer_t writer{};
    REQUIRE( BitWriter_Init( &writer, Span_FromArray( storage ), bitOrder ) );
    REQUIRE( BitWriter_WriteBits( &writer, 0b101u, 3u ) );
    REQUIRE( BitWriter_AlignToByte( &writer, CY_TRUE ) );
    REQUIRE( BitWriter_BitsWritten( &writer ) == 8u );
    REQUIRE( storage[0] ==
             ( bitOrder == bit_order_t::LEAST_SIGNIFICANT_FIRST
                   ? 0xFDu
                   : 0xBFu ) );

    REQUIRE( BitWriter_Seek( &writer, 0u ) );
    REQUIRE( BitWriter_WriteBits( &writer, 0u, 3u ) );
    REQUIRE( BitWriter_BitsWritten( &writer ) == 8u );
    REQUIRE( storage[0] ==
             ( bitOrder == bit_order_t::LEAST_SIGNIFICANT_FIRST
                   ? 0xF8u
                   : 0x1Fu ) );

    REQUIRE_FALSE( BitWriter_Seek( &writer, 9u ) );
    REQUIRE( BitWriter_Offset( &writer ) == 3u );
    BitWriter_Reset( &writer );
    REQUIRE( BitWriter_BitsWritten( &writer ) == 0u );
    REQUIRE( BinaryBlock_IsEmpty( BitWriter_Block( &writer ) ) );
}
