//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_PerformanceCounter_Tests.cpp
//  Purpose: Tests the Tier0 performance-counter compatibility surface.
//  Details: Ensures counter terminology remains a zero-policy adapter over the
//           canonical monotonic timer instead of introducing a second clock.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_PerformanceCounter.h"
#include "CypherCommon_Timer.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "PerformanceCounter shares Timer frequency and conversion", "[CypherCommon][Tier0][PerformanceCounter]" )
{
    REQUIRE( Cy_TimerInit() );
    const u64 nFrequency = Cy_PerformanceCounterFrequency();

    REQUIRE( nFrequency == Cy_TimerGetFrequency() );
    REQUIRE( Cy_PerformanceCounterToSeconds( nFrequency ) == 1.0 );
}

TEST_CASE( "PerformanceCounter is monotonic", "[CypherCommon][Tier0][PerformanceCounter]" )
{
    const performance_counter_t nFirst = Cy_PerformanceCounterNow();
    const performance_counter_t nSecond = Cy_PerformanceCounterNow();
    REQUIRE( nSecond >= nFirst );
}
