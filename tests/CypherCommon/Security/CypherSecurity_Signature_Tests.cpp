//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Security/CypherSecurity_Signature_Tests.cpp
//  Purpose: Tests one-shot and multipart Ed25519 signatures.
//  Details: Known vectors, tampering, deterministic keys, canonical imports,
//           stream lifecycle, and concurrent signing are covered.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSecurity.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <thread>
#include <type_traits>

using namespace cypher::common;
using namespace cypher::security;

static_assert( !std::is_copy_constructible_v<signature_keypair_t> );
static_assert( !std::is_move_constructible_v<signature_keypair_t> );
static_assert( !std::is_copy_constructible_v<signature_stream_t> );

TEST_CASE( "CypherSecurity signatures match the RFC 8032 empty-message vector",
           "[CypherSecurity][Signature][Vector]" )
{
    constexpr std::array<byte, CY_SECURITY_SIGN_SEED_SIZE> seed{
        0x9du, 0x61u, 0xb1u, 0x9du, 0xefu, 0xfdu, 0x5au, 0x60u,
        0xbau, 0x84u, 0x4au, 0xf4u, 0x92u, 0xecu, 0x2cu, 0xc4u,
        0x44u, 0x49u, 0xc5u, 0x69u, 0x7bu, 0x32u, 0x69u, 0x19u,
        0x70u, 0x3bu, 0xacu, 0x03u, 0x1cu, 0xaeu, 0x7fu, 0x60u
    };
    constexpr std::array<byte, CY_SECURITY_SIGN_PUBLIC_KEY_SIZE> expectedPublic{
        0xd7u, 0x5au, 0x98u, 0x01u, 0x82u, 0xb1u, 0x0au, 0xb7u,
        0xd5u, 0x4bu, 0xfeu, 0xd3u, 0xc9u, 0x64u, 0x07u, 0x3au,
        0x0eu, 0xe1u, 0x72u, 0xf3u, 0xdau, 0xa6u, 0x23u, 0x25u,
        0xafu, 0x02u, 0x1au, 0x68u, 0xf7u, 0x07u, 0x51u, 0x1au
    };
    constexpr std::array<byte, CY_SECURITY_SIGNATURE_SIZE> expectedSignature{
        0xe5u, 0x56u, 0x43u, 0x00u, 0xc3u, 0x60u, 0xacu, 0x72u,
        0x90u, 0x86u, 0xe2u, 0xccu, 0x80u, 0x6eu, 0x82u, 0x8au,
        0x84u, 0x87u, 0x7fu, 0x1eu, 0xb8u, 0xe5u, 0xd9u, 0x74u,
        0xd8u, 0x73u, 0xe0u, 0x65u, 0x22u, 0x49u, 0x01u, 0x55u,
        0x5fu, 0xb8u, 0x82u, 0x15u, 0x90u, 0xa3u, 0x3bu, 0xacu,
        0xc6u, 0x1eu, 0x39u, 0x70u, 0x1cu, 0xf9u, 0xb4u, 0x6bu,
        0xd2u, 0x5bu, 0xf5u, 0xf0u, 0x59u, 0x5bu, 0xbeu, 0x24u,
        0x65u, 0x51u, 0x41u, 0x43u, 0x8eu, 0x7au, 0x10u, 0x0bu
    };

    signature_keypair_t keyPair{};
    REQUIRE(
        SignatureKeyPair_FromSeed(
            BinaryBlock_FromData( seed.data(), seed.size() ),
            secure_memory_lock_policy_t::BEST_EFFORT,
            &keyPair ) == security_status_t::OK );
    REQUIRE(
        Security_ConstantTimeEquals(
            keyPair.publicKey.bytes,
            expectedPublic.data(),
            expectedPublic.size() ) );

    signature_t signature{};
    REQUIRE(
        Signature_Sign( {}, &keyPair, &signature ) ==
        security_status_t::OK );
    REQUIRE(
        Security_ConstantTimeEquals(
            signature.bytes,
            expectedSignature.data(),
            expectedSignature.size() ) );
    REQUIRE(
        Signature_Verify( {}, signature, keyPair.publicKey ) ==
        security_status_t::OK );

    std::array<byte, CY_SECURITY_SIGN_SECRET_KEY_SIZE> canonicalSecret{};
    for ( usize iByte = 0u; iByte < seed.size(); ++iByte ) {
        canonicalSecret[iByte] = seed[iByte];
        canonicalSecret[seed.size() + iByte] = expectedPublic[iByte];
    }
    signature_keypair_t imported{};
    REQUIRE(
        SignatureKeyPair_ImportSecret(
            BinaryBlock_FromData(
                canonicalSecret.data(),
                canonicalSecret.size() ),
            secure_memory_lock_policy_t::BEST_EFFORT,
            &imported ) == security_status_t::OK );

    canonicalSecret.back() ^= 0x01u;
    signature_keypair_t inconsistent{};
    REQUIRE(
        SignatureKeyPair_ImportSecret(
            BinaryBlock_FromData(
                canonicalSecret.data(),
                canonicalSecret.size() ),
            secure_memory_lock_policy_t::BEST_EFFORT,
            &inconsistent ) == security_status_t::INVALID_ARGUMENT );
    REQUIRE_FALSE( SignatureKeyPair_IsValid( &inconsistent ) );
}

TEST_CASE( "CypherSecurity signatures reject modified content and wrong keys",
           "[CypherSecurity][Signature][Authentication]" )
{
    signature_keypair_t signer{};
    signature_keypair_t other{};
    REQUIRE(
        SignatureKeyPair_Generate(
            secure_memory_lock_policy_t::BEST_EFFORT,
            &signer ) == security_status_t::OK );
    REQUIRE(
        SignatureKeyPair_Generate(
            secure_memory_lock_policy_t::BEST_EFFORT,
            &other ) == security_status_t::OK );

    std::array<byte, 12u> message{
        'C', 'y', 'p', 'h', 'e', 'r', 'P', 'a', 'c', 'k', 'a', 'g'
    };
    signature_t signature{};
    REQUIRE(
        Signature_Sign(
            BinaryBlock_FromData( message.data(), message.size() ),
            &signer,
            &signature ) == security_status_t::OK );

    message[0] ^= 0x01u;
    REQUIRE(
        Signature_Verify(
            BinaryBlock_FromData( message.data(), message.size() ),
            signature,
            signer.publicKey ) == security_status_t::AUTHENTICATION_FAILED );
    message[0] ^= 0x01u;
    REQUIRE(
        Signature_Verify(
            BinaryBlock_FromData( message.data(), message.size() ),
            signature,
            other.publicKey ) == security_status_t::AUTHENTICATION_FAILED );

    signature_t modified = signature;
    modified.bytes[7] ^= 0x80u;
    REQUIRE_FALSE( Signature_Equals( signature, modified ) );
    REQUIRE(
        Signature_Verify(
            BinaryBlock_FromData( message.data(), message.size() ),
            modified,
            signer.publicKey ) == security_status_t::AUTHENTICATION_FAILED );
}

TEST_CASE( "CypherSecurity multipart signatures enforce stream lifecycle",
           "[CypherSecurity][Signature][Stream]" )
{
    signature_keypair_t keyPair{};
    REQUIRE(
        SignatureKeyPair_Generate(
            secure_memory_lock_policy_t::BEST_EFFORT,
            &keyPair ) == security_status_t::OK );
    constexpr std::array<byte, 5u> first{ 'f', 'i', 'r', 's', 't' };
    constexpr std::array<byte, 6u> second{ 's', 'e', 'c', 'o', 'n', 'd' };

    signature_stream_t signer{};
    REQUIRE( SignatureStream_Begin( &signer ) == security_status_t::OK );
    REQUIRE(
        SignatureStream_Update(
            &signer,
            BinaryBlock_FromData( first.data(), first.size() ) ) ==
        security_status_t::OK );
    REQUIRE(
        SignatureStream_Update( &signer, {} ) == security_status_t::OK );
    REQUIRE(
        SignatureStream_Update(
            &signer,
            BinaryBlock_FromData( second.data(), second.size() ) ) ==
        security_status_t::OK );

    signature_t signature{};
    REQUIRE(
        SignatureStream_EndSign( &signer, &keyPair, &signature ) ==
        security_status_t::OK );
    REQUIRE_FALSE( signer.bActive );
    REQUIRE(
        SignatureStream_Update( &signer, {} ) ==
        security_status_t::INVALID_STATE );

    signature_stream_t verifier{};
    REQUIRE( SignatureStream_Begin( &verifier ) == security_status_t::OK );
    REQUIRE(
        SignatureStream_Update(
            &verifier,
            BinaryBlock_FromData( first.data(), first.size() ) ) ==
        security_status_t::OK );
    REQUIRE(
        SignatureStream_Update(
            &verifier,
            BinaryBlock_FromData( second.data(), second.size() ) ) ==
        security_status_t::OK );
    REQUIRE(
        SignatureStream_EndVerify(
            &verifier,
            signature,
            keyPair.publicKey ) == security_status_t::OK );
    REQUIRE_FALSE( verifier.bActive );

    REQUIRE( SignatureStream_Begin( &verifier ) == security_status_t::OK );
    REQUIRE(
        SignatureStream_Update(
            &verifier,
            BinaryBlock_FromData( second.data(), second.size() ) ) ==
        security_status_t::OK );
    REQUIRE(
        SignatureStream_EndVerify(
            &verifier,
            signature,
            keyPair.publicKey ) == security_status_t::AUTHENTICATION_FAILED );
    REQUIRE_FALSE( verifier.bActive );
}

TEST_CASE( "CypherSecurity signing supports concurrent read-only key use",
           "[CypherSecurity][Signature][Thread]" )
{
    signature_keypair_t keyPair{};
    REQUIRE(
        SignatureKeyPair_Generate(
            secure_memory_lock_policy_t::BEST_EFFORT,
            &keyPair ) == security_status_t::OK );

    constexpr usize cThreads = 8u;
    std::array<std::thread, cThreads> threads{};
    std::atomic<bool> bAllSucceeded{ true };
    for ( usize iThread = 0u; iThread < cThreads; ++iThread ) {
        threads[iThread] = std::thread( [&keyPair, iThread, &bAllSucceeded] {
            const u64 nMessage = static_cast<u64>( iThread );
            const binary_block_t message =
                BinaryBlock_FromData( &nMessage, sizeof( nMessage ) );
            signature_t signature{};
            if ( Signature_Sign( message, &keyPair, &signature ) !=
                     security_status_t::OK ||
                 Signature_Verify(
                     message,
                     signature,
                     keyPair.publicKey ) != security_status_t::OK ) {
                bAllSucceeded.store( false, std::memory_order_relaxed );
            }
        } );
    }
    for ( std::thread &thread : threads ) {
        thread.join();
    }
    REQUIRE( bAllSucceeded.load( std::memory_order_relaxed ) );
}
