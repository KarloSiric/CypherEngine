//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Security/CypherSecurity_SecretStream_Tests.cpp
//  Purpose: Tests authenticated streaming encryption and state policy.
//  Details: Ordered records, tags, rekeying, finalization, tampering, metadata,
//           capacity failures, and empty records are covered.
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
#include <type_traits>

using namespace cypher::common;
using namespace cypher::security;

static_assert( !std::is_copy_constructible_v<secret_stream_key_t> );
static_assert( !std::is_copy_constructible_v<secret_stream_push_t> );
static_assert( !std::is_copy_constructible_v<secret_stream_pull_t> );

TEST_CASE( "CypherSecurity secret streams preserve ordered records and tags",
           "[CypherSecurity][SecretStream]" )
{
    secret_stream_key_t key{};
    REQUIRE(
        SecretStreamKey_Generate(
            secure_memory_lock_policy_t::BEST_EFFORT,
            &key ) == security_status_t::OK );

    constexpr std::array<byte, 5u> first{ 'f', 'i', 'r', 's', 't' };
    constexpr std::array<byte, 6u> second{ 's', 'e', 'c', 'o', 'n', 'd' };
    constexpr std::array<byte, 5u> metadata{ 'f', 'i', 'l', 'e', '1' };
    std::array<byte, first.size() + CY_SECURITY_SECRET_STREAM_OVERHEAD_SIZE>
        firstCiphertext{};
    std::array<byte, second.size() + CY_SECURITY_SECRET_STREAM_OVERHEAD_SIZE>
        secondCiphertext{};
    std::array<byte, CY_SECURITY_SECRET_STREAM_OVERHEAD_SIZE> finalCiphertext{};

    secret_stream_push_t push{};
    secret_stream_header_t header{};
    REQUIRE(
        SecretStreamPush_Begin( &key, &push, &header ) ==
        security_status_t::OK );
    usize cbFirst = 0u;
    REQUIRE(
        SecretStreamPush_Message(
            &push,
            BinaryBlock_FromData( first.data(), first.size() ),
            BinaryBlock_FromData( metadata.data(), metadata.size() ),
            secret_stream_tag_t::MESSAGE,
            firstCiphertext.data(),
            firstCiphertext.size(),
            &cbFirst ) == security_status_t::OK );
    REQUIRE( cbFirst == firstCiphertext.size() );

    usize cbSecond = 0u;
    REQUIRE(
        SecretStreamPush_Message(
            &push,
            BinaryBlock_FromData( second.data(), second.size() ),
            {},
            secret_stream_tag_t::REKEY,
            secondCiphertext.data(),
            secondCiphertext.size(),
            &cbSecond ) == security_status_t::OK );
    REQUIRE( cbSecond == secondCiphertext.size() );

    usize cbFinal = 0u;
    REQUIRE(
        SecretStreamPush_Message(
            &push,
            {},
            {},
            secret_stream_tag_t::FINAL,
            finalCiphertext.data(),
            finalCiphertext.size(),
            &cbFinal ) == security_status_t::OK );
    REQUIRE( cbFinal == finalCiphertext.size() );
    REQUIRE_FALSE( push.bActive );

    secret_stream_pull_t pull{};
    REQUIRE(
        SecretStreamPull_Begin( &key, header, &pull ) ==
        security_status_t::OK );
    std::array<byte, first.size()> firstPlaintext{};
    usize cbPlaintext = 0u;
    secret_stream_tag_t tag = secret_stream_tag_t::FINAL;
    REQUIRE(
        SecretStreamPull_Message(
            &pull,
            BinaryBlock_FromData( firstCiphertext.data(), cbFirst ),
            BinaryBlock_FromData( metadata.data(), metadata.size() ),
            firstPlaintext.data(),
            firstPlaintext.size(),
            &cbPlaintext,
            &tag ) == security_status_t::OK );
    REQUIRE( firstPlaintext == first );
    REQUIRE( cbPlaintext == first.size() );
    REQUIRE( tag == secret_stream_tag_t::MESSAGE );

    std::array<byte, second.size()> secondPlaintext{};
    REQUIRE(
        SecretStreamPull_Message(
            &pull,
            BinaryBlock_FromData( secondCiphertext.data(), cbSecond ),
            {},
            secondPlaintext.data(),
            secondPlaintext.size(),
            &cbPlaintext,
            &tag ) == security_status_t::OK );
    REQUIRE( secondPlaintext == second );
    REQUIRE( tag == secret_stream_tag_t::REKEY );

    REQUIRE(
        SecretStreamPull_Message(
            &pull,
            BinaryBlock_FromData( finalCiphertext.data(), cbFinal ),
            {},
            nullptr,
            0u,
            &cbPlaintext,
            &tag ) == security_status_t::OK );
    REQUIRE( cbPlaintext == 0u );
    REQUIRE( tag == secret_stream_tag_t::FINAL );
    REQUIRE_FALSE( pull.bActive );
}

TEST_CASE( "CypherSecurity secret streams reject tampering and close pull state",
           "[CypherSecurity][SecretStream][Authentication]" )
{
    secret_stream_key_t key{};
    REQUIRE(
        SecretStreamKey_Generate(
            secure_memory_lock_policy_t::BEST_EFFORT,
            &key ) == security_status_t::OK );
    constexpr std::array<byte, 8u> plaintext{
        't', 'a', 'm', 'p', 'e', 'r', 'e', 'd'
    };
    constexpr std::array<byte, 2u> metadata{ 4u, 2u };

    secret_stream_push_t push{};
    secret_stream_header_t header{};
    REQUIRE(
        SecretStreamPush_Begin( &key, &push, &header ) ==
        security_status_t::OK );
    std::array<byte, plaintext.size() + CY_SECURITY_SECRET_STREAM_OVERHEAD_SIZE>
        ciphertext{};
    usize cbCiphertext = 0u;
    REQUIRE(
        SecretStreamPush_Message(
            &push,
            BinaryBlock_FromData( plaintext.data(), plaintext.size() ),
            BinaryBlock_FromData( metadata.data(), metadata.size() ),
            secret_stream_tag_t::FINAL,
            ciphertext.data(),
            ciphertext.size(),
            &cbCiphertext ) == security_status_t::OK );

    ciphertext[3] ^= 0x40u;
    secret_stream_pull_t pull{};
    REQUIRE(
        SecretStreamPull_Begin( &key, header, &pull ) ==
        security_status_t::OK );
    std::array<byte, plaintext.size()> output{};
    output.fill( 0xA5u );
    usize cbOutput = 77u;
    secret_stream_tag_t tag = secret_stream_tag_t::MESSAGE;
    REQUIRE(
        SecretStreamPull_Message(
            &pull,
            BinaryBlock_FromData( ciphertext.data(), cbCiphertext ),
            BinaryBlock_FromData( metadata.data(), metadata.size() ),
            output.data(),
            output.size(),
            &cbOutput,
            &tag ) == security_status_t::AUTHENTICATION_FAILED );
    REQUIRE_FALSE( pull.bActive );
    for ( const byte value : output ) {
        REQUIRE( value == 0u );
    }
}

TEST_CASE( "CypherSecurity secret streams reject wrong metadata and ordering",
           "[CypherSecurity][SecretStream][Order]" )
{
    secret_stream_key_t key{};
    REQUIRE(
        SecretStreamKey_Generate(
            secure_memory_lock_policy_t::BEST_EFFORT,
            &key ) == security_status_t::OK );
    constexpr std::array<byte, 3u> first{ 1u, 2u, 3u };
    constexpr std::array<byte, 3u> second{ 4u, 5u, 6u };
    constexpr std::array<byte, 1u> metadata{ 9u };
    std::array<byte, first.size() + CY_SECURITY_SECRET_STREAM_OVERHEAD_SIZE> c1{};
    std::array<byte, second.size() + CY_SECURITY_SECRET_STREAM_OVERHEAD_SIZE> c2{};

    secret_stream_push_t push{};
    secret_stream_header_t header{};
    REQUIRE(
        SecretStreamPush_Begin( &key, &push, &header ) ==
        security_status_t::OK );
    usize cbC1 = 0u;
    usize cbC2 = 0u;
    REQUIRE(
        SecretStreamPush_Message(
            &push,
            BinaryBlock_FromData( first.data(), first.size() ),
            BinaryBlock_FromData( metadata.data(), metadata.size() ),
            secret_stream_tag_t::MESSAGE,
            c1.data(), c1.size(), &cbC1 ) == security_status_t::OK );
    REQUIRE(
        SecretStreamPush_Message(
            &push,
            BinaryBlock_FromData( second.data(), second.size() ),
            {},
            secret_stream_tag_t::FINAL,
            c2.data(), c2.size(), &cbC2 ) == security_status_t::OK );

    secret_stream_pull_t pull{};
    REQUIRE(
        SecretStreamPull_Begin( &key, header, &pull ) ==
        security_status_t::OK );
    std::array<byte, second.size()> output{};
    usize cbOutput = 0u;
    secret_stream_tag_t tag{};
    REQUIRE(
        SecretStreamPull_Message(
            &pull,
            BinaryBlock_FromData( c2.data(), cbC2 ),
            {},
            output.data(), output.size(), &cbOutput, &tag ) ==
        security_status_t::AUTHENTICATION_FAILED );

    REQUIRE(
        SecretStreamPull_Begin( &key, header, &pull ) ==
        security_status_t::OK );
    std::array<byte, first.size()> firstOutput{};
    std::array<byte, metadata.size()> wrongMetadata{ 8u };
    REQUIRE(
        SecretStreamPull_Message(
            &pull,
            BinaryBlock_FromData( c1.data(), cbC1 ),
            BinaryBlock_FromData(
                wrongMetadata.data(),
                wrongMetadata.size() ),
            firstOutput.data(),
            firstOutput.size(),
            &cbOutput,
            &tag ) == security_status_t::AUTHENTICATION_FAILED );
}

TEST_CASE( "CypherSecurity stream capacity failures do not consume a record",
           "[CypherSecurity][SecretStream][Capacity]" )
{
    secret_stream_key_t key{};
    REQUIRE(
        SecretStreamKey_Generate(
            secure_memory_lock_policy_t::BEST_EFFORT,
            &key ) == security_status_t::OK );
    constexpr std::array<byte, 4u> plaintext{ 7u, 8u, 9u, 10u };
    std::array<byte, plaintext.size() + CY_SECURITY_SECRET_STREAM_OVERHEAD_SIZE>
        ciphertext{};

    secret_stream_push_t push{};
    secret_stream_header_t header{};
    REQUIRE(
        SecretStreamPush_Begin( &key, &push, &header ) ==
        security_status_t::OK );
    usize cbCiphertext = 99u;
    REQUIRE(
        SecretStreamPush_Message(
            &push,
            BinaryBlock_FromData( plaintext.data(), plaintext.size() ),
            {},
            secret_stream_tag_t::FINAL,
            ciphertext.data(),
            ciphertext.size() - 1u,
            &cbCiphertext ) == security_status_t::BUFFER_TOO_SMALL );
    REQUIRE( push.bActive );
    REQUIRE( cbCiphertext == 99u );
    REQUIRE(
        SecretStreamPush_Message(
            &push,
            BinaryBlock_FromData( plaintext.data(), plaintext.size() ),
            {},
            secret_stream_tag_t::FINAL,
            ciphertext.data(),
            ciphertext.size(),
            &cbCiphertext ) == security_status_t::OK );

    secret_stream_pull_t pull{};
    REQUIRE(
        SecretStreamPull_Begin( &key, header, &pull ) ==
        security_status_t::OK );
    std::array<byte, plaintext.size()> output{};
    usize cbOutput = 88u;
    secret_stream_tag_t tag{};
    REQUIRE(
        SecretStreamPull_Message(
            &pull,
            BinaryBlock_FromData( ciphertext.data(), cbCiphertext ),
            {},
            output.data(),
            output.size() - 1u,
            &cbOutput,
            &tag ) == security_status_t::BUFFER_TOO_SMALL );
    REQUIRE( pull.bActive );
    REQUIRE( cbOutput == 88u );
    REQUIRE(
        SecretStreamPull_Message(
            &pull,
            BinaryBlock_FromData( ciphertext.data(), cbCiphertext ),
            {},
            output.data(),
            output.size(),
            &cbOutput,
            &tag ) == security_status_t::OK );
    REQUIRE( output == plaintext );
}
