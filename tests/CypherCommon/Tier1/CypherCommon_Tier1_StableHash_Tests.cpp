//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_StableHash_Tests.cpp
//  Purpose: Tests the versioned deterministic stable-hash contract.
//  Details: Pins canonical byte order, type and length separation, semantic domains,
//           schema versions, floating-point normalization, and builder state rules.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StableHash.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

hash64_t BuildStableHash(
    stable_hash_domain_t nDomain,
    u32 nSchemaVersion ) noexcept
{
    stable_hash_builder_t builder{};
    hash64_t hash = 0u;
    if ( !StableHash_Begin( &builder, nDomain, nSchemaVersion ) ||
         !StableHash_WriteU16( &builder, 0xABCDu ) ||
         !StableHash_WriteString(
             &builder,
             StringView_FromCString( "cypher" ) ) ||
         !StableHash_WriteF32( &builder, -0.0f ) ||
         !StableHash_End( &builder, &hash ) ) {
        return 0u;
    }
    return hash;
}

} // namespace

TEST_CASE( "StableHash matches its canonical little-endian byte contract",
           "[CypherCommon][Tier1][StableHash]" )
{
    constexpr stable_hash_domain_t nDomain = 0x0102030405060708ull;
    constexpr u32 nSchemaVersion = 3u;
    const hash64_t actual = BuildStableHash( nDomain, nSchemaVersion );

    const byte canonical[]{
        'C', 'Y', 'S', 'H',
        0x01u, 0x00u, 0x00u, 0x00u,
        0x08u, 0x07u, 0x06u, 0x05u, 0x04u, 0x03u, 0x02u, 0x01u,
        0x03u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x03u, 0xCDu, 0xABu,
        0x0Du, 0x06u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        'c', 'y', 'p', 'h', 'e', 'r',
        0x0Au, 0x00u, 0x00u, 0x00u, 0x00u
    };
    REQUIRE( actual == HashXXH3_64_Data( { canonical, sizeof( canonical ) } ) );
}

TEST_CASE( "StableHash separates type, domain, schema, and sequence",
           "[CypherCommon][Tier1][StableHash]" )
{
    stable_hash_builder_t u16Builder{};
    stable_hash_builder_t bytesBuilder{};
    hash64_t u16Hash = 0u;
    hash64_t bytesHash = 0u;
    REQUIRE( StableHash_Begin( &u16Builder, 1u, 1u ) );
    REQUIRE( StableHash_WriteU16( &u16Builder, 0x0201u ) );
    REQUIRE( StableHash_End( &u16Builder, &u16Hash ) );

    const byte bytes[]{ 0x01u, 0x02u };
    REQUIRE( StableHash_Begin( &bytesBuilder, 1u, 1u ) );
    REQUIRE( StableHash_WriteBytes( &bytesBuilder, { bytes, sizeof( bytes ) } ) );
    REQUIRE( StableHash_End( &bytesBuilder, &bytesHash ) );
    REQUIRE( u16Hash != bytesHash );

    REQUIRE( BuildStableHash( 1u, 1u ) != BuildStableHash( 2u, 1u ) );
    REQUIRE( BuildStableHash( 1u, 1u ) != BuildStableHash( 1u, 2u ) );
}

TEST_CASE( "StableHash canonicalizes signed zero and NaN payloads",
           "[CypherCommon][Tier1][StableHash]" )
{
    stable_hash_builder_t first{};
    stable_hash_builder_t second{};
    hash64_t firstHash = 0u;
    hash64_t secondHash = 0u;

    REQUIRE( StableHash_Begin( &first, 9u, 1u ) );
    REQUIRE( StableHash_WriteF32( &first, 0.0f ) );
    REQUIRE( StableHash_WriteF64(
        &first,
        std::bit_cast<f64>( 0x7FF0000000000001ull ) ) );
    REQUIRE( StableHash_End( &first, &firstHash ) );

    REQUIRE( StableHash_Begin( &second, 9u, 1u ) );
    REQUIRE( StableHash_WriteF32( &second, -0.0f ) );
    REQUIRE( StableHash_WriteF64(
        &second,
        std::bit_cast<f64>( 0xFFFABCDE12345678ull ) ) );
    REQUIRE( StableHash_End( &second, &secondHash ) );

    REQUIRE( firstHash == secondHash );
}

TEST_CASE( "StableHash closes after finalization and can be restarted",
           "[CypherCommon][Tier1][StableHash]" )
{
    stable_hash_builder_t builder{};
    hash64_t firstHash = 0u;
    hash64_t secondHash = 0u;

    REQUIRE_FALSE( StableHash_IsActive( &builder ) );
    REQUIRE( StableHash_Begin( &builder, 3u, 4u ) );
    REQUIRE( StableHash_WriteBool( &builder, CY_TRUE ) );
    REQUIRE( StableHash_End( &builder, &firstHash ) );
    REQUIRE_FALSE( StableHash_IsActive( &builder ) );

    REQUIRE( StableHash_Begin( &builder, 3u, 4u ) );
    REQUIRE( StableHash_WriteBool( &builder, CY_TRUE ) );
    REQUIRE( StableHash_End( &builder, &secondHash ) );
    REQUIRE( firstHash == secondHash );
}

TEST_CASE( "StableHash encodes every signed unsigned and hash-width writer",
           "[CypherCommon][Tier1][StableHash]" )
{
    stable_hash_builder_t builder{};
    REQUIRE( StableHash_Begin( &builder, 0x77u, 1u ) );
    REQUIRE( StableHash_WriteU8( &builder, 0x12u ) );
    REQUIRE( StableHash_WriteU32( &builder, 0x12345678u ) );
    REQUIRE( StableHash_WriteU64( &builder, 0x0123456789ABCDEFull ) );
    REQUIRE( StableHash_WriteI8( &builder, -8 ) );
    REQUIRE( StableHash_WriteI16( &builder, -1600 ) );
    REQUIRE( StableHash_WriteI32( &builder, -320000 ) );
    REQUIRE( StableHash_WriteI64( &builder, -64000000 ) );
    REQUIRE( StableHash_WriteHash64( &builder, 0x8877665544332211ull ) );
    REQUIRE( StableHash_WriteHash128(
        &builder,
        { 0x1020304050607080ull, 0x90A0B0C0D0E0F000ull } ) );

    hash64_t first = 0u;
    REQUIRE( StableHash_End( &builder, &first ) );

    stable_hash_builder_t different{};
    REQUIRE( StableHash_Begin( &different, 0x77u, 1u ) );
    REQUIRE( StableHash_WriteU8( &different, 0x13u ) );
    hash64_t second = 0u;
    REQUIRE( StableHash_End( &different, &second ) );
    REQUIRE( first != second );
}
