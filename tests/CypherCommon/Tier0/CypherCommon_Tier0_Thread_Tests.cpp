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

#include <array>
#include <atomic>
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
    const timer_tick_t nStartTicks = Cy_TimerNowTicks();
    Cy_ThreadSleepMs( 1u );
    const timer_tick_t nEndTicks = Cy_TimerNowTicks();

    REQUIRE( nEndTicks > nStartTicks );
}

TEST_CASE( "Thread microsecond sleep returns after monotonic time advances", "[CypherCommon][Tier0][Thread]" )
{
    const timer_tick_t nStartTicks = Cy_TimerNowTicks();
    Cy_ThreadSleepUs( 100u );
    const timer_tick_t nEndTicks = Cy_TimerNowTicks();

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
    REQUIRE( Cy_ThreadCaptureMainThread() );

    bool_t bWorkerSawMainThread = CY_TRUE;
    bool_t bWorkerNameSet = CY_FALSE;
    thread_id_t nWorkerThreadId = CY_THREAD_INVALID_ID;

    std::thread worker( [&]() {
        bWorkerNameSet = Cy_ThreadSetCurrentName( "CyTestWorker" );
        bWorkerSawMainThread = Cy_ThreadIsMainThread();
        nWorkerThreadId = Cy_ThreadGetCurrentId();
    } );
    worker.join();

    REQUIRE( bWorkerNameSet );
    REQUIRE_FALSE( bWorkerSawMainThread );
    REQUIRE( nWorkerThreadId != CY_THREAD_INVALID_ID );
    REQUIRE( nWorkerThreadId != Cy_ThreadGetMainThreadId() );
}

TEST_CASE( "Thread module assigns distinct process-local IDs", "[CypherCommon][Tier0][Thread]" )
{
    constexpr usize nThreadCount = 8u;
    std::array<thread_id_t, nThreadCount> threadIds{};
    std::array<std::thread, nThreadCount> threads;

    for ( usize nIndex = 0u; nIndex < nThreadCount; ++nIndex ) {
        threads[nIndex] = std::thread( [&, nIndex]() {
            threadIds[nIndex] = Cy_ThreadGetCurrentId();
        } );
    }
    for ( std::thread &thread : threads ) {
        thread.join();
    }

    for ( usize nLeft = 0u; nLeft < nThreadCount; ++nLeft ) {
        REQUIRE( threadIds[nLeft] != CY_THREAD_INVALID_ID );
        for ( usize nRight = nLeft + 1u; nRight < nThreadCount; ++nRight ) {
            REQUIRE( threadIds[nLeft] != threadIds[nRight] );
        }
    }
}

TEST_CASE( "Thread main identity cannot be rebound by a worker", "[CypherCommon][Tier0][Thread]" )
{
    Cy_ThreadShutdown();
    REQUIRE( Cy_ThreadCaptureMainThread() );
    const thread_id_t nMainThreadId = Cy_ThreadGetMainThreadId();
    bool_t didWorkerCapture = CY_TRUE;

    std::thread worker( [&]() {
        didWorkerCapture = Cy_ThreadCaptureMainThread();
    } );
    worker.join();

    REQUIRE_FALSE( didWorkerCapture );
    REQUIRE( Cy_ThreadGetMainThreadId() == nMainThreadId );
    REQUIRE( Cy_ThreadIsMainThread() );
}

namespace
{

i32 ThreadTestProc( void *pUserData ) noexcept
{
    auto *pValue = static_cast<i32 *>( pUserData );
    *pValue += 7;
    return *pValue;
}

struct blocking_thread_context_t {
    std::atomic_bool hasStarted{ false };
    std::atomic_bool mayExit{ false };
};

i32 BlockingThreadTestProc( void *pUserData ) noexcept
{
    auto *pContext = static_cast<blocking_thread_context_t *>( pUserData );
    pContext->hasStarted.store( true, std::memory_order_release );
    while ( !pContext->mayExit.load( std::memory_order_acquire ) ) {
        Cy_ThreadYield();
    }
    return 23;
}

} // namespace

TEST_CASE( "Thread create and join preserve procedure result and identity", "[CypherCommon][Tier0][Thread]" )
{
    cy_thread_t thread{};
    i32 value = 5;

    REQUIRE_FALSE( Cy_ThreadCreate( nullptr, ThreadTestProc, &value ) );
    REQUIRE_FALSE( Cy_ThreadCreate( &thread, nullptr, &value ) );
    REQUIRE( Cy_ThreadCreate( &thread, ThreadTestProc, &value, "CyThreadTest" ) );
    REQUIRE_FALSE( Cy_ThreadCreate( &thread, ThreadTestProc, &value ) );

    i32 nResult = 0;
    REQUIRE( Cy_ThreadJoin( &thread, &nResult ) );
    REQUIRE( nResult == 12 );
    REQUIRE( value == 12 );
    REQUIRE( Cy_ThreadGetId( &thread ) != CY_THREAD_INVALID_ID );
    REQUIRE_FALSE( Cy_ThreadIsRunning( &thread ) );
    REQUIRE_FALSE( Cy_ThreadJoin( &thread ) );
}

TEST_CASE( "Thread reports running state for its complete joinable lifetime", "[CypherCommon][Tier0][Thread]" )
{
    cy_thread_t thread{};
    blocking_thread_context_t context{};

    REQUIRE( Cy_ThreadCreate(
        &thread,
        BlockingThreadTestProc,
        &context,
        "CyRunningTest" ) );
    REQUIRE( Cy_ThreadIsRunning( &thread ) );

    while ( !context.hasStarted.load( std::memory_order_acquire ) ) {
        Cy_ThreadYield();
    }
    REQUIRE( Cy_ThreadIsRunning( &thread ) );

    context.mayExit.store( true, std::memory_order_release );
    i32 nResult = 0;
    REQUIRE( Cy_ThreadJoin( &thread, &nResult ) );
    REQUIRE( nResult == 23 );
    REQUIRE_FALSE( Cy_ThreadIsRunning( &thread ) );
}
