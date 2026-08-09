//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Security/CypherSecurity_Hash_Tests.cpp
//  Purpose: Tests CypherSecurity digest and keyed short-hash contracts.
//  Details: Reference vectors, streaming boundaries, secure state cleanup, and
//           malformed input behavior protect the cryptographic wrapper boundary.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSecurity.h"
#include "CypherCommon_Assert.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;
using namespace cypher::security;

namespace
{

u32 g_securityAssertCount = 0u;

assert_action_t CaptureSecurityAssert( const assert_info_t & ) noexcept
{
    ++g_securityAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "CypherSecurity initializes its cryptographic backend",
           "[CypherSecurity][Lifecycle]" )
{
    REQUIRE( Security_Init() == security_status_t::OK );
    REQUIRE( Security_IsReady() );
}

TEST_CASE( "BLAKE2b-256 digest matches its empty-input reference vector",
           "[CypherSecurity][Hash]" )
{
    constexpr byte expected[]{
        0x0Eu, 0x57u, 0x51u, 0xC0u, 0x26u, 0xE5u, 0x43u, 0xB2u,
        0xE8u, 0xABu, 0x2Eu, 0xB0u, 0x60u, 0x99u, 0xDAu, 0xA1u,
        0xD1u, 0xE5u, 0xDFu, 0x47u, 0x77u, 0x8Fu, 0x77u, 0x87u,
        0xFAu, 0xABu, 0x45u, 0xCDu, 0xF1u, 0x2Fu, 0xE3u, 0xA8u
    };

    security_digest_t digest{};
    REQUIRE(
        SecurityDigest_Data(
            {},
            {},
            CY_SECURITY_DIGEST_DEFAULT_SIZE,
            &digest ) == security_status_t::OK );
    REQUIRE( digest.cbSize == sizeof( expected ) );
    REQUIRE( Security_ConstantTimeEquals(
        digest.bytes,
        expected,
        sizeof( expected ) ) );
}

TEST_CASE( "BLAKE2b streaming equals one-shot hashing and clears state",
           "[CypherSecurity][Hash]" )
{
    byte bytes[4096]{};
    byte keyBytes[32]{};
    for ( usize iByte = 0u; iByte < sizeof( bytes ); ++iByte ) {
        bytes[iByte] = static_cast<byte>( iByte * 29u + 7u );
    }
    for ( usize iByte = 0u; iByte < sizeof( keyBytes ); ++iByte ) {
        keyBytes[iByte] = static_cast<byte>( iByte * 11u + 3u );
    }

    const binary_block_t data{ bytes, sizeof( bytes ) };
    const binary_block_t key{ keyBytes, sizeof( keyBytes ) };
    security_digest_t oneShot{};
    REQUIRE(
        SecurityDigest_Data( data, key, 48u, &oneShot ) ==
        security_status_t::OK );

    security_digest_stream_t stream{};
    REQUIRE(
        SecurityDigest_Begin( &stream, key, 48u ) == security_status_t::OK );
    REQUIRE(
        SecurityDigest_Begin( &stream, key, 48u ) ==
        security_status_t::INVALID_STATE );
    REQUIRE(
        SecurityDigest_Update( &stream, { bytes, 17u } ) ==
        security_status_t::OK );
    REQUIRE(
        SecurityDigest_Update( &stream, { bytes + 17u, 997u } ) ==
        security_status_t::OK );
    REQUIRE(
        SecurityDigest_Update(
            &stream,
            { bytes + 1014u, sizeof( bytes ) - 1014u } ) ==
        security_status_t::OK );

    security_digest_t streamed{};
    REQUIRE(
        SecurityDigest_End( &stream, &streamed ) == security_status_t::OK );
    REQUIRE( SecurityDigest_Equals( oneShot, streamed ) );
    REQUIRE_FALSE( stream.bActive );
    for ( byte value : stream.storage ) {
        REQUIRE( value == 0u );
    }

    REQUIRE(
        SecurityDigest_Begin( &stream, key ) == security_status_t::OK );
    REQUIRE(
        SecurityDigest_Update( &stream, data ) == security_status_t::OK );
    SecurityDigest_Cancel( &stream );
    REQUIRE_FALSE( stream.bActive );
    REQUIRE( stream.cbDigest == 0u );
    for ( byte value : stream.storage ) {
        REQUIRE( value == 0u );
    }
    REQUIRE(
        SecurityDigest_Update( &stream, {} ) ==
        security_status_t::INVALID_STATE );
    REQUIRE(
        SecurityDigest_End( &stream, &streamed ) ==
        security_status_t::INVALID_STATE );
}

TEST_CASE( "SipHash matches the reference vector and is key dependent",
           "[CypherSecurity][Hash]" )
{
    security_short_hash_key_t referenceKey{};
    for ( usize iByte = 0u; iByte < sizeof( referenceKey.bytes ); ++iByte ) {
        referenceKey.bytes[iByte] = static_cast<byte>( iByte );
    }

    security_short_hash_t referenceHash{};
    REQUIRE(
        SecurityShortHash_Data( {}, referenceKey, &referenceHash ) ==
        security_status_t::OK );
    REQUIRE(
        SecurityShortHash_ToU64( referenceHash ) ==
        0x726FDB47DD0E0E31ull );

    constexpr byte dataBytes[]{ 'c', 'y', 'p', 'h', 'e', 'r' };
    const binary_block_t data{ dataBytes, sizeof( dataBytes ) };
    security_short_hash_key_t randomKey{};
    REQUIRE(
        SecurityShortHash_GenerateKey( &randomKey ) == security_status_t::OK );

    security_short_hash_t first{};
    security_short_hash_t second{};
    REQUIRE(
        SecurityShortHash_Data( data, randomKey, &first ) ==
        security_status_t::OK );
    REQUIRE(
        SecurityShortHash_Data( data, randomKey, &second ) ==
        security_status_t::OK );
    REQUIRE( SecurityShortHash_Equals( first, second ) );
}

TEST_CASE( "Security helpers reject invalid ranges and clear sensitive bytes",
           "[CypherSecurity][Contract]" )
{
    byte sensitive[]{ 1u, 2u, 3u, 4u };
    Security_ZeroMemory( sensitive, sizeof( sensitive ) );
    for ( byte value : sensitive ) {
        REQUIRE( value == 0u );
    }

    constexpr byte one[]{ 1u };
    constexpr byte same[]{ 1u };
    constexpr byte different[]{ 2u };
    REQUIRE( Security_ConstantTimeEquals( one, same, sizeof( one ) ) );
    REQUIRE_FALSE(
        Security_ConstantTimeEquals( one, different, sizeof( one ) ) );

    g_securityAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureSecurityAssert );

    security_digest_t digest{};
    byte shortKey[8]{};
    REQUIRE(
        SecurityDigest_Data(
            {},
            { shortKey, sizeof( shortKey ) },
            CY_SECURITY_DIGEST_DEFAULT_SIZE,
            &digest ) == security_status_t::INVALID_ARGUMENT );
    REQUIRE_FALSE( Security_ConstantTimeEquals( nullptr, one, 1u ) );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_securityAssertCount ==
        2u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
