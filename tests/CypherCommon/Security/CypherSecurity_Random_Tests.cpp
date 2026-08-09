//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Security/CypherSecurity_Random_Tests.cpp
//  Purpose: Tests cryptographically secure random-data contracts.
//  Details: The tests cover valid and invalid ranges, scalar generation,
//           unbiased bounds, transactional outputs, and concurrent use.
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
#include <limits>
#include <thread>

using namespace cypher::common;
using namespace cypher::security;

namespace
{

u32 g_randomAssertCount = 0u;

assert_action_t CaptureRandomAssert( const assert_info_t & ) noexcept
{
    ++g_randomAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "CypherSecurity random services fill bytes and generate scalars",
           "[CypherSecurity][Random]" )
{
    REQUIRE( Security_Init() == security_status_t::OK );

    std::array<byte, 64u> first{};
    std::array<byte, 64u> second{};
    REQUIRE(
        SecurityRandom_Fill( first.data(), first.size() ) ==
        security_status_t::OK );
    REQUIRE(
        SecurityRandom_Fill( second.data(), second.size() ) ==
        security_status_t::OK );
    REQUIRE( first != second );

    REQUIRE(
        SecurityRandom_Fill( nullptr, 0u ) == security_status_t::OK );
    REQUIRE(
        SecurityRandom_Fill( first.data(), 0u ) == security_status_t::OK );

    u32 nValue32 = 0u;
    u64 nValue64 = 0u;
    REQUIRE(
        SecurityRandom_U32( &nValue32 ) == security_status_t::OK );
    REQUIRE(
        SecurityRandom_U64( &nValue64 ) == security_status_t::OK );
}

TEST_CASE( "CypherSecurity bounded random values remain inside their ranges",
           "[CypherSecurity][Random]" )
{
    constexpr std::array<u32, 6u> bounds32{
        1u,
        2u,
        3u,
        10u,
        257u,
        std::numeric_limits<u32>::max()
    };
    for ( const u32 nBound : bounds32 ) {
        for ( usize iSample = 0u; iSample < 1024u; ++iSample ) {
            u32 nValue = std::numeric_limits<u32>::max();
            REQUIRE(
                SecurityRandom_UniformU32( nBound, &nValue ) ==
                security_status_t::OK );
            REQUIRE( nValue < nBound );
        }
    }

    constexpr std::array<u64, 6u> bounds64{
        1u,
        2u,
        3u,
        10u,
        0x8000000000000001ull,
        std::numeric_limits<u64>::max()
    };
    for ( const u64 nBound : bounds64 ) {
        for ( usize iSample = 0u; iSample < 1024u; ++iSample ) {
            u64 nValue = std::numeric_limits<u64>::max();
            REQUIRE(
                SecurityRandom_UniformU64( nBound, &nValue ) ==
                security_status_t::OK );
            REQUIRE( nValue < nBound );
        }
    }
}

TEST_CASE( "CypherSecurity random services reject invalid arguments",
           "[CypherSecurity][Random][Contract]" )
{
    g_randomAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureRandomAssert );

    u32 nValue32 = 0x12345678u;
    u64 nValue64 = 0x123456789ABCDEF0ull;
    const security_status_t fillResult = SecurityRandom_Fill( nullptr, 1u );
    const security_status_t value32Result = SecurityRandom_U32( nullptr );
    const security_status_t value64Result = SecurityRandom_U64( nullptr );
    const security_status_t zeroBound32Result =
        SecurityRandom_UniformU32( 0u, &nValue32 );
    const security_status_t nullOutput32Result =
        SecurityRandom_UniformU32( 2u, nullptr );
    const security_status_t zeroBound64Result =
        SecurityRandom_UniformU64( 0u, &nValue64 );
    const security_status_t nullOutput64Result =
        SecurityRandom_UniformU64( 2u, nullptr );

    Cy_AssertSetHandler( pPreviousHandler );

    REQUIRE( fillResult == security_status_t::INVALID_ARGUMENT );
    REQUIRE( value32Result == security_status_t::INVALID_ARGUMENT );
    REQUIRE( value64Result == security_status_t::INVALID_ARGUMENT );
    REQUIRE( zeroBound32Result == security_status_t::INVALID_ARGUMENT );
    REQUIRE( nullOutput32Result == security_status_t::INVALID_ARGUMENT );
    REQUIRE( zeroBound64Result == security_status_t::INVALID_ARGUMENT );
    REQUIRE( nullOutput64Result == security_status_t::INVALID_ARGUMENT );
    REQUIRE( nValue32 == 0x12345678u );
    REQUIRE( nValue64 == 0x123456789ABCDEF0ull );
    REQUIRE(
        g_randomAssertCount ==
        7u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}

TEST_CASE( "CypherSecurity random generation supports concurrent callers",
           "[CypherSecurity][Random][Thread]" )
{
    constexpr usize cThreads = 8u;
    constexpr usize cOperationsPerThread = 128u;
    std::array<std::thread, cThreads> threads{};
    std::atomic<bool> bAllSucceeded{ true };

    for ( usize iThread = 0u; iThread < cThreads; ++iThread ) {
        threads[iThread] = std::thread( [&bAllSucceeded] {
            std::array<byte, 32u> bytes{};
            for ( usize iOperation = 0u;
                  iOperation < cOperationsPerThread;
                  ++iOperation ) {
                u64 nValue = 0u;
                if ( Security_Init() != security_status_t::OK ||
                     SecurityRandom_Fill( bytes.data(), bytes.size() ) !=
                         security_status_t::OK ||
                     SecurityRandom_U64( &nValue ) != security_status_t::OK ) {
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
