//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_MemoryStream_Tests.cpp
//  Purpose: Tests borrowed read-only and writable memory streams.
//  Details: These tests protect capabilities, bounds, partial transfer status,
//           patch seeking, logical growth initialization, truncation, and overlap.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_MemoryStream.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "MemoryStream read-only adapter exposes initialized bytes",
           "[CypherCommon][Tier1][MemoryStream]" )
{
    const byte source[]{ 1u, 2u, 3u, 4u };
    memory_stream_t memory{};
    REQUIRE( MemoryStream_InitRead( &memory, { source, sizeof( source ) } ) );
    REQUIRE( MemoryStream_IsValid( &memory ) );
    REQUIRE_FALSE( MemoryStream_IsWritable( &memory ) );

    stream_t stream = MemoryStream_AsStream( &memory );
    REQUIRE( Stream_HasCapabilities(
        &stream,
        STREAM_CAPABILITY_READ | STREAM_CAPABILITY_SEEK |
        STREAM_CAPABILITY_SIZE ) );
    REQUIRE_FALSE( Stream_HasCapabilities( &stream, STREAM_CAPABILITY_WRITE ) );

    byte output[3]{};
    REQUIRE( Stream_ReadExact( &stream, output, sizeof( output ) ) == stream_status_t::OK );
    REQUIRE( output[0] == 1u );
    REQUIRE( output[1] == 2u );
    REQUIRE( output[2] == 3u );

    const stream_io_result_t tail = Stream_Read( &stream, output, sizeof( output ) );
    REQUIRE( tail.status == stream_status_t::END_OF_STREAM );
    REQUIRE( tail.cbTransferred == 1u );
    REQUIRE( output[0] == 4u );
    REQUIRE( Stream_Write( &stream, source, 1u ).status == stream_status_t::UNSUPPORTED );

    MemoryStream_Reset( &memory );
    REQUIRE( MemoryStream_Position( &memory ) == 0u );
}

TEST_CASE( "MemoryStream writable adapter tracks high water and patch writes",
           "[CypherCommon][Tier1][MemoryStream]" )
{
    byte storage[16]{};
    memory_stream_t memory{};
    REQUIRE( MemoryStream_InitWrite( &memory, Span_FromArray( storage ) ) );
    REQUIRE( MemoryStream_IsWritable( &memory ) );
    REQUIRE( MemoryStream_Capacity( &memory ) == sizeof( storage ) );

    stream_t stream = MemoryStream_AsStream( &memory );
    const byte source[]{ 1u, 2u, 3u, 4u };
    REQUIRE( Stream_WriteExact( &stream, source, sizeof( source ) ) == stream_status_t::OK );
    REQUIRE( MemoryStream_Size( &memory ) == sizeof( source ) );

    REQUIRE( Stream_Seek(
        &stream,
        1,
        stream_seek_origin_t::BEGIN ) == stream_status_t::OK );
    const byte patch[]{ 0xAAu, 0xBBu };
    REQUIRE( Stream_WriteExact( &stream, patch, sizeof( patch ) ) == stream_status_t::OK );
    REQUIRE( MemoryStream_Size( &memory ) == sizeof( source ) );
    REQUIRE( storage[0] == 1u );
    REQUIRE( storage[1] == 0xAAu );
    REQUIRE( storage[2] == 0xBBu );
    REQUIRE( storage[3] == 4u );

    REQUIRE( MemoryStream_SetSize( &memory, 8u ) );
    for ( usize iByte = 4u; iByte < 8u; ++iByte ) {
        REQUIRE( storage[iByte] == 0u );
    }
    REQUIRE( MemoryStream_SetSize( &memory, 2u ) );
    REQUIRE( MemoryStream_Position( &memory ) == 2u );
    REQUIRE( MemoryStream_Clear( &memory ) );
    REQUIRE( MemoryStream_Size( &memory ) == 0u );
    REQUIRE( BinaryBlock_IsEmpty( MemoryStream_Block( &memory ) ) );
}

TEST_CASE( "MemoryStream reports partial capacity exhaustion without overflow",
           "[CypherCommon][Tier1][MemoryStream]" )
{
    byte storage[4]{};
    memory_stream_t memory{};
    REQUIRE( MemoryStream_InitWrite( &memory, Span_FromArray( storage ) ) );
    stream_t stream = MemoryStream_AsStream( &memory );

    const byte source[]{ 1u, 2u, 3u, 4u, 5u, 6u };
    const stream_io_result_t result = Stream_Write(
        &stream,
        source,
        sizeof( source ) );
    REQUIRE( result.status == stream_status_t::OUT_OF_RANGE );
    REQUIRE( result.cbTransferred == sizeof( storage ) );
    REQUIRE( MemoryStream_Size( &memory ) == sizeof( storage ) );
    REQUIRE( MemoryStream_Position( &memory ) == sizeof( storage ) );

    for ( usize iByte = 0u; iByte < sizeof( storage ); ++iByte ) {
        REQUIRE( storage[iByte] == source[iByte] );
    }
}

TEST_CASE( "MemoryStream uses overlap-safe memory transfers",
           "[CypherCommon][Tier1][MemoryStream]" )
{
    byte storage[]{ 1u, 2u, 3u, 4u, 5u, 0u };
    memory_stream_t memory{};
    REQUIRE( MemoryStream_InitWrite(
        &memory,
        Span_FromArray( storage ),
        5u ) );
    stream_t stream = MemoryStream_AsStream( &memory );
    REQUIRE( Stream_Seek(
        &stream,
        1,
        stream_seek_origin_t::BEGIN ) == stream_status_t::OK );
    REQUIRE( Stream_WriteExact( &stream, storage, 4u ) == stream_status_t::OK );
    REQUIRE( storage[0] == 1u );
    REQUIRE( storage[1] == 1u );
    REQUIRE( storage[2] == 2u );
    REQUIRE( storage[3] == 3u );
    REQUIRE( storage[4] == 4u );
}

