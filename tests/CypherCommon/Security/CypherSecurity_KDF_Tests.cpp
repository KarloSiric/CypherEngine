//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Security/CypherSecurity_KDF_Tests.cpp
//  Purpose: Tests master-key ownership and domain-separated derivation.
//  Details: Determinism, context and identifier separation, supported sizes,
//           lifecycle failures, and concurrent derivation are covered.
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

#include <array>
#include <atomic>
#include <thread>
#include <type_traits>

using namespace cypher::common;
using namespace cypher::security;

namespace
{

assert_action_t CaptureKdfAssert( const assert_info_t & ) noexcept
{
    return assert_action_t::Continue;
}

} // namespace

static_assert( !std::is_copy_constructible_v<kdf_master_key_t> );
static_assert( !std::is_move_constructible_v<kdf_master_key_t> );
static_assert( !std::is_copy_constructible_v<kdf_subkey_t> );
static_assert( !std::is_move_constructible_v<kdf_subkey_t> );

TEST_CASE( "CypherSecurity KDF derives deterministic domain-separated keys",
           "[CypherSecurity][KDF]" )
{
    std::array<byte, CY_SECURITY_KDF_MASTER_KEY_SIZE> masterBytes{};
    for ( usize iByte = 0u; iByte < masterBytes.size(); ++iByte ) {
        masterBytes[iByte] = static_cast<byte>( iByte );
    }

    kdf_master_key_t master{};
    REQUIRE(
        SecurityKdf_ImportMasterKey(
            BinaryBlock_FromData( masterBytes.data(), masterBytes.size() ),
            secure_memory_lock_policy_t::BEST_EFFORT,
            &master ) == security_status_t::OK );
    REQUIRE( SecurityKdf_MasterKeyIsValid( &master ) );

    kdf_context_t contextA{};
    kdf_context_t contextB{};
    REQUIRE(
        SecurityKdf_ContextFromBytes( "CYPHAEAD", 8u, &contextA ) ==
        security_status_t::OK );
    REQUIRE(
        SecurityKdf_ContextFromBytes( "CYPHPAK0", 8u, &contextB ) ==
        security_status_t::OK );

    kdf_subkey_t first{};
    kdf_subkey_t repeated{};
    kdf_subkey_t otherId{};
    kdf_subkey_t otherContext{};
    REQUIRE(
        SecurityKdf_DeriveSubkey(
            &master,
            contextA,
            7u,
            32u,
            secure_memory_lock_policy_t::BEST_EFFORT,
            &first ) == security_status_t::OK );
    REQUIRE(
        SecurityKdf_DeriveSubkey(
            &master,
            contextA,
            7u,
            32u,
            secure_memory_lock_policy_t::BEST_EFFORT,
            &repeated ) == security_status_t::OK );
    REQUIRE(
        SecurityKdf_DeriveSubkey(
            &master,
            contextA,
            8u,
            32u,
            secure_memory_lock_policy_t::BEST_EFFORT,
            &otherId ) == security_status_t::OK );
    REQUIRE(
        SecurityKdf_DeriveSubkey(
            &master,
            contextB,
            7u,
            32u,
            secure_memory_lock_policy_t::BEST_EFFORT,
            &otherContext ) == security_status_t::OK );

    const binary_block_t firstBytes = SecurityKdf_SubkeyBlock( &first );
    const binary_block_t repeatedBytes = SecurityKdf_SubkeyBlock( &repeated );
    const binary_block_t otherIdBytes = SecurityKdf_SubkeyBlock( &otherId );
    const binary_block_t otherContextBytes =
        SecurityKdf_SubkeyBlock( &otherContext );
    REQUIRE( firstBytes.cbSize == 32u );
    REQUIRE(
        Security_ConstantTimeEquals(
            firstBytes.pData,
            repeatedBytes.pData,
            firstBytes.cbSize ) );
    REQUIRE_FALSE(
        Security_ConstantTimeEquals(
            firstBytes.pData,
            otherIdBytes.pData,
            firstBytes.cbSize ) );
    REQUIRE_FALSE(
        Security_ConstantTimeEquals(
            firstBytes.pData,
            otherContextBytes.pData,
            firstBytes.cbSize ) );
}

TEST_CASE( "CypherSecurity KDF accepts its complete subkey size range",
           "[CypherSecurity][KDF][Bounds]" )
{
    kdf_master_key_t master{};
    REQUIRE(
        SecurityKdf_GenerateMasterKey(
            secure_memory_lock_policy_t::BEST_EFFORT,
            &master ) == security_status_t::OK );

    kdf_context_t context{};
    REQUIRE(
        SecurityKdf_ContextFromBytes( "CYPHTEST", 8u, &context ) ==
        security_status_t::OK );

    kdf_subkey_t shortest{};
    kdf_subkey_t longest{};
    REQUIRE(
        SecurityKdf_DeriveSubkey(
            &master,
            context,
            0u,
            CY_SECURITY_KDF_SUBKEY_MIN_SIZE,
            secure_memory_lock_policy_t::BEST_EFFORT,
            &shortest ) == security_status_t::OK );
    REQUIRE(
        SecurityKdf_DeriveSubkey(
            &master,
            context,
            CY_U64_MAX,
            CY_SECURITY_KDF_SUBKEY_MAX_SIZE,
            secure_memory_lock_policy_t::BEST_EFFORT,
            &longest ) == security_status_t::OK );
    REQUIRE(
        SecurityKdf_SubkeyBlock( &shortest ).cbSize ==
        CY_SECURITY_KDF_SUBKEY_MIN_SIZE );
    REQUIRE(
        SecurityKdf_SubkeyBlock( &longest ).cbSize ==
        CY_SECURITY_KDF_SUBKEY_MAX_SIZE );
}

TEST_CASE( "CypherSecurity KDF rejects invalid contracts",
           "[CypherSecurity][KDF][Contract]" )
{
    std::array<byte, CY_SECURITY_KDF_MASTER_KEY_SIZE> keyBytes{};
    kdf_master_key_t master{};
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureKdfAssert );
    const security_status_t invalidImportResult =
        SecurityKdf_ImportMasterKey(
            BinaryBlock_FromData( keyBytes.data(), keyBytes.size() - 1u ),
            secure_memory_lock_policy_t::BEST_EFFORT,
            &master );

    kdf_context_t context{};
    const security_status_t invalidContextResult =
        SecurityKdf_ContextFromBytes( "SHORT", 5u, &context );
    Cy_AssertSetHandler( pPreviousHandler );

    REQUIRE( invalidImportResult == security_status_t::INVALID_ARGUMENT );
    REQUIRE_FALSE( SecurityKdf_MasterKeyIsValid( &master ) );
    REQUIRE( invalidContextResult == security_status_t::INVALID_ARGUMENT );
    REQUIRE(
        SecurityKdf_ContextFromBytes( "CYPHTEST", 8u, &context ) ==
        security_status_t::OK );

    kdf_subkey_t subkey{};
    Cy_AssertSetHandler( CaptureKdfAssert );
    const security_status_t invalidMasterResult =
        SecurityKdf_DeriveSubkey(
            &master,
            context,
            0u,
            32u,
            secure_memory_lock_policy_t::BEST_EFFORT,
            &subkey );
    Cy_AssertSetHandler( pPreviousHandler );

    REQUIRE( invalidMasterResult == security_status_t::INVALID_ARGUMENT );
    REQUIRE( SecurityKdf_SubkeyBlock( &subkey ).pData == nullptr );

    SecurityKdf_DestroyMasterKey( nullptr );
    SecurityKdf_DestroySubkey( nullptr );
}

TEST_CASE( "CypherSecurity KDF supports concurrent derivation from one master",
           "[CypherSecurity][KDF][Thread]" )
{
    kdf_master_key_t master{};
    REQUIRE(
        SecurityKdf_GenerateMasterKey(
            secure_memory_lock_policy_t::BEST_EFFORT,
            &master ) == security_status_t::OK );
    kdf_context_t context{};
    REQUIRE(
        SecurityKdf_ContextFromBytes( "CYPHTHRD", 8u, &context ) ==
        security_status_t::OK );

    constexpr usize cThreads = 8u;
    std::array<std::thread, cThreads> threads{};
    std::atomic<bool> bAllSucceeded{ true };
    for ( usize iThread = 0u; iThread < cThreads; ++iThread ) {
        threads[iThread] = std::thread( [&master, &context, iThread, &bAllSucceeded] {
            kdf_subkey_t subkey{};
            if ( SecurityKdf_DeriveSubkey(
                     &master,
                     context,
                     static_cast<u64>( iThread ),
                     32u,
                     secure_memory_lock_policy_t::BEST_EFFORT,
                     &subkey ) != security_status_t::OK ||
                 !SecurityKdf_SubkeyIsValid( &subkey ) ) {
                bAllSucceeded.store( false, std::memory_order_relaxed );
            }
        } );
    }
    for ( std::thread &thread : threads ) {
        thread.join();
    }
    REQUIRE( bAllSucceeded.load( std::memory_order_relaxed ) );
}
