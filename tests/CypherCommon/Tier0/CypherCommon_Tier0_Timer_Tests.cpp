//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Timer_Tests.cpp
//  Purpose: Tests CypherCommon Tier0 timer behavior.
//  Details: This test file validates monotonic tick ordering and deterministic
//           tick-to-second conversion rules used by profiling and frame timing.
//
//  History:
//  - Created by Karlo Siric on 2026-07-03
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Timer.h"
#include "CypherCommon_Thread.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <thread>

using namespace cypher::common;

TEST_CASE( "Timer lifecycle initializes cached native frequency", "[CypherCommon][Tier0][Timer]" )
{
    Cy_TimerShutdown();
    REQUIRE_FALSE( Cy_TimerIsInitialized() );
    REQUIRE( Cy_TimerInit() );
    REQUIRE( Cy_TimerIsInitialized() );
    REQUIRE( Cy_TimerGetFrequency() > 0u );
    REQUIRE( Cy_TimerInit() );
}

TEST_CASE( "Timer conversion helpers use native frequency", "[CypherCommon][Tier0][Timer]" )
{
    REQUIRE( Cy_TimerInit() );

    const timer_frequency_t nFrequency = Cy_TimerGetFrequency();

    REQUIRE( Cy_TimerTicksToSeconds( 0u ) == 0.0 );
    REQUIRE( Cy_TimerTicksToSeconds( nFrequency ) == 1.0 );
    REQUIRE( Cy_TimerTicksToMilliseconds( nFrequency ) == 1000.0 );
    REQUIRE( Cy_TimerTicksToMicroseconds( nFrequency ) == 1000000.0 );
    REQUIRE( Cy_TimerTicksToNanoseconds( nFrequency ) == 1000000000.0 );
    REQUIRE( Cy_TimerElapsedSeconds( nFrequency, nFrequency * 3u ) == 2.0 );
    STATIC_REQUIRE( Cy_TimerElapsedTicks( 10u, 5u ) == 0u );
}

TEST_CASE( "Timer_NowTicks returns monotonic nondecreasing ticks", "[CypherCommon][Tier0][Timer]" )
{
    const timer_tick_t nStartTicks = Cy_TimerNowTicks();
    const timer_tick_t nEndTicks = Cy_TimerNowTicks();

    REQUIRE( nEndTicks >= nStartTicks );
}

TEST_CASE( "Timer elapsed conversion follows current tick samples", "[CypherCommon][Tier0][Timer]" )
{
    const timer_tick_t nBeforeTicks = Cy_TimerNowTicks();
    const f64 flNowSeconds = Cy_TimerTicksToSeconds( Cy_TimerNowTicks() );
    const timer_tick_t nAfterTicks = Cy_TimerNowTicks();

    REQUIRE( flNowSeconds >= Cy_TimerTicksToSeconds( nBeforeTicks ) );
    REQUIRE( flNowSeconds <= Cy_TimerTicksToSeconds( nAfterTicks ) );
}

TEST_CASE( "Timer elapsed time increases after sleeping", "[CypherCommon][Tier0][Timer]" )
{
    const timer_tick_t nStartTicks = Cy_TimerNowTicks();
    Cy_ThreadSleepMs( 1u );
    const timer_tick_t nEndTicks = Cy_TimerNowTicks();

    REQUIRE( nEndTicks > nStartTicks );
    REQUIRE( Cy_TimerElapsedSeconds( nStartTicks, nEndTicks ) > 0.0 );
}

TEST_CASE( "Timer object stores begin and end tick samples", "[CypherCommon][Tier0][Timer]" )
{
    cy_timer_t timer = {};

    REQUIRE_FALSE( Cy_TimerEnd( &timer ) );
    REQUIRE( Cy_TimerBegin( &timer ) );
    REQUIRE( timer.isRunning );
    Cy_ThreadSleepMs( 1u );
    REQUIRE( Cy_TimerGetTicks( &timer ) > 0u );
    REQUIRE( Cy_TimerEnd( &timer ) );

    REQUIRE( timer.nEndTicks > timer.nStartTicks );
    REQUIRE( Cy_TimerGetTicks( &timer ) > 0u );
    REQUIRE( Cy_TimerGetMilliseconds( &timer ) > 0.0 );
}

TEST_CASE( "Timer initialization is stable across concurrent callers", "[CypherCommon][Tier0][Timer]" )
{
    Cy_TimerShutdown();
    constexpr usize nThreadCount = 8u;
    std::array<std::thread, nThreadCount> threads;
    std::array<bool_t, nThreadCount> results{};

    for ( usize nIndex = 0u; nIndex < nThreadCount; ++nIndex ) {
        threads[nIndex] = std::thread( [&, nIndex]() {
            results[nIndex] = Cy_TimerInit();
        } );
    }
    for ( std::thread &thread : threads ) {
        thread.join();
    }

    for ( bool_t result : results ) {
        REQUIRE( result );
    }
    REQUIRE( Cy_TimerIsInitialized() );
    REQUIRE( Cy_TimerGetFrequency() != 0u );
}

TEST_CASE( "Timer deadlines saturate and report reached state", "[CypherCommon][Tier0][Timer]" )
{
    const timer_tick_t nNow = Cy_TimerNowTicks();
    const timer_tick_t nImmediateDeadline = Cy_TimerDeadlineAfterTicks( 0u );
    REQUIRE( nImmediateDeadline >= nNow );
    REQUIRE( Cy_TimerHasReached( nImmediateDeadline ) );
    REQUIRE( Cy_TimerDeadlineAfterTicks( CY_U64_MAX ) == CY_U64_MAX );
}
