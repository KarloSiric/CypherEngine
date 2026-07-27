//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_CPUMonitoring_Tests.cpp
//  Purpose: Tests caller-owned Tier0 CPU monitoring state and sample contracts.
//  Details: These tests validate lifecycle, output initialization, independent
//           baselines, elapsed intervals, and bounded usage percentages.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CPUMonitoring.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

using namespace cypher::common;

TEST_CASE( "CPU monitor rejects invalid state and clears valid output", "[CypherCommon][Tier0][CPUMonitoring]" )
{
    REQUIRE_FALSE( Cy_CPUMonitorInit( nullptr ) );
    REQUIRE_FALSE( Cy_CPUMonitorReset( nullptr ) );

    cy_cpu_monitor_sample_t sample{};
    sample.totalUsagePercent = 42.0f;
    REQUIRE_FALSE( Cy_CPUMonitorSample( nullptr, &sample ) );
    REQUIRE( sample.totalUsagePercent == 0.0f );

    cy_cpu_monitor_t monitor{};
    REQUIRE_FALSE( Cy_CPUMonitorSample( &monitor, &sample ) );
    REQUIRE_FALSE( Cy_CPUMonitorSample( &monitor, nullptr ) );
}

TEST_CASE( "CPU monitor samples a bounded interval", "[CypherCommon][Tier0][CPUMonitoring]" )
{
    cy_cpu_monitor_t monitor{};
    REQUIRE( Cy_CPUMonitorInit( &monitor ) );

    std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );

    cy_cpu_monitor_sample_t sample{};
    REQUIRE( Cy_CPUMonitorSample( &monitor, &sample ) );
    REQUIRE( sample.nLogicalThreadCount >= 1u );
    REQUIRE( sample.intervalSeconds > 0.0 );
    REQUIRE( sample.totalUsagePercent >= 0.0f );
    REQUIRE( sample.totalUsagePercent <= 100.0f );
    REQUIRE( sample.processUsagePercent >= 0.0f );
    REQUIRE( sample.processUsagePercent <= 100.0f );
    if ( sample.hasSystemUsage ) {
        REQUIRE( sample.totalUsagePercent >= 0.0f );
        REQUIRE( sample.totalUsagePercent <= 100.0f );
    }
    if ( sample.hasProcessUsage ) {
        REQUIRE( sample.processUsagePercent >= 0.0f );
        REQUIRE( sample.processUsagePercent <= 100.0f );
    }
}

TEST_CASE( "CPU monitors own independent baselines", "[CypherCommon][Tier0][CPUMonitoring]" )
{
    cy_cpu_monitor_t first{};
    cy_cpu_monitor_t second{};
    REQUIRE( Cy_CPUMonitorInit( &first ) );

    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    REQUIRE( Cy_CPUMonitorInit( &second ) );
    REQUIRE( first.nPreviousWallTicks != second.nPreviousWallTicks );

    REQUIRE( Cy_CPUMonitorReset( &first ) );
    REQUIRE( first.isInitialized );
    REQUIRE( second.isInitialized );
}
