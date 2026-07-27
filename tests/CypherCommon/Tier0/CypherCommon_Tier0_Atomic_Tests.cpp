//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Atomic_Tests.cpp
//  Purpose: Tests CypherCommon Tier0 atomic primitive behavior.
//  Details: These tests validate the small shared-state API used by future
//           async IO, profiler counters, job queues, and runtime shutdown flags.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Atomic.h"

#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <vector>

using namespace cypher::common;

TEST_CASE( "Atomic load and store update scalar values", "[CypherCommon][Tier0][Atomic]" )
{
    atomic_u32_t nValue{ 7u };

    REQUIRE( Cy_AtomicLoad( &nValue ) == 7u );

    Cy_AtomicStore( &nValue, 42u, CY_MEMORY_ORDER_RELAXED );

    REQUIRE( Cy_AtomicLoad( &nValue, CY_MEMORY_ORDER_RELAXED ) == 42u );
}

TEST_CASE( "Atomic exchange returns the previous value", "[CypherCommon][Tier0][Atomic]" )
{
    atomic_i32_t nValue{ -4 };

    const i32 nOldValue = Cy_AtomicExchange( &nValue, 9 );

    REQUIRE( nOldValue == -4 );
    REQUIRE( Cy_AtomicLoad( &nValue ) == 9 );
}

TEST_CASE( "Atomic compare exchange reports success and updates expected on failure", "[CypherCommon][Tier0][Atomic]" )
{
    atomic_u32_t nValue{ 12u };

    u32 nExpected = 12u;
    REQUIRE( Cy_AtomicCompareExchange( &nValue, &nExpected, 30u ) );
    REQUIRE( nExpected == 12u );
    REQUIRE( Cy_AtomicLoad( &nValue ) == 30u );

    nExpected = 12u;
    REQUIRE_FALSE( Cy_AtomicCompareExchange( &nValue, &nExpected, 40u ) );
    REQUIRE( nExpected == 30u );
    REQUIRE( Cy_AtomicLoad( &nValue ) == 30u );
}

TEST_CASE( "Atomic fetch arithmetic returns old values and mutates integers", "[CypherCommon][Tier0][Atomic]" )
{
    atomic_u64_t nValue{ 100u };

    REQUIRE( Cy_AtomicFetchAdd( &nValue, static_cast<u64>( 25u ), CY_MEMORY_ORDER_RELAXED ) == 100u );
    REQUIRE( Cy_AtomicLoad( &nValue, CY_MEMORY_ORDER_RELAXED ) == 125u );

    REQUIRE( Cy_AtomicFetchSub( &nValue, static_cast<u64>( 5u ), CY_MEMORY_ORDER_RELAXED ) == 125u );
    REQUIRE( Cy_AtomicLoad( &nValue, CY_MEMORY_ORDER_RELAXED ) == 120u );
}

TEST_CASE( "Atomic fetch bit operations return old values and mutate integers", "[CypherCommon][Tier0][Atomic]" )
{
    atomic_u32_t nFlags{ 0b1010u };

    REQUIRE( Cy_AtomicFetchOr( &nFlags, 0b0101u, CY_MEMORY_ORDER_RELAXED ) == 0b1010u );
    REQUIRE( Cy_AtomicLoad( &nFlags, CY_MEMORY_ORDER_RELAXED ) == 0b1111u );

    REQUIRE( Cy_AtomicFetchAnd( &nFlags, 0b0110u, CY_MEMORY_ORDER_RELAXED ) == 0b1111u );
    REQUIRE( Cy_AtomicLoad( &nFlags, CY_MEMORY_ORDER_RELAXED ) == 0b0110u );

    REQUIRE( Cy_AtomicFetchXor( &nFlags, 0b0011u, CY_MEMORY_ORDER_RELAXED ) == 0b0110u );
    REQUIRE( Cy_AtomicLoad( &nFlags, CY_MEMORY_ORDER_RELAXED ) == 0b0101u );
}

TEST_CASE( "Atomic pointer alias can publish and read pointer values", "[CypherCommon][Tier0][Atomic]" )
{
    i32 nValueA = 10;
    i32 nValueB = 20;
    atomic_ptr_t<i32> pValue{ &nValueA };

    REQUIRE( Cy_AtomicLoad( &pValue ) == &nValueA );

    Cy_AtomicStore( &pValue, &nValueB, CY_MEMORY_ORDER_RELEASE );

    REQUIRE( Cy_AtomicLoad( &pValue, CY_MEMORY_ORDER_ACQUIRE ) == &nValueB );
}

TEST_CASE( "Atomic pointer arithmetic advances by elements", "[CypherCommon][Tier0][Atomic]" )
{
    i32 values[4] = {};
    atomic_ptr_t<i32> pValue{ values };

    REQUIRE( Cy_AtomicFetchAdd( &pValue, 2 ) == values );
    REQUIRE( Cy_AtomicLoad( &pValue ) == values + 2 );
    REQUIRE( Cy_AtomicFetchSub( &pValue, 1 ) == values + 2 );
    REQUIRE( Cy_AtomicLoad( &pValue ) == values + 1 );
}

TEST_CASE( "Atomic order normalization prevents invalid standard orders", "[CypherCommon][Tier0][Atomic]" )
{
    STATIC_REQUIRE( Cy_AtomicNormalizeLoadOrder( CY_MEMORY_ORDER_RELEASE ) ==
                    CY_MEMORY_ORDER_SEQ_CST );
    STATIC_REQUIRE( Cy_AtomicNormalizeStoreOrder( CY_MEMORY_ORDER_ACQUIRE ) ==
                    CY_MEMORY_ORDER_SEQ_CST );
    STATIC_REQUIRE( Cy_AtomicNormalizeReadModifyWriteOrder(
                        static_cast<memory_order_t>( 255 ) ) ==
                    CY_MEMORY_ORDER_SEQ_CST );
    STATIC_REQUIRE( Cy_AtomicNormalizeFailureOrder(
                        CY_MEMORY_ORDER_RELEASE,
                        CY_MEMORY_ORDER_ACQUIRE ) ==
                    CY_MEMORY_ORDER_RELAXED );
    STATIC_REQUIRE( Cy_AtomicNormalizeFailureOrder(
                        CY_MEMORY_ORDER_ACQ_REL,
                        CY_MEMORY_ORDER_SEQ_CST ) ==
                    CY_MEMORY_ORDER_ACQUIRE );

    atomic_u32_t nValue{ 3u };
    const memory_order_t invalidOrder = static_cast<memory_order_t>( 255 );
    REQUIRE( Cy_AtomicExchange( &nValue, 4u, invalidOrder ) == 3u );
    REQUIRE( Cy_AtomicFetchAdd( &nValue, 2u, invalidOrder ) == 4u );
    REQUIRE( Cy_AtomicLoad( &nValue ) == 6u );
}

TEST_CASE( "Atomic fetch add remains exact under contention", "[CypherCommon][Tier0][Atomic]" )
{
    atomic_u64_t nCounter{ 0u };
    constexpr u32 nThreadCount = 4u;
    constexpr u32 nIterations = 10000u;
    std::vector<std::thread> threads;
    threads.reserve( nThreadCount );

    for ( u32 nThread = 0u; nThread < nThreadCount; ++nThread ) {
        threads.emplace_back( [&nCounter]() {
            for ( u32 nIteration = 0u; nIteration < nIterations; ++nIteration ) {
                ( void )Cy_AtomicFetchAdd(
                    &nCounter,
                    static_cast<u64>( 1u ),
                    CY_MEMORY_ORDER_RELAXED );
            }
        } );
    }
    for ( std::thread &thread : threads ) {
        thread.join();
    }

    REQUIRE( Cy_AtomicLoad( &nCounter ) ==
             static_cast<u64>( nThreadCount ) * nIterations );
}

TEST_CASE( "Atomic wait and notify publish state", "[CypherCommon][Tier0][Atomic]" )
{
    atomic_u32_t nState{ 0u };
    u32 nObserved = 0u;
    std::thread waiter( [&]() {
        Cy_AtomicWait( &nState, 0u, CY_MEMORY_ORDER_ACQUIRE );
        nObserved = Cy_AtomicLoad( &nState, CY_MEMORY_ORDER_ACQUIRE );
    } );

    Cy_AtomicStore( &nState, 1u, CY_MEMORY_ORDER_RELEASE );
    Cy_AtomicNotifyOne( &nState );
    waiter.join();

    REQUIRE( nObserved == 1u );
}

TEST_CASE( "Atomic flags test set and clear", "[CypherCommon][Tier0][Atomic]" )
{
    atomic_flag_t flag = ATOMIC_FLAG_INIT;

    REQUIRE_FALSE( Cy_AtomicFlagTest( &flag ) );
    REQUIRE_FALSE( Cy_AtomicFlagTestAndSet( &flag ) );
    REQUIRE( Cy_AtomicFlagTest( &flag ) );
    REQUIRE( Cy_AtomicFlagTestAndSet( &flag ) );
    Cy_AtomicFlagClear( &flag );
    REQUIRE_FALSE( Cy_AtomicFlagTest( &flag ) );
}

TEST_CASE( "Atomic lock-free query and fences are callable", "[CypherCommon][Tier0][Atomic]" )
{
    atomic_u32_t nValue{ 0u };

    static_cast<void>( Cy_AtomicIsLockFree( &nValue ) );

    Cy_AtomicFenceAcquire();
    Cy_AtomicFenceRelease();
    Cy_AtomicFenceAcqRel();
    Cy_AtomicFenceSeqCst();
    Cy_AtomicSignalFence();

    SUCCEED( "Atomic fences returned." );
}
