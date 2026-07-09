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

using namespace cypher::common;

TEST_CASE( "Timer lifecycle initializes cached native frequency", "[CypherCommon][Tier0][Timer]" )
{
    REQUIRE( Timer_Init() == CY_TRUE );
    REQUIRE( Timer_IsInitialized() == CY_TRUE );
    REQUIRE( Timer_GetFrequency() > 0 );
}

TEST_CASE( "Timer conversion helpers use native frequency", "[CypherCommon][Tier0][Timer]" )
{
    REQUIRE( Timer_Init() == CY_TRUE );

    const timer_frequency_t nFrequency = Timer_GetFrequency();

    REQUIRE( Timer_TicksToSeconds( 0 ) == 0.0 );
    REQUIRE( Timer_TicksToSeconds( nFrequency ) == 1.0 );
    REQUIRE( Timer_TicksToMilliseconds( nFrequency ) == 1000.0 );
    REQUIRE( Timer_TicksToMicroseconds( nFrequency ) == 1000000.0 );
    REQUIRE( Timer_TicksToNanoseconds( nFrequency ) == 1000000000.0 );
    REQUIRE( Timer_ElapsedSeconds( nFrequency, nFrequency * 3 ) == 2.0 );
}

TEST_CASE( "Timer_NowTicks returns monotonic nondecreasing ticks", "[CypherCommon][Tier0][Timer]" )
{
    const timer_tick_t nStartTicks = Timer_NowTicks();
    const timer_tick_t nEndTicks = Timer_NowTicks();

    REQUIRE( nEndTicks >= nStartTicks );
}

TEST_CASE( "Timer elapsed conversion follows current tick samples", "[CypherCommon][Tier0][Timer]" )
{
    const timer_tick_t nBeforeTicks = Timer_NowTicks();
    const f64 flNowSeconds = Timer_TicksToSeconds( Timer_NowTicks() );
    const timer_tick_t nAfterTicks = Timer_NowTicks();

    REQUIRE( flNowSeconds >= Timer_TicksToSeconds( nBeforeTicks ) );
    REQUIRE( flNowSeconds <= Timer_TicksToSeconds( nAfterTicks ) );
}

TEST_CASE( "Timer elapsed time increases after sleeping", "[CypherCommon][Tier0][Timer]" )
{
    const timer_tick_t nStartTicks = Timer_NowTicks();
    Cy_ThreadSleepMs( 1u );
    const timer_tick_t nEndTicks = Timer_NowTicks();

    REQUIRE( nEndTicks > nStartTicks );
    REQUIRE( Timer_ElapsedSeconds( nStartTicks, nEndTicks ) > 0.0 );
}

TEST_CASE( "Timer object stores begin and end tick samples", "[CypherCommon][Tier0][Timer]" )
{
    cy_timer_t timer = {};

    Timer_Begin( &timer );
    Cy_ThreadSleepMs( 1u );
    Timer_End( &timer );

    REQUIRE( timer.nEndTicks > timer.nStartTicks );
    REQUIRE( Timer_GetTicks( &timer ) > 0 );
    REQUIRE( Timer_GetMilliseconds( &timer ) > 0.0 );
}
