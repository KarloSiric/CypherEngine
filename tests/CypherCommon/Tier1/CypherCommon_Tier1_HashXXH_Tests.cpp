//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_HashXXH_Tests.cpp
//  Purpose: Tests the stable xxHash adapter.
//  Details: Pins upstream 0.8.3 sanity vectors, 128-bit word ordering, seeding,
//           binary input behavior, and malformed borrowed ranges.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_HashXXH.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_hashXXHAssertCount = 0u;

assert_action_t CaptureHashXXHAssert( const assert_info_t & ) noexcept
{
    ++g_hashXXHAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "xxHash adapter matches upstream empty-input sanity vectors",
           "[CypherCommon][Tier1][HashXXH]" )
{
    REQUIRE( HashXXH32_Data( {} ) == 0x02CC5D05u );
    REQUIRE( HashXXH64_Data( {} ) == 0xEF46DB3751D8E999ull );
    REQUIRE( HashXXH3_64_Data( {} ) == 0x2D06800538D394C2ull );

    const hash128_t hash = HashXXH3_128_Data( {} );
    REQUIRE( hash.low == 0x6001C324468D497Full );
    REQUIRE( hash.high == 0x99AA06D3014798D8ull );
}

TEST_CASE( "xxHash adapter is deterministic and seed-sensitive for binary data",
           "[CypherCommon][Tier1][HashXXH]" )
{
    const byte bytes[]{ 0x00u, 0x7Fu, 0x80u, 0xFFu, 'c', 'y' };
    const binary_block_t data{ bytes, sizeof( bytes ) };

    REQUIRE( HashXXH32_Data( data ) == HashXXH32_Data( data ) );
    REQUIRE( HashXXH64_Data( data ) == HashXXH64_Data( data ) );
    REQUIRE( HashXXH3_64_Data( data ) == HashXXH3_64_Data( data ) );
    REQUIRE( Hash128_Equals(
        HashXXH3_128_Data( data ),
        HashXXH3_128_Data( data ) ) );

    REQUIRE( HashXXH32_Data( data, 1u ) != HashXXH32_Data( data, 2u ) );
    REQUIRE( HashXXH64_Data( data, 1u ) != HashXXH64_Data( data, 2u ) );
    REQUIRE( HashXXH3_64_Data( data, 1u ) != HashXXH3_64_Data( data, 2u ) );
    REQUIRE_FALSE( Hash128_Equals(
        HashXXH3_128_Data( data, 1u ),
        HashXXH3_128_Data( data, 2u ) ) );
}

TEST_CASE( "xxHash adapter rejects malformed borrowed ranges",
           "[CypherCommon][Tier1][HashXXH]" )
{
    g_hashXXHAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureHashXXHAssert );

    const binary_block_t invalid{ nullptr, 1u };
    REQUIRE( HashXXH32_Data( invalid ) == 0u );
    REQUIRE( HashXXH64_Data( invalid ) == 0u );
    REQUIRE( HashXXH3_64_Data( invalid ) == 0u );
    REQUIRE( Hash128_Equals( HashXXH3_128_Data( invalid ), {} ) );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_hashXXHAssertCount ==
        4u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}

TEST_CASE( "xxHash streaming digests equal one-shot hashes across chunk boundaries",
           "[CypherCommon][Tier1][HashXXH]" )
{
    byte bytes[1024]{};
    for ( usize iByte = 0u; iByte < sizeof( bytes ); ++iByte ) {
        bytes[iByte] = static_cast<byte>( ( iByte * 37u + 11u ) & 0xFFu );
    }
    const binary_block_t allData{ bytes, sizeof( bytes ) };
    constexpr hash64_t nSeed = 0x0123456789ABCDEFull;

    hash_xxh3_stream_t stream64{};
    REQUIRE( HashXXH3_StreamInit(
        &stream64,
        hash_xxh3_stream_mode_t::HASH_64,
        nSeed ) );
    usize iOffset = 0u;
    for ( usize cbChunk : { 1u, 7u, 128u, 3u, 257u, 19u, 609u } ) {
        REQUIRE( HashXXH3_StreamUpdate(
            &stream64,
            { bytes + iOffset, cbChunk } ) );
        iOffset += cbChunk;
    }
    REQUIRE( iOffset == sizeof( bytes ) );

    hash64_t streamHash64 = 0u;
    REQUIRE( HashXXH3_StreamDigest64( &stream64, &streamHash64 ) );
    REQUIRE( streamHash64 == HashXXH3_64_Data( allData, nSeed ) );

    hash_xxh3_stream_t stream128{};
    REQUIRE( HashXXH3_StreamInit(
        &stream128,
        hash_xxh3_stream_mode_t::HASH_128,
        nSeed ) );
    REQUIRE( HashXXH3_StreamUpdate( &stream128, { bytes, 333u } ) );
    REQUIRE( HashXXH3_StreamUpdate(
        &stream128,
        { bytes + 333u, sizeof( bytes ) - 333u } ) );
    hash128_t streamHash128{};
    REQUIRE( HashXXH3_StreamDigest128( &stream128, &streamHash128 ) );
    REQUIRE( Hash128_Equals(
        streamHash128,
        HashXXH3_128_Data( allData, nSeed ) ) );
}

TEST_CASE( "xxHash stream reset changes seed and digest does not consume state",
           "[CypherCommon][Tier1][HashXXH]" )
{
    const byte prefix[]{ 1u, 2u, 3u, 4u };
    const byte suffix[]{ 5u, 6u, 7u };
    hash_xxh3_stream_t stream{};
    REQUIRE( HashXXH3_StreamInit(
        &stream,
        hash_xxh3_stream_mode_t::HASH_64,
        7u ) );
    REQUIRE( HashXXH3_StreamUpdate( &stream, { prefix, sizeof( prefix ) } ) );

    hash64_t prefixHash = 0u;
    REQUIRE( HashXXH3_StreamDigest64( &stream, &prefixHash ) );
    REQUIRE( prefixHash == HashXXH3_64_Data( { prefix, sizeof( prefix ) }, 7u ) );

    REQUIRE( HashXXH3_StreamUpdate( &stream, { suffix, sizeof( suffix ) } ) );
    hash64_t completeHash = 0u;
    REQUIRE( HashXXH3_StreamDigest64( &stream, &completeHash ) );
    const byte complete[]{ 1u, 2u, 3u, 4u, 5u, 6u, 7u };
    REQUIRE( completeHash == HashXXH3_64_Data( { complete, sizeof( complete ) }, 7u ) );

    REQUIRE( HashXXH3_StreamReset( &stream, 11u ) );
    REQUIRE( HashXXH3_StreamUpdate( &stream, { complete, sizeof( complete ) } ) );
    hash64_t resetHash = 0u;
    REQUIRE( HashXXH3_StreamDigest64( &stream, &resetHash ) );
    REQUIRE( resetHash == HashXXH3_64_Data( { complete, sizeof( complete ) }, 11u ) );
    REQUIRE( resetHash != completeHash );
}

TEST_CASE( "xxHash ASCII-insensitive helpers normalize case and validate streams",
           "[CypherCommon][Tier1][HashXXH]" )
{
    const string_view_t mixed = StringView_FromCString( "Materials/WALL.CYMAT" );
    const string_view_t lower = StringView_FromCString( "materials/wall.cymat" );
    REQUIRE(
        HashXXH32_StringInsensitiveAscii( mixed ) ==
        HashXXH32_StringInsensitiveAscii( lower ) );
    REQUIRE(
        HashXXH3_64_StringInsensitiveAscii( mixed ) ==
        HashXXH3_64_StringInsensitiveAscii( lower ) );

    hash_xxh3_stream_t stream{};
    REQUIRE_FALSE( HashXXH3_StreamIsValid( &stream ) );
    REQUIRE( HashXXH3_StreamInit(
        &stream,
        hash_xxh3_stream_mode_t::HASH_64,
        0u ) );
    REQUIRE( HashXXH3_StreamIsValid( &stream ) );
}
