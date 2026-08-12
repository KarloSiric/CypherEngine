//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_PacketBuffer_Tests.cpp
//  Purpose: Tests borrowed packet storage and cursor adapters.
//  Details: These tests protect byte and bit cursor commits, storage provenance,
//           failed-writer rejection, incoming payload sizing, and reset behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_PacketBuffer.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "PacketBuffer commits and reads a byte payload",
           "[CypherCommon][Tier1][PacketBuffer]" )
{
    byte storage[32]{};
    packet_buffer_t packet{};
    REQUIRE( PacketBuffer_Init( &packet, Span_FromArray( storage ) ) );
    REQUIRE( PacketBuffer_IsValid( &packet ) );
    REQUIRE( PacketBuffer_IsEmpty( &packet ) );
    REQUIRE( PacketBuffer_Capacity( &packet ) == sizeof( storage ) );

    byte_writer_t writer = PacketBuffer_ByteWriter( &packet );
    REQUIRE( ByteWriter_WriteU32( &writer, 0x12345678u ) );
    REQUIRE( ByteWriter_WriteString(
        &writer,
        StringView_FromCString( "net" ),
        CY_TRUE ) );
    REQUIRE( PacketBuffer_CommitByteWriter( &packet, writer ) );
    REQUIRE( PacketBuffer_Size( &packet ) == 8u );
    const binary_block_t packetBlock = PacketBuffer_Block( &packet );
    REQUIRE( packetBlock.pData == storage );
    REQUIRE( packetBlock.cbSize == 8u );

    byte_reader_t reader = PacketBuffer_ByteReader( &packet );
    u32 value = 0u;
    REQUIRE( ByteReader_ReadU32( &reader, &value ) );
    REQUIRE( value == 0x12345678u );
    string_view_t text{};
    REQUIRE( ByteReader_ReadCString( &reader, 3u, &text ) );
    REQUIRE( StringView_Equals( text, StringView_FromCString( "net" ) ) );

    PacketBuffer_Clear( &packet );
    REQUIRE( PacketBuffer_IsEmpty( &packet ) );
}

TEST_CASE( "PacketBuffer commits and reads a bit payload",
           "[CypherCommon][Tier1][PacketBuffer]" )
{
    byte storage[8]{};
    packet_buffer_t packet{};
    REQUIRE( PacketBuffer_Init( &packet, Span_FromArray( storage ) ) );

    bit_writer_t writer = PacketBuffer_BitWriter(
        &packet,
        bit_order_t::MOST_SIGNIFICANT_FIRST );
    REQUIRE( BitWriter_WriteBits( &writer, 0b101u, 3u ) );
    REQUIRE( BitWriter_WriteSignedBits( &writer, -7, 5u ) );
    REQUIRE( BitWriter_WriteBool( &writer, CY_TRUE ) );
    REQUIRE( PacketBuffer_CommitBitWriter( &packet, writer ) );
    REQUIRE( PacketBuffer_Size( &packet ) == 2u );

    bit_reader_t reader = PacketBuffer_BitReader(
        &packet,
        bit_order_t::MOST_SIGNIFICANT_FIRST );
    u64 value = 0u;
    i64 signedValue = 0;
    bool_t bValue = CY_FALSE;
    REQUIRE( BitReader_ReadBits( &reader, 3u, &value ) );
    REQUIRE( value == 0b101u );
    REQUIRE( BitReader_ReadSignedBits( &reader, 5u, &signedValue ) );
    REQUIRE( signedValue == -7 );
    REQUIRE( BitReader_ReadBool( &reader, &bValue ) );
    REQUIRE( bValue );
}

TEST_CASE( "PacketBuffer refuses failed and foreign writer commits",
           "[CypherCommon][Tier1][PacketBuffer]" )
{
    byte packetStorage[4]{};
    packet_buffer_t packet{};
    REQUIRE( PacketBuffer_Init( &packet, Span_FromArray( packetStorage ) ) );
    REQUIRE( PacketBuffer_SetSize( &packet, 2u ) );

    byte_writer_t failed = PacketBuffer_ByteWriter( &packet );
    REQUIRE_FALSE( ByteWriter_WriteU64( &failed, 1u ) );
    REQUIRE_FALSE( PacketBuffer_CommitByteWriter( &packet, failed ) );
    REQUIRE( PacketBuffer_Size( &packet ) == 2u );

    byte foreignStorage[4]{};
    byte_writer_t foreign{};
    REQUIRE( ByteWriter_Init( &foreign, Span_FromArray( foreignStorage ) ) );
    REQUIRE( ByteWriter_WriteU8( &foreign, 0xA5u ) );
    REQUIRE_FALSE( PacketBuffer_CommitByteWriter( &packet, foreign ) );
    REQUIRE( PacketBuffer_Size( &packet ) == 2u );

    REQUIRE_FALSE( PacketBuffer_SetSize( &packet, 5u ) );
    REQUIRE( PacketBuffer_Size( &packet ) == 2u );
}
