//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Security/CypherSecurity_SecureMemory_Tests.cpp
//  Purpose: Tests guarded secret-memory ownership and access contracts.
//  Details: The tests cover lifecycle, zeroing, protection transitions,
//           invalid states, lock policy, and independent concurrent owners.
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

u32 g_secureMemoryAssertCount = 0u;

assert_action_t CaptureSecureMemoryAssert( const assert_info_t & ) noexcept
{
    ++g_secureMemoryAssertCount;
    return assert_action_t::Continue;
}

} // namespace

static_assert( !std::is_copy_constructible_v<secure_memory_t> );
static_assert( !std::is_copy_assignable_v<secure_memory_t> );
static_assert( !std::is_move_constructible_v<secure_memory_t> );
static_assert( !std::is_move_assignable_v<secure_memory_t> );

TEST_CASE( "CypherSecurity secure memory owns a fixed guarded allocation",
           "[CypherSecurity][SecureMemory]" )
{
    REQUIRE( Security_Init() == security_status_t::OK );

    secure_memory_t memory{};
    REQUIRE_FALSE( SecureMemory_IsValid( nullptr ) );
    REQUIRE_FALSE( SecureMemory_IsLocked( nullptr ) );
    REQUIRE( SecureMemory_Size( nullptr ) == 0u );
    REQUIRE(
        SecureMemory_GetAccess( nullptr ) ==
        secure_memory_access_t::INACTIVE );
    REQUIRE_FALSE( SecureMemory_IsValid( &memory ) );
    REQUIRE_FALSE( SecureMemory_IsLocked( &memory ) );
    REQUIRE( SecureMemory_Size( &memory ) == 0u );
    REQUIRE(
        SecureMemory_GetAccess( &memory ) ==
        secure_memory_access_t::INACTIVE );

    REQUIRE(
        SecureMemory_Create(
            64u,
            secure_memory_lock_policy_t::BEST_EFFORT,
            &memory ) == security_status_t::OK );
    REQUIRE( SecureMemory_IsValid( &memory ) );
    REQUIRE( SecureMemory_Size( &memory ) == 64u );
    REQUIRE(
        SecureMemory_GetAccess( &memory ) ==
        secure_memory_access_t::READ_WRITE );

    byte *pBytes = SecureMemory_Data( &memory );
    REQUIRE( pBytes != nullptr );
    for ( usize iByte = 0u; iByte < SecureMemory_Size( &memory ); ++iByte ) {
        REQUIRE( pBytes[iByte] == 0u );
        pBytes[iByte] = static_cast<byte>( iByte + 1u );
    }

    SecureMemory_Destroy( &memory );
    REQUIRE_FALSE( SecureMemory_IsValid( &memory ) );
    REQUIRE( SecureMemory_Size( &memory ) == 0u );
    REQUIRE(
        SecureMemory_GetAccess( &memory ) ==
        secure_memory_access_t::INACTIVE );

    // Destruction is deliberately idempotent for cleanup paths.
    SecureMemory_Destroy( &memory );
    SecureMemory_Destroy( nullptr );
}

TEST_CASE( "CypherSecurity secure memory preserves bytes across protection states",
           "[CypherSecurity][SecureMemory][Protection]" )
{
    secure_memory_t memory{};
    REQUIRE(
        SecureMemory_Create(
            32u,
            secure_memory_lock_policy_t::BEST_EFFORT,
            &memory ) == security_status_t::OK );

    byte *pWritable = SecureMemory_Data( &memory );
    REQUIRE( pWritable != nullptr );
    for ( usize iByte = 0u; iByte < 32u; ++iByte ) {
        pWritable[iByte] = static_cast<byte>( 0xA0u + iByte );
    }

    REQUIRE(
        SecureMemory_SetReadOnly( &memory ) == security_status_t::OK );
    REQUIRE(
        SecureMemory_GetAccess( &memory ) ==
        secure_memory_access_t::READ_ONLY );
    REQUIRE(
        SecureMemory_SetReadOnly( &memory ) == security_status_t::OK );
    const byte *pReadable = SecureMemory_ConstData( &memory );
    REQUIRE( pReadable != nullptr );
    for ( usize iByte = 0u; iByte < 32u; ++iByte ) {
        REQUIRE( pReadable[iByte] == static_cast<byte>( 0xA0u + iByte ) );
    }

    REQUIRE(
        SecureMemory_SetNoAccess( &memory ) == security_status_t::OK );
    REQUIRE(
        SecureMemory_GetAccess( &memory ) ==
        secure_memory_access_t::NO_ACCESS );
    REQUIRE(
        SecureMemory_SetNoAccess( &memory ) == security_status_t::OK );

    REQUIRE(
        SecureMemory_SetReadWrite( &memory ) == security_status_t::OK );
    REQUIRE(
        SecureMemory_SetReadWrite( &memory ) == security_status_t::OK );
    pWritable = SecureMemory_Data( &memory );
    REQUIRE( pWritable != nullptr );
    for ( usize iByte = 0u; iByte < 32u; ++iByte ) {
        REQUIRE( pWritable[iByte] == static_cast<byte>( 0xA0u + iByte ) );
    }
}

TEST_CASE( "CypherSecurity secure zeroing restores the previous protection",
           "[CypherSecurity][SecureMemory][Zero]" )
{
    secure_memory_t memory{};
    REQUIRE(
        SecureMemory_Create(
            32u,
            secure_memory_lock_policy_t::BEST_EFFORT,
            &memory ) == security_status_t::OK );

    byte *pBytes = SecureMemory_Data( &memory );
    REQUIRE( pBytes != nullptr );
    for ( usize iByte = 0u; iByte < 32u; ++iByte ) {
        pBytes[iByte] = 0xA5u;
    }

    REQUIRE(
        SecureMemory_SetNoAccess( &memory ) == security_status_t::OK );
    REQUIRE( SecureMemory_Zero( &memory ) == security_status_t::OK );
    REQUIRE(
        SecureMemory_GetAccess( &memory ) ==
        secure_memory_access_t::NO_ACCESS );

    REQUIRE(
        SecureMemory_SetReadOnly( &memory ) == security_status_t::OK );
    const byte *pZeroed = SecureMemory_ConstData( &memory );
    REQUIRE( pZeroed != nullptr );
    for ( usize iByte = 0u; iByte < 32u; ++iByte ) {
        REQUIRE( pZeroed[iByte] == 0u );
    }

    REQUIRE( SecureMemory_Zero( &memory ) == security_status_t::OK );
    REQUIRE(
        SecureMemory_GetAccess( &memory ) ==
        secure_memory_access_t::READ_ONLY );
}

TEST_CASE( "CypherSecurity secure memory enforces access and lifecycle contracts",
           "[CypherSecurity][SecureMemory][Contract]" )
{
    g_secureMemoryAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureSecureMemoryAssert );

    secure_memory_t memory{};
    const security_status_t zeroSizeResult = SecureMemory_Create(
        0u,
        secure_memory_lock_policy_t::BEST_EFFORT,
        &memory );
    const security_status_t invalidPolicyResult = SecureMemory_Create(
        16u,
        static_cast<secure_memory_lock_policy_t>( 0xFFu ),
        &memory );
    const security_status_t nullOutputResult = SecureMemory_Create(
        16u,
        secure_memory_lock_policy_t::BEST_EFFORT,
        nullptr );

    REQUIRE(
        SecureMemory_Create(
            16u,
            secure_memory_lock_policy_t::BEST_EFFORT,
            &memory ) == security_status_t::OK );
    const security_status_t activeOutputResult = SecureMemory_Create(
        16u,
        secure_memory_lock_policy_t::BEST_EFFORT,
        &memory );
    REQUIRE(
        SecureMemory_SetReadOnly( &memory ) == security_status_t::OK );
    byte *pInvalidWrite = SecureMemory_Data( &memory );
    REQUIRE(
        SecureMemory_SetNoAccess( &memory ) == security_status_t::OK );
    const byte *pInvalidRead = SecureMemory_ConstData( &memory );
    SecureMemory_Destroy( &memory );

    const security_status_t inactiveWriteResult =
        SecureMemory_SetReadWrite( &memory );
    const security_status_t inactiveReadResult =
        SecureMemory_SetReadOnly( &memory );
    const security_status_t inactiveNoAccessResult =
        SecureMemory_SetNoAccess( &memory );
    const security_status_t inactiveZeroResult = SecureMemory_Zero( &memory );

    Cy_AssertSetHandler( pPreviousHandler );

    REQUIRE( zeroSizeResult == security_status_t::INVALID_ARGUMENT );
    REQUIRE( invalidPolicyResult == security_status_t::INVALID_ARGUMENT );
    REQUIRE( nullOutputResult == security_status_t::INVALID_ARGUMENT );
    REQUIRE( activeOutputResult == security_status_t::INVALID_STATE );
    REQUIRE( pInvalidWrite == nullptr );
    REQUIRE( pInvalidRead == nullptr );
    REQUIRE( inactiveWriteResult == security_status_t::INVALID_STATE );
    REQUIRE( inactiveReadResult == security_status_t::INVALID_STATE );
    REQUIRE( inactiveNoAccessResult == security_status_t::INVALID_STATE );
    REQUIRE( inactiveZeroResult == security_status_t::INVALID_STATE );
    REQUIRE(
        g_secureMemoryAssertCount ==
        10u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}

TEST_CASE( "CypherSecurity required locking is explicit about OS limits",
           "[CypherSecurity][SecureMemory][Lock]" )
{
    secure_memory_t memory{};
    const security_status_t result = SecureMemory_Create(
        64u,
        secure_memory_lock_policy_t::REQUIRE_LOCKED,
        &memory );

    if ( result == security_status_t::OK ) {
        REQUIRE( SecureMemory_IsLocked( &memory ) );
    } else {
        REQUIRE( result == security_status_t::PROTECTION_FAILED );
        REQUIRE_FALSE( SecureMemory_IsValid( &memory ) );
    }
}

TEST_CASE( "CypherSecurity secure memory supports independent concurrent owners",
           "[CypherSecurity][SecureMemory][Thread]" )
{
    constexpr usize cThreads = 8u;
    constexpr usize cOperationsPerThread = 32u;
    std::array<std::thread, cThreads> threads{};
    std::atomic<bool> bAllSucceeded{ true };

    for ( usize iThread = 0u; iThread < cThreads; ++iThread ) {
        threads[iThread] = std::thread( [&bAllSucceeded] {
            for ( usize iOperation = 0u;
                  iOperation < cOperationsPerThread;
                  ++iOperation ) {
                secure_memory_t memory{};
                if ( SecureMemory_Create(
                         32u,
                         secure_memory_lock_policy_t::BEST_EFFORT,
                         &memory ) != security_status_t::OK ||
                     SecureMemory_SetReadOnly( &memory ) !=
                         security_status_t::OK ||
                     SecureMemory_SetNoAccess( &memory ) !=
                         security_status_t::OK ||
                     SecureMemory_Zero( &memory ) != security_status_t::OK ) {
                    bAllSucceeded.store( false, std::memory_order_relaxed );
                    return;
                }
            }
        } );
    }

    for ( std::thread &thread : threads ) {
        thread.join();
    }

    REQUIRE( bAllSucceeded.load( std::memory_order_relaxed ) );
}
