//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_ByteReader_Tests.cpp
//  Purpose: Tests bounds-checked binary reader behavior.
//  Details: These tests protect endian conversion, transactional cursor movement,
//           canonical variable integers, C-string bounds, and sticky failure state.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ByteReader.h"
#include "CypherCommon_ByteWriter.h"

#include <catch2/generators/catch_generators.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace cypher::common;

TEST_CASE( "ByteReader exposes cursor state and preserves it on failure",
           "[CypherCommon][Tier1][ByteReader]" )
{
    const byte source[]{ 0x10u, 0x20u, 0x30u, 0x40u };
    byte_reader_t reader{};
    REQUIRE( ByteReader_Init( &reader, { source, sizeof( source ) } ) );
    REQUIRE( ByteReader_IsValid( &reader ) );
    REQUIRE( ByteReader_Size( &reader ) == sizeof( source ) );
    REQUIRE( ByteReader_Offset( &reader ) == 0u );
    REQUIRE( ByteReader_Remaining( &reader ) == sizeof( source ) );

    u8 value = 0u;
    REQUIRE( ByteReader_ReadU8( &reader, &value ) );
    REQUIRE( value == 0x10u );
    REQUIRE( ByteReader_Skip( &reader, 1u ) );

    binary_block_t block{};
    REQUIRE( ByteReader_ReadBlock( &reader, 2u, &block ) );
    REQUIRE( block.cbSize == 2u );
    REQUIRE( block.pData[0] == 0x30u );
    REQUIRE( block.pData[1] == 0x40u );
    REQUIRE( ByteReader_Remaining( &reader ) == 0u );

    value = 0xA5u;
    REQUIRE_FALSE( ByteReader_ReadU8( &reader, &value ) );
    REQUIRE( value == 0xA5u );
    REQUIRE( ByteReader_Offset( &reader ) == sizeof( source ) );
    REQUIRE( ByteReader_Status( &reader ) == byte_cursor_status_t::OUT_OF_BOUNDS );

    REQUIRE_FALSE( ByteReader_Seek( &reader, 0u ) );
    ByteReader_ClearStatus( &reader );
    REQUIRE( ByteReader_Seek( &reader, 1u ) );
    REQUIRE( ByteReader_Offset( &reader ) == 1u );

    ByteReader_Reset( &reader );
    REQUIRE( ByteReader_Status( &reader ) == byte_cursor_status_t::OK );
    REQUIRE( ByteReader_Offset( &reader ) == 0u );
}

TEST_CASE( "ByteReader and ByteWriter round trip typed values in either byte order",
           "[CypherCommon][Tier1][ByteReader][ByteWriter]" )
{
    const data_byte_order_t byteOrder = GENERATE(
        data_byte_order_t::LITTLE,
        data_byte_order_t::BIG );

    byte storage[256]{};
    byte_writer_t writer{};
    REQUIRE( ByteWriter_Init( &writer, Span_FromArray( storage ), byteOrder ) );

    REQUIRE( ByteWriter_WriteU8( &writer, 0xA5u ) );
    REQUIRE( ByteWriter_WriteU16( &writer, 0x1234u ) );
    REQUIRE( ByteWriter_WriteU32( &writer, 0x89ABCDEFu ) );
    REQUIRE( ByteWriter_WriteU64( &writer, 0x0123456789ABCDEFull ) );
    REQUIRE( ByteWriter_WriteI32( &writer, -1234567 ) );
    REQUIRE( ByteWriter_WriteI64( &writer, std::numeric_limits<i64>::min() ) );
    REQUIRE( ByteWriter_WriteF32( &writer, -12.5f ) );
    REQUIRE( ByteWriter_WriteF64( &writer, 1.0 / 3.0 ) );

    const u64 unsignedValues[]{
        0u,
        127u,
        128u,
        16384u,
        std::numeric_limits<u64>::max()
    };
    for ( const u64 value : unsignedValues ) {
        REQUIRE( ByteWriter_WriteVarU64( &writer, value ) );
    }

    const i64 signedValues[]{
        std::numeric_limits<i64>::min(),
        -1,
        0,
        1,
        std::numeric_limits<i64>::max()
    };
    for ( const i64 value : signedValues ) {
        REQUIRE( ByteWriter_WriteVarI64( &writer, value ) );
    }
    REQUIRE( ByteWriter_WriteString(
        &writer,
        StringView_FromCString( "cypher" ),
        CY_TRUE ) );

    byte_reader_t reader{};
    REQUIRE( ByteReader_Init( &reader, ByteWriter_Block( &writer ), byteOrder ) );

    u8 nU8 = 0u;
    u16 nU16 = 0u;
    u32 nU32 = 0u;
    u64 nU64 = 0u;
    i32 nI32 = 0;
    i64 nI64 = 0;
    f32 flF32 = 0.0f;
    f64 flF64 = 0.0;
    REQUIRE( ByteReader_ReadU8( &reader, &nU8 ) );
    REQUIRE( ByteReader_ReadU16( &reader, &nU16 ) );
    REQUIRE( ByteReader_ReadU32( &reader, &nU32 ) );
    REQUIRE( ByteReader_ReadU64( &reader, &nU64 ) );
    REQUIRE( ByteReader_ReadI32( &reader, &nI32 ) );
    REQUIRE( ByteReader_ReadI64( &reader, &nI64 ) );
    REQUIRE( ByteReader_ReadF32( &reader, &flF32 ) );
    REQUIRE( ByteReader_ReadF64( &reader, &flF64 ) );

    REQUIRE( nU8 == 0xA5u );
    REQUIRE( nU16 == 0x1234u );
    REQUIRE( nU32 == 0x89ABCDEFu );
    REQUIRE( nU64 == 0x0123456789ABCDEFull );
    REQUIRE( nI32 == -1234567 );
    REQUIRE( nI64 == std::numeric_limits<i64>::min() );
    REQUIRE( flF32 == -12.5f );
    REQUIRE( flF64 == 1.0 / 3.0 );

    for ( const u64 expected : unsignedValues ) {
        u64 actual = 0u;
        REQUIRE( ByteReader_ReadVarU64( &reader, &actual ) );
        REQUIRE( actual == expected );
    }
    for ( const i64 expected : signedValues ) {
        i64 actual = 0;
        REQUIRE( ByteReader_ReadVarI64( &reader, &actual ) );
        REQUIRE( actual == expected );
    }

    string_view_t text{};
    REQUIRE( ByteReader_ReadCString( &reader, 6u, &text ) );
    REQUIRE( StringView_Equals( text, StringView_FromCString( "cypher" ) ) );
    REQUIRE( ByteReader_Remaining( &reader ) == 0u );
}

TEST_CASE( "ByteReader rejects malformed and non-canonical variable integers",
           "[CypherCommon][Tier1][ByteReader]" )
{
    SECTION( "truncated encoding" ) {
        const byte source[]{ 0x80u };
        byte_reader_t reader{};
        REQUIRE( ByteReader_Init( &reader, { source, sizeof( source ) } ) );
        u64 value = 77u;
        REQUIRE_FALSE( ByteReader_ReadVarU64( &reader, &value ) );
        REQUIRE( value == 77u );
        REQUIRE( ByteReader_Offset( &reader ) == 0u );
        REQUIRE( ByteReader_Status( &reader ) == byte_cursor_status_t::OUT_OF_BOUNDS );
    }

    SECTION( "overlong zero" ) {
        const byte source[]{ 0x80u, 0x00u };
        byte_reader_t reader{};
        REQUIRE( ByteReader_Init( &reader, { source, sizeof( source ) } ) );
        u64 value = 77u;
        REQUIRE_FALSE( ByteReader_ReadVarU64( &reader, &value ) );
        REQUIRE( value == 77u );
        REQUIRE( ByteReader_Offset( &reader ) == 0u );
        REQUIRE( ByteReader_Status( &reader ) == byte_cursor_status_t::INVALID_ENCODING );
    }

    SECTION( "more than 64 payload bits" ) {
        const byte source[]{
            0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
            0xFFu, 0xFFu, 0xFFu, 0xFFu, 0x02u
        };
        byte_reader_t reader{};
        REQUIRE( ByteReader_Init( &reader, { source, sizeof( source ) } ) );
        u64 value = 77u;
        REQUIRE_FALSE( ByteReader_ReadVarU64( &reader, &value ) );
        REQUIRE( value == 77u );
        REQUIRE( ByteReader_Offset( &reader ) == 0u );
        REQUIRE( ByteReader_Status( &reader ) == byte_cursor_status_t::INVALID_ENCODING );
    }
}

TEST_CASE( "ByteReader enforces bounded terminated strings transactionally",
           "[CypherCommon][Tier1][ByteReader]" )
{
    const byte source[]{ 'a', 'b', 0u, 'x', 0u };
    byte_reader_t reader{};
    REQUIRE( ByteReader_Init( &reader, { source, sizeof( source ) } ) );

    string_view_t text{};
    REQUIRE( ByteReader_ReadCString( &reader, 2u, &text ) );
    REQUIRE( StringView_Equals( text, StringView_FromCString( "ab" ) ) );
    REQUIRE( ByteReader_ReadCString( &reader, 1u, &text ) );
    REQUIRE( StringView_Equals( text, StringView_FromCString( "x" ) ) );

    const byte unterminated[]{ 'a', 'b', 'c' };
    REQUIRE( ByteReader_Init(
        &reader,
        { unterminated, sizeof( unterminated ) } ) );
    text = StringView_FromCString( "unchanged" );
    REQUIRE_FALSE( ByteReader_ReadCString( &reader, 2u, &text ) );
    REQUIRE( StringView_Equals( text, StringView_FromCString( "unchanged" ) ) );
    REQUIRE( ByteReader_Offset( &reader ) == 0u );
    REQUIRE( ByteReader_Status( &reader ) == byte_cursor_status_t::INVALID_ENCODING );
}
