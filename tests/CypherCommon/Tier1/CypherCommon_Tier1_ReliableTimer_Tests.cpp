//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_ReliableTimer_Tests.cpp
//  Purpose: Tests RTT estimation and retransmission timeout policy.
//  Details: Configuration validation, RFC smoothing, deadlines, reset, and bounded
//           exponential backoff are verified with deterministic monotonic times.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ReliableTimer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "ReliableTimer validates and smooths RTT samples",
           "[CypherCommon][Tier1][ReliableTimer]" )
{
    reliable_timer_t timer{};
    REQUIRE( ReliableTimer_Init( &timer, {} ) );
    REQUIRE( timer.flRtoSeconds == Catch::Approx( 1.0 ) );
    REQUIRE( ReliableTimer_AddRttSample( &timer, 0.100 ) );
    REQUIRE( timer.flSmoothedRttSeconds == Catch::Approx( 0.100 ) );
    REQUIRE( timer.flRttVariationSeconds == Catch::Approx( 0.050 ) );
    REQUIRE( timer.flRtoSeconds == Catch::Approx( 0.300 ) );

    REQUIRE( ReliableTimer_AddRttSample( &timer, 0.140 ) );
    REQUIRE( timer.flSmoothedRttSeconds == Catch::Approx( 0.105 ) );
    REQUIRE( timer.flRttVariationSeconds == Catch::Approx( 0.0475 ) );
    REQUIRE( timer.flRtoSeconds == Catch::Approx( 0.295 ) );
    REQUIRE_FALSE( ReliableTimer_AddRttSample( &timer, 0.0 ) );
}

TEST_CASE( "ReliableTimer arms and applies bounded timeout backoff",
           "[CypherCommon][Tier1][ReliableTimer]" )
{
    reliable_timer_t timer{};
    const reliable_timer_config_t config{ 1.0, 0.05, 2.5, 0.001 };
    REQUIRE( ReliableTimer_Init( &timer, config ) );

    ReliableTimer_Arm( &timer, 10.0 );
    REQUIRE_FALSE( ReliableTimer_HasExpired( &timer, 10.999 ) );
    REQUIRE( ReliableTimer_HasExpired( &timer, 11.0 ) );

    ReliableTimer_OnTimeout( &timer, 11.0 );
    REQUIRE( timer.flRtoSeconds == Catch::Approx( 2.0 ) );
    REQUIRE( timer.flDeadlineSeconds == Catch::Approx( 13.0 ) );
    ReliableTimer_OnTimeout( &timer, 13.0 );
    REQUIRE( timer.flRtoSeconds == Catch::Approx( 2.5 ) );
    REQUIRE( timer.nBackoffCount == 2u );

    ReliableTimer_Disarm( &timer );
    REQUIRE_FALSE( ReliableTimer_HasExpired( &timer, 100.0 ) );
    ReliableTimer_Reset( &timer );
    REQUIRE( timer.flRtoSeconds == Catch::Approx( 1.0 ) );
    REQUIRE_FALSE( timer.bHasSample );
}

TEST_CASE( "ReliableTimer rejects incoherent configuration",
           "[CypherCommon][Tier1][ReliableTimer]" )
{
    reliable_timer_t timer{};
    reliable_timer_config_t invalid{};
    invalid.flMinRtoSeconds = 2.0;
    invalid.flMaxRtoSeconds = 1.0;
    REQUIRE_FALSE( ReliableTimer_Init( &timer, invalid ) );
}
