//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Security/CypherSecurity_AEAD_Tests.cpp
//  Purpose: Tests authenticated encryption and nonce sequencing.
//  Details: Round trips, empty payloads, tampering, capacity failures, counter
//           exhaustion, lifecycle rules, and concurrent key use are covered.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSecurity.h"
#include "CypherCommon_Assert.h"
#include "CypherCommon_Endian.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <thread>
#include <type_traits>

using namespace cypher::common;
using namespace cypher::security;

namespace
{

assert_action_t CaptureAeadAssert( const assert_info_t & ) noexcept
{
    return assert_action_t::Continue;
}

} // namespace

static_assert( !std::is_copy_constructible_v<aead_key_t> );
static_assert( !std::is_move_constructible_v<aead_key_t> );

TEST_CASE( "CypherSecurity AEAD encrypts and authenticates a message",
           "[CypherSecurity][AEAD]" )
{
    std::array<byte, CY_SECURITY_AEAD_KEY_SIZE> keyBytes{};
    std::array<byte, CY_SECURITY_AEAD_NONCE_PREFIX_SIZE> noncePrefix{};
    for ( usize iByte = 0u; iByte < keyBytes.size(); ++iByte ) {
        keyBytes[iByte] = static_cast<byte>( iByte + 1u );
    }
    for ( usize iByte = 0u; iByte < noncePrefix.size(); ++iByte ) {
        noncePrefix[iByte] = static_cast<byte>( 0x80u + iByte );
    }

    aead_key_t key{};
    REQUIRE(
        AeadKey_Import(
            BinaryBlock_FromData( keyBytes.data(), keyBytes.size() ),
            secure_memory_lock_policy_t::BEST_EFFORT,
            &key ) == security_status_t::OK );

    aead_nonce_sequence_t sequence{};
    REQUIRE(
        AeadNonceSequence_InitFromPrefix(
            BinaryBlock_FromData( noncePrefix.data(), noncePrefix.size() ),
            5u,
            &sequence ) == security_status_t::OK );
    aead_nonce_t nonce{};
    REQUIRE(
        AeadNonceSequence_Next( &sequence, &nonce ) ==
        security_status_t::OK );

    constexpr std::array<byte, 19u> plaintext{
        'C', 'y', 'p', 'h', 'e', 'r', ' ', 's', 'e', 'c',
        'u', 'r', 'e', ' ', 'd', 'a', 't', 'a', '.'
    };
    constexpr std::array<byte, 8u> authenticatedData{
        'p', 'a', 'c', 'k', 'e', 't', '-', '1'
    };
    std::array<byte, plaintext.size() + CY_SECURITY_AEAD_TAG_SIZE> ciphertext{};
    usize cbCiphertext = 0u;
    REQUIRE(
        Aead_Encrypt(
            BinaryBlock_FromData( plaintext.data(), plaintext.size() ),
            BinaryBlock_FromData(
                authenticatedData.data(),
                authenticatedData.size() ),
            nonce,
            &key,
            ciphertext.data(),
            ciphertext.size(),
            &cbCiphertext ) == security_status_t::OK );
    REQUIRE( cbCiphertext == ciphertext.size() );

    std::array<byte, plaintext.size()> decrypted{};
    usize cbDecrypted = 0u;
    REQUIRE(
        Aead_Decrypt(
            BinaryBlock_FromData( ciphertext.data(), cbCiphertext ),
            BinaryBlock_FromData(
                authenticatedData.data(),
                authenticatedData.size() ),
            nonce,
            &key,
            decrypted.data(),
            decrypted.size(),
            &cbDecrypted ) == security_status_t::OK );
    REQUIRE( cbDecrypted == plaintext.size() );
    REQUIRE( decrypted == plaintext );
}

TEST_CASE( "CypherSecurity AEAD rejects modified ciphertext and metadata",
           "[CypherSecurity][AEAD][Authentication]" )
{
    aead_key_t key{};
    REQUIRE(
        AeadKey_Generate(
            secure_memory_lock_policy_t::BEST_EFFORT,
            &key ) == security_status_t::OK );
    aead_nonce_t nonce{};
    REQUIRE( AeadNonce_Generate( &nonce ) == security_status_t::OK );

    constexpr std::array<byte, 8u> plaintext{ 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };
    std::array<byte, 4u> authenticatedData{ 9u, 10u, 11u, 12u };
    std::array<byte, plaintext.size() + CY_SECURITY_AEAD_TAG_SIZE> ciphertext{};
    usize cbCiphertext = 0u;
    REQUIRE(
        Aead_Encrypt(
            BinaryBlock_FromData( plaintext.data(), plaintext.size() ),
            BinaryBlock_FromData(
                authenticatedData.data(),
                authenticatedData.size() ),
            nonce,
            &key,
            ciphertext.data(),
            ciphertext.size(),
            &cbCiphertext ) == security_status_t::OK );

    ciphertext[0] ^= 0x01u;
    std::array<byte, plaintext.size()> output{};
    output.fill( 0xA5u );
    usize cbOutput = 0xFFFFu;
    REQUIRE(
        Aead_Decrypt(
            BinaryBlock_FromData( ciphertext.data(), cbCiphertext ),
            BinaryBlock_FromData(
                authenticatedData.data(),
                authenticatedData.size() ),
            nonce,
            &key,
            output.data(),
            output.size(),
            &cbOutput ) == security_status_t::AUTHENTICATION_FAILED );
    REQUIRE( cbOutput == 0xFFFFu );
    for ( const byte value : output ) {
        REQUIRE( value == 0u );
    }

    ciphertext[0] ^= 0x01u;
    authenticatedData[0] ^= 0x01u;
    REQUIRE(
        Aead_Decrypt(
            BinaryBlock_FromData( ciphertext.data(), cbCiphertext ),
            BinaryBlock_FromData(
                authenticatedData.data(),
                authenticatedData.size() ),
            nonce,
            &key,
            output.data(),
            output.size(),
            &cbOutput ) == security_status_t::AUTHENTICATION_FAILED );
}

TEST_CASE( "CypherSecurity AEAD supports an authenticated empty payload",
           "[CypherSecurity][AEAD][Empty]" )
{
    aead_key_t key{};
    aead_nonce_t nonce{};
    REQUIRE(
        AeadKey_Generate(
            secure_memory_lock_policy_t::BEST_EFFORT,
            &key ) == security_status_t::OK );
    REQUIRE( AeadNonce_Generate( &nonce ) == security_status_t::OK );

    std::array<byte, CY_SECURITY_AEAD_TAG_SIZE> ciphertext{};
    usize cbCiphertext = 0u;
    REQUIRE(
        Aead_Encrypt(
            {},
            {},
            nonce,
            &key,
            ciphertext.data(),
            ciphertext.size(),
            &cbCiphertext ) == security_status_t::OK );
    REQUIRE( cbCiphertext == CY_SECURITY_AEAD_TAG_SIZE );

    usize cbPlaintext = 123u;
    REQUIRE(
        Aead_Decrypt(
            BinaryBlock_FromData( ciphertext.data(), ciphertext.size() ),
            {},
            nonce,
            &key,
            nullptr,
            0u,
            &cbPlaintext ) == security_status_t::OK );
    REQUIRE( cbPlaintext == 0u );
}

TEST_CASE( "CypherSecurity AEAD nonce sequences are unique and exhaust safely",
           "[CypherSecurity][AEAD][Nonce]" )
{
    std::array<byte, CY_SECURITY_AEAD_NONCE_PREFIX_SIZE> prefix{};
    for ( usize iByte = 0u; iByte < prefix.size(); ++iByte ) {
        prefix[iByte] = static_cast<byte>( iByte );
    }

    aead_nonce_sequence_t sequence{};
    REQUIRE(
        AeadNonceSequence_InitFromPrefix(
            BinaryBlock_FromData( prefix.data(), prefix.size() ),
            42u,
            &sequence ) == security_status_t::OK );
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureAeadAssert );
    const security_status_t activeInitResult =
        AeadNonceSequence_Init( 0u, &sequence );
    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE( activeInitResult == security_status_t::INVALID_STATE );

    aead_nonce_t first{};
    aead_nonce_t second{};
    REQUIRE(
        AeadNonceSequence_Next( &sequence, &first ) ==
        security_status_t::OK );
    REQUIRE(
        AeadNonceSequence_Next( &sequence, &second ) ==
        security_status_t::OK );
    REQUIRE( Cy_ReadLittle64( first.bytes + prefix.size() ) == 42u );
    REQUIRE( Cy_ReadLittle64( second.bytes + prefix.size() ) == 43u );
    REQUIRE_FALSE(
        Security_ConstantTimeEquals(
            first.bytes,
            second.bytes,
            sizeof( first.bytes ) ) );

    AeadNonceSequence_Reset( &sequence );
    REQUIRE(
        AeadNonceSequence_InitFromPrefix(
            BinaryBlock_FromData( prefix.data(), prefix.size() ),
            CY_U64_MAX,
            &sequence ) == security_status_t::OK );
    REQUIRE(
        AeadNonceSequence_Next( &sequence, &first ) ==
        security_status_t::OK );
    REQUIRE(
        AeadNonceSequence_Next( &sequence, &second ) ==
        security_status_t::COUNTER_EXHAUSTED );
}

TEST_CASE( "CypherSecurity AEAD reports output capacity before encryption",
           "[CypherSecurity][AEAD][Capacity]" )
{
    aead_key_t key{};
    aead_nonce_t nonce{};
    REQUIRE(
        AeadKey_Generate(
            secure_memory_lock_policy_t::BEST_EFFORT,
            &key ) == security_status_t::OK );
    REQUIRE( AeadNonce_Generate( &nonce ) == security_status_t::OK );

    constexpr std::array<byte, 16u> plaintext{};
    std::array<byte, plaintext.size() + CY_SECURITY_AEAD_TAG_SIZE> ciphertext{};
    usize cbOutput = 77u;
    REQUIRE(
        Aead_Encrypt(
            BinaryBlock_FromData( plaintext.data(), plaintext.size() ),
            {},
            nonce,
            &key,
            ciphertext.data(),
            ciphertext.size() - 1u,
            &cbOutput ) == security_status_t::BUFFER_TOO_SMALL );
    REQUIRE( cbOutput == 77u );
}

TEST_CASE( "CypherSecurity AEAD permits concurrent use of one read-only key",
           "[CypherSecurity][AEAD][Thread]" )
{
    aead_key_t key{};
    REQUIRE(
        AeadKey_Generate(
            secure_memory_lock_policy_t::BEST_EFFORT,
            &key ) == security_status_t::OK );

    constexpr usize cThreads = 8u;
    std::array<std::thread, cThreads> threads{};
    std::atomic<bool> bAllSucceeded{ true };
    for ( usize iThread = 0u; iThread < cThreads; ++iThread ) {
        threads[iThread] = std::thread( [&key, &bAllSucceeded] {
            constexpr std::array<byte, 32u> plaintext{};
            std::array<byte, plaintext.size() + CY_SECURITY_AEAD_TAG_SIZE> ciphertext{};
            aead_nonce_t nonce{};
            usize cbCiphertext = 0u;
            if ( AeadNonce_Generate( &nonce ) != security_status_t::OK ||
                 Aead_Encrypt(
                     BinaryBlock_FromData( plaintext.data(), plaintext.size() ),
                     {},
                     nonce,
                     &key,
                     ciphertext.data(),
                     ciphertext.size(),
                     &cbCiphertext ) != security_status_t::OK ) {
                bAllSucceeded.store( false, std::memory_order_relaxed );
            }
        } );
    }
    for ( std::thread &thread : threads ) {
        thread.join();
    }
    REQUIRE( bAllSucceeded.load( std::memory_order_relaxed ) );
}
