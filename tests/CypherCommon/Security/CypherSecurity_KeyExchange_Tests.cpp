//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Security/CypherSecurity_KeyExchange_Tests.cpp
//  Purpose: Tests directional X25519 session-key derivation.
//  Details: Role symmetry, seed determinism, rejected peer keys, cleanup, and
//           concurrent derivation from guarded local keys are covered.
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

static_assert( !std::is_copy_constructible_v<key_exchange_keypair_t> );
static_assert( !std::is_copy_constructible_v<key_exchange_session_keys_t> );

TEST_CASE( "CypherSecurity key exchange derives matching directional keys",
           "[CypherSecurity][KeyExchange]" )
{
    key_exchange_keypair_t client{};
    key_exchange_keypair_t server{};
    REQUIRE(
        KeyExchangeKeyPair_Generate(
            secure_memory_lock_policy_t::BEST_EFFORT,
            &client ) == security_status_t::OK );
    REQUIRE(
        KeyExchangeKeyPair_Generate(
            secure_memory_lock_policy_t::BEST_EFFORT,
            &server ) == security_status_t::OK );

    key_exchange_session_keys_t clientSession{};
    key_exchange_session_keys_t serverSession{};
    REQUIRE(
        KeyExchange_ClientSessionKeys(
            &client,
            server.publicKey,
            secure_memory_lock_policy_t::BEST_EFFORT,
            &clientSession ) == security_status_t::OK );
    REQUIRE(
        KeyExchange_ServerSessionKeys(
            &server,
            client.publicKey,
            secure_memory_lock_policy_t::BEST_EFFORT,
            &serverSession ) == security_status_t::OK );

    const binary_block_t clientReceive =
        KeyExchangeSessionKeys_ReceiveBlock( &clientSession );
    const binary_block_t clientTransmit =
        KeyExchangeSessionKeys_TransmitBlock( &clientSession );
    const binary_block_t serverReceive =
        KeyExchangeSessionKeys_ReceiveBlock( &serverSession );
    const binary_block_t serverTransmit =
        KeyExchangeSessionKeys_TransmitBlock( &serverSession );
    REQUIRE( clientReceive.cbSize == CY_SECURITY_KX_SESSION_KEY_SIZE );
    REQUIRE(
        Security_ConstantTimeEquals(
            clientReceive.pData,
            serverTransmit.pData,
            clientReceive.cbSize ) );
    REQUIRE(
        Security_ConstantTimeEquals(
            clientTransmit.pData,
            serverReceive.pData,
            clientTransmit.cbSize ) );
    REQUIRE_FALSE(
        Security_ConstantTimeEquals(
            clientReceive.pData,
            clientTransmit.pData,
            clientReceive.cbSize ) );
}

TEST_CASE( "CypherSecurity key exchange is deterministic for fixed seeds",
           "[CypherSecurity][KeyExchange][Seed]" )
{
    std::array<byte, CY_SECURITY_KX_SEED_SIZE> seed{};
    for ( usize iByte = 0u; iByte < seed.size(); ++iByte ) {
        seed[iByte] = static_cast<byte>( iByte + 1u );
    }

    key_exchange_keypair_t first{};
    key_exchange_keypair_t second{};
    REQUIRE(
        KeyExchangeKeyPair_FromSeed(
            BinaryBlock_FromData( seed.data(), seed.size() ),
            secure_memory_lock_policy_t::BEST_EFFORT,
            &first ) == security_status_t::OK );
    REQUIRE(
        KeyExchangeKeyPair_FromSeed(
            BinaryBlock_FromData( seed.data(), seed.size() ),
            secure_memory_lock_policy_t::BEST_EFFORT,
            &second ) == security_status_t::OK );
    REQUIRE(
        Security_ConstantTimeEquals(
            first.publicKey.bytes,
            second.publicKey.bytes,
            sizeof( first.publicKey.bytes ) ) );
}

TEST_CASE( "CypherSecurity key exchange rejects unacceptable peer keys",
           "[CypherSecurity][KeyExchange][Authentication]" )
{
    key_exchange_keypair_t local{};
    REQUIRE(
        KeyExchangeKeyPair_Generate(
            secure_memory_lock_policy_t::BEST_EFFORT,
            &local ) == security_status_t::OK );

    key_exchange_public_key_t invalidPeer{};
    key_exchange_session_keys_t session{};
    REQUIRE(
        KeyExchange_ClientSessionKeys(
            &local,
            invalidPeer,
            secure_memory_lock_policy_t::BEST_EFFORT,
            &session ) == security_status_t::PEER_KEY_REJECTED );
    REQUIRE_FALSE( KeyExchangeSessionKeys_AreValid( &session ) );
    REQUIRE( KeyExchangeSessionKeys_ReceiveBlock( &session ).pData == nullptr );
}

TEST_CASE( "CypherSecurity key exchange supports concurrent local-key reads",
           "[CypherSecurity][KeyExchange][Thread]" )
{
    key_exchange_keypair_t local{};
    key_exchange_keypair_t peer{};
    REQUIRE(
        KeyExchangeKeyPair_Generate(
            secure_memory_lock_policy_t::BEST_EFFORT,
            &local ) == security_status_t::OK );
    REQUIRE(
        KeyExchangeKeyPair_Generate(
            secure_memory_lock_policy_t::BEST_EFFORT,
            &peer ) == security_status_t::OK );

    constexpr usize cThreads = 8u;
    std::array<std::thread, cThreads> threads{};
    std::atomic<bool> bAllSucceeded{ true };
    for ( std::thread &thread : threads ) {
        thread = std::thread( [&local, &peer, &bAllSucceeded] {
            key_exchange_session_keys_t session{};
            if ( KeyExchange_ClientSessionKeys(
                     &local,
                     peer.publicKey,
                     secure_memory_lock_policy_t::BEST_EFFORT,
                     &session ) != security_status_t::OK ||
                 !KeyExchangeSessionKeys_AreValid( &session ) ) {
                bAllSucceeded.store( false, std::memory_order_relaxed );
            }
        } );
    }
    for ( std::thread &thread : threads ) {
        thread.join();
    }
    REQUIRE( bAllSucceeded.load( std::memory_order_relaxed ) );
}

TEST_CASE( "CypherSecurity key exchange imports public keys and clears ownership",
           "[CypherSecurity][KeyExchange][Lifecycle]" )
{
    key_exchange_keypair_t client{};
    key_exchange_keypair_t server{};
    REQUIRE_FALSE( KeyExchangeKeyPair_IsValid( nullptr ) );
    REQUIRE_FALSE( KeyExchangeKeyPair_IsValid( &client ) );
    REQUIRE(
        KeyExchangeKeyPair_Generate(
            secure_memory_lock_policy_t::BEST_EFFORT,
            &client ) == security_status_t::OK );
    REQUIRE(
        KeyExchangeKeyPair_Generate(
            secure_memory_lock_policy_t::BEST_EFFORT,
            &server ) == security_status_t::OK );
    REQUIRE( KeyExchangeKeyPair_IsValid( &client ) );

    key_exchange_public_key_t imported{};
    REQUIRE(
        KeyExchangePublicKey_FromBytes(
            BinaryBlock_FromData(
                server.publicKey.bytes,
                sizeof( server.publicKey.bytes ) ),
            &imported ) == security_status_t::OK );
    REQUIRE(
        Security_ConstantTimeEquals(
            imported.bytes,
            server.publicKey.bytes,
            sizeof( imported.bytes ) ) );

    key_exchange_session_keys_t session{};
    REQUIRE(
        KeyExchange_ClientSessionKeys(
            &client,
            imported,
            secure_memory_lock_policy_t::BEST_EFFORT,
            &session ) == security_status_t::OK );
    REQUIRE( KeyExchangeSessionKeys_AreValid( &session ) );

    KeyExchangeSessionKeys_Destroy( &session );
    REQUIRE_FALSE( KeyExchangeSessionKeys_AreValid( &session ) );
    KeyExchangeSessionKeys_Destroy( &session );
    KeyExchangeSessionKeys_Destroy( nullptr );

    KeyExchangeKeyPair_Destroy( &client );
    REQUIRE_FALSE( KeyExchangeKeyPair_IsValid( &client ) );
    for ( const byte value : client.publicKey.bytes ) {
        REQUIRE( value == 0u );
    }
    KeyExchangeKeyPair_Destroy( &client );
    KeyExchangeKeyPair_Destroy( nullptr );
}
