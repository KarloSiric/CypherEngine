//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_BitReader_Tests.cpp
//  Purpose: Tests bounds-checked bitstream reading.
//  Details: These tests protect LSB/MSB round trips, sign extension, byte alignment,
//           transactional output, seeking, reset, and sticky bounds failures.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_BitReader.h"
#include "CypherCommon_BitWriter.h"

#include <catch2/generators/catch_generators.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace cypher::common;

TEST_CASE( "BitReader round trips fields signed values and Booleans",
           "[CypherCommon][Tier1][BitReader][BitWriter]" )
{
    const bit_order_t bitOrder = GENERATE(
        bit_order_t::LEAST_SIGNIFICANT_FIRST,
        bit_order_t::MOST_SIGNIFICANT_FIRST );
    byte storage[32]{};
    bit_writer_t writer{};
    REQUIRE( BitWriter_Init( &writer, Span_FromArray( storage ), bitOrder ) );
    REQUIRE( BitWriter_WriteBits( &writer, 0b101u, 3u ) );
    REQUIRE( BitWriter_WriteBits( &writer, 0b11010u, 5u ) );
    REQUIRE( BitWriter_WriteBool( &writer, CY_TRUE ) );
    REQUIRE( BitWriter_WriteSignedBits( &writer, -16, 5u ) );
    REQUIRE( BitWriter_WriteSignedBits( &writer, 15, 5u ) );
    REQUIRE( BitWriter_WriteSignedBits(
        &writer,
        std::numeric_limits<i64>::min(),
        64u ) );

    bit_reader_t reader{};
    REQUIRE( BitReader_Init(
        &reader,
        BitWriter_Block( &writer ),
        BitWriter_BitsWritten( &writer ),
        bitOrder ) );
    REQUIRE( BitReader_IsValid( &reader ) );
    REQUIRE( BitReader_Size( &reader ) == BitWriter_BitsWritten( &writer ) );

    u64 value = 0u;
    REQUIRE( BitReader_ReadBits( &reader, 3u, &value ) );
    REQUIRE( value == 0b101u );
    REQUIRE( BitReader_ReadBits( &reader, 5u, &value ) );
    REQUIRE( value == 0b11010u );

    bool_t bValue = CY_FALSE;
    REQUIRE( BitReader_ReadBool( &reader, &bValue ) );
    REQUIRE( bValue );

    i64 nSigned = 0;
    REQUIRE( BitReader_ReadSignedBits( &reader, 5u, &nSigned ) );
    REQUIRE( nSigned == -16 );
    REQUIRE( BitReader_ReadSignedBits( &reader, 5u, &nSigned ) );
    REQUIRE( nSigned == 15 );
    REQUIRE( BitReader_ReadSignedBits( &reader, 64u, &nSigned ) );
    REQUIRE( nSigned == std::numeric_limits<i64>::min() );
    REQUIRE( BitReader_Remaining( &reader ) == 0u );
}

TEST_CASE( "BitReader aligns according to stream order",
           "[CypherCommon][Tier1][BitReader]" )
{
    const bit_order_t bitOrder = GENERATE(
        bit_order_t::LEAST_SIGNIFICANT_FIRST,
        bit_order_t::MOST_SIGNIFICANT_FIRST );
    const byte lsbSource[]{ 0xFDu, 0x2Au };
    const byte msbSource[]{ 0xBFu, 0x2Au };
    const binary_block_t source = bitOrder == bit_order_t::LEAST_SIGNIFICANT_FIRST
        ? binary_block_t{ lsbSource, sizeof( lsbSource ) }
        : binary_block_t{ msbSource, sizeof( msbSource ) };

    bit_reader_t reader{};
    REQUIRE( BitReader_Init( &reader, source, 16u, bitOrder ) );
    u64 value = 0u;
    REQUIRE( BitReader_ReadBits( &reader, 3u, &value ) );
    REQUIRE( value == 0b101u );
    REQUIRE( BitReader_AlignToByte( &reader ) );
    REQUIRE( BitReader_Offset( &reader ) == 8u );
    REQUIRE( BitReader_ReadBits( &reader, 8u, &value ) );
    REQUIRE( value == 0x2Au );
}

TEST_CASE( "BitReader failures preserve output and cursor",
           "[CypherCommon][Tier1][BitReader]" )
{
    const byte source[]{ 0xFFu };
    bit_reader_t reader{};
    REQUIRE( BitReader_Init( &reader, { source, sizeof( source ) }, 8u ) );

    u64 value = 0u;
    REQUIRE( BitReader_ReadBits( &reader, 7u, &value ) );
    value = 0xA5u;
    REQUIRE_FALSE( BitReader_ReadBits( &reader, 2u, &value ) );
    REQUIRE( value == 0xA5u );
    REQUIRE( BitReader_Offset( &reader ) == 7u );
    REQUIRE( BitReader_Status( &reader ) == bit_cursor_status_t::OUT_OF_BOUNDS );

    REQUIRE_FALSE( BitReader_Skip( &reader, 1u ) );
    BitReader_ClearStatus( &reader );
    REQUIRE( BitReader_Seek( &reader, 0u ) );
    REQUIRE( BitReader_Offset( &reader ) == 0u );

    value = 0xA5u;
    REQUIRE_FALSE( BitReader_ReadBits( &reader, 0u, &value ) );
    REQUIRE( value == 0xA5u );
    REQUIRE( BitReader_Status( &reader ) == bit_cursor_status_t::INVALID_BIT_COUNT );

    BitReader_Reset( &reader );
    REQUIRE( BitReader_Status( &reader ) == bit_cursor_status_t::OK );
    REQUIRE( BitReader_Offset( &reader ) == 0u );
}

