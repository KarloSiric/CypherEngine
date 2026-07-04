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

using namespace cypher::common;

TEST_CASE( "Thread helper reports at least one logical thread", "[CypherCommon][Tier0][Thread]" )
{
    REQUIRE( GetLogicalThreadCount() >= 1u );
}

TEST_CASE( "Thread id hash is stable inside the same thread", "[CypherCommon][Tier0][Thread]" )
{
    const u64 nThreadHashA = GetCurrentThreadIdHash();
    const u64 nThreadHashB = GetCurrentThreadIdHash();

    REQUIRE( nThreadHashA == nThreadHashB );
}

TEST_CASE( "Thread yield returns control to the caller", "[CypherCommon][Tier0][Thread]" )
{
    ThreadYield();
    SUCCEED( "ThreadYield returned." );
}

TEST_CASE( "Thread sleep waits for observable monotonic time", "[CypherCommon][Tier0][Thread]" )
{
    const timer_tick_t nStartTicks = Timer_NowTicks();
    ThreadSleepMs( 1u );
    const timer_tick_t nEndTicks = Timer_NowTicks();

    REQUIRE( nEndTicks > nStartTicks );
}
