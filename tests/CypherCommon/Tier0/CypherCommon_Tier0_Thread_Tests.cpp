//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Thread_Tests.cpp
//  Purpose: Tests CypherCommon Tier0 thread helper behavior.
//  Details: This test file validates the minimal thread utilities used below the
//           job system, profiler, async IO, and platform runtime code.
//
//  History:
//  - Created by Karlo Siric on 2026-07-03
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Thread.h"
#include "CypherCommon_Timer.h"

#include <catch2/catch_test_macros.hpp>

#include <thread>

using namespace cypher::common;

TEST_CASE( "Thread helper reports at least one logical thread", "[CypherCommon][Tier0][Thread]" )
{
    REQUIRE( Cy_ThreadGetLogicalCount() >= 1u );
}

TEST_CASE( "Thread id hash is stable inside the same thread", "[CypherCommon][Tier0][Thread]" )
{
    const thread_id_t nThreadHashA = Cy_ThreadGetCurrentId();
    const thread_id_t nThreadHashB = Cy_ThreadGetCurrentIdHash();

    REQUIRE( nThreadHashA == nThreadHashB );
    REQUIRE( nThreadHashA != CY_THREAD_INVALID_ID );
}

TEST_CASE( "Thread yield returns control to the caller", "[CypherCommon][Tier0][Thread]" )
{
    Cy_ThreadYield();
    SUCCEED( "Cy_ThreadYield returned." );
}

TEST_CASE( "Thread sleep waits for observable monotonic time", "[CypherCommon][Tier0][Thread]" )
{
    const timer_tick_t nStartTicks = Timer_NowTicks();
    Cy_ThreadSleepMs( 1u );
    const timer_tick_t nEndTicks = Timer_NowTicks();

    REQUIRE( nEndTicks > nStartTicks );
}

TEST_CASE( "Thread microsecond sleep returns after monotonic time advances", "[CypherCommon][Tier0][Thread]" )
{
    const timer_tick_t nStartTicks = Timer_NowTicks();
    Cy_ThreadSleepUs( 100u );
    const timer_tick_t nEndTicks = Timer_NowTicks();

    REQUIRE( nEndTicks > nStartTicks );
}

TEST_CASE( "Thread module captures and reports main thread identity", "[CypherCommon][Tier0][Thread]" )
{
    Cy_ThreadShutdown();

    REQUIRE_FALSE( Cy_ThreadIsInitialized() );
    REQUIRE_FALSE( Cy_ThreadIsMainThread() );
    REQUIRE( Cy_ThreadGetMainThreadId() == CY_THREAD_INVALID_ID );

    REQUIRE( Cy_ThreadInit() );
    REQUIRE( Cy_ThreadIsInitialized() );
    REQUIRE( Cy_ThreadIsMainThread() );
    REQUIRE( Cy_ThreadGetMainThreadId() == Cy_ThreadGetCurrentId() );
}

TEST_CASE( "Thread module distinguishes worker thread from captured main thread", "[CypherCommon][Tier0][Thread]" )
{
    Cy_ThreadCaptureMainThread();

    bool_t bWorkerSawMainThread = CY_TRUE;
    thread_id_t nWorkerThreadId = CY_THREAD_INVALID_ID;

    std::thread worker( [&]() {
        Cy_ThreadSetCurrentName( "CyTestWorker" );
        bWorkerSawMainThread = Cy_ThreadIsMainThread();
        nWorkerThreadId = Cy_ThreadGetCurrentId();
    } );
    worker.join();

    REQUIRE_FALSE( bWorkerSawMainThread );
    REQUIRE( nWorkerThreadId != CY_THREAD_INVALID_ID );
    REQUIRE( nWorkerThreadId != Cy_ThreadGetMainThreadId() );
}
