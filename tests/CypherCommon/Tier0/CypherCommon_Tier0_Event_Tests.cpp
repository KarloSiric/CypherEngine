//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Event_Tests.cpp
//  Purpose: Tests CypherCommon Tier0 event synchronization behavior.
//  Details: These tests validate manual-reset and auto-reset waits used by
//           worker wakeups, async IO completion, and runtime shutdown.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Event.h"
#include "CypherCommon_Thread.h"

#include <catch2/catch_test_macros.hpp>

#include <thread>

using namespace cypher::common;

TEST_CASE( "Event initializes, resets, and times out when unsignaled", "[CypherCommon][Tier0][Event]" )
{
    cy_event_t event{};

    REQUIRE( Cy_EventInit( &event, cy_event_reset_mode_t::Manual, CY_FALSE ) );
    REQUIRE( Cy_EventIsInitialized( &event ) );
    REQUIRE_FALSE( Cy_EventWaitTimeoutMs( &event, 1u ) );

    Cy_EventSignal( &event );
    REQUIRE( Cy_EventWaitTimeoutMs( &event, 1u ) );

    Cy_EventReset( &event );
    REQUIRE_FALSE( Cy_EventWaitTimeoutMs( &event, 1u ) );

    Cy_EventShutdown( &event );
    REQUIRE_FALSE( Cy_EventIsInitialized( &event ) );
}

TEST_CASE( "Auto-reset event releases one waiter per signal", "[CypherCommon][Tier0][Event]" )
{
    cy_event_t event{};

    REQUIRE( Cy_EventInit( &event, cy_event_reset_mode_t::Auto, CY_TRUE ) );
    REQUIRE( Cy_EventWaitTimeoutMs( &event, 1u ) );
    REQUIRE_FALSE( Cy_EventWaitTimeoutMs( &event, 1u ) );

    Cy_EventSignal( &event );
    REQUIRE( Cy_EventWaitTimeoutMs( &event, 1u ) );
    REQUIRE_FALSE( Cy_EventWaitTimeoutMs( &event, 1u ) );
}

TEST_CASE( "Event wakes a waiting worker thread", "[CypherCommon][Tier0][Event]" )
{
    cy_event_t event{};
    bool_t bWorkerWoke = CY_FALSE;

    REQUIRE( Cy_EventInit( &event, cy_event_reset_mode_t::Manual, CY_FALSE ) );

    std::thread worker( [&]() {
        bWorkerWoke = Cy_EventWait( &event );
    } );

    Cy_ThreadSleepMs( 1u );
    Cy_EventSignal( &event );
    worker.join();

    REQUIRE( bWorkerWoke );
}
