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

TEST_CASE( "Atomic lock-free query and fences are callable", "[CypherCommon][Tier0][Atomic]" )
{
    atomic_u32_t nValue{ 0u };

    static_cast<void>( Cy_AtomicIsLockFree( &nValue ) );

    Cy_AtomicFenceAcquire();
    Cy_AtomicFenceRelease();
    Cy_AtomicFenceAcqRel();
    Cy_AtomicFenceSeqCst();

    SUCCEED( "Atomic fences returned." );
}
