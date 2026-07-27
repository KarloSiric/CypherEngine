//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Profile_Tests.cpp
//  Purpose: Tests the Tier0 synchronous profiling event spine.
//  Details: These tests cover disabled behavior, zone/frame/counter events,
//           timestamps, thread identity, stat publication, and reentrancy drops.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Profile.h"

#include "CypherCommon_Stats.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace cypher::common;

namespace
{

struct profile_capture_t {
    std::array<profile_event_t, 32> events{};
    usize nCount = 0u;
    bool_t emitNestedCounter = CY_FALSE;
};

void CYPHER_CALL CaptureProfileEvent(
    const profile_event_t &event,
    void *pUserData ) noexcept
{
    auto *pCapture = static_cast<profile_capture_t *>( pUserData );
    if ( pCapture->nCount < pCapture->events.size() ) {
        pCapture->events[pCapture->nCount] = event;
        ++pCapture->nCount;
    }

    if ( pCapture->emitNestedCounter ) {
        pCapture->emitNestedCounter = CY_FALSE;
        static_cast<void>( Cy_ProfileCounterSet( "nested.counter", 1 ) );
    }
}

} // namespace

TEST_CASE( "Profile disabled path emits no tokens or events", "[CypherCommon][Tier0][Profile]" )
{
    profile_capture_t capture{};
    Cy_ProfileSetSink( CaptureProfileEvent, &capture );
    Cy_ProfileSetEnabled( CY_FALSE );
    Cy_ProfileResetState();

    const profile_zone_desc_t zone{
        "disabled",
        "Test",
        CY_SOURCE_LOCATION,
        PROFILE_FLAG_CPU
    };
    REQUIRE( Cy_ProfileBeginZone( &zone ) == CY_PROFILE_INVALID_TOKEN );
    REQUIRE_FALSE( Cy_ProfileEndZone( 1u ) );
    REQUIRE_FALSE( Cy_ProfileCounterSet( "counter", 1 ) );
    REQUIRE( Cy_ProfileFrameBegin() == 0u );
    REQUIRE_FALSE( Cy_ProfileFrameEnd() );
    REQUIRE( capture.nCount == 0u );
}

TEST_CASE( "Profile emits coherent zone counter and frame events", "[CypherCommon][Tier0][Profile]" )
{
    Cy_StatsClearRegistry();
    profile_capture_t capture{};
    Cy_ProfileSetSink( CaptureProfileEvent, &capture );
    Cy_ProfileResetState();
    Cy_ProfileSetEnabled( CY_TRUE );

    const profile_zone_desc_t zone{
        "render.world",
        "Renderer",
        CY_SOURCE_LOCATION,
        PROFILE_FLAG_CPU
    };
    const profile_token_t token = Cy_ProfileBeginZone( &zone );
    REQUIRE( token != CY_PROFILE_INVALID_TOKEN );
    REQUIRE( Cy_ProfileEndZone( token ) );
    REQUIRE( Cy_ProfileCounterSet( "draw.calls", 10 ) );
    REQUIRE( Cy_ProfileCounterAdd( "draw.calls", 5 ) );
    REQUIRE( Cy_ProfileFrameBegin() == 1u );
    REQUIRE( Cy_ProfileFrameEnd() );

    REQUIRE( capture.nCount == 6u );
    REQUIRE( capture.events[0].type == profile_event_type_t::ZoneBegin );
    REQUIRE( capture.events[0].token == token );
    REQUIRE( capture.events[1].type == profile_event_type_t::ZoneEnd );
    REQUIRE( capture.events[1].token == token );
    REQUIRE(
        capture.events[1].nTimestampTicks >=
        capture.events[0].nTimestampTicks );
    REQUIRE( capture.events[0].nThreadId != CY_THREAD_INVALID_ID );
    REQUIRE( capture.events[3].nCounterValue == 15 );
    REQUIRE( capture.events[4].nFrameIndex == 1u );
    REQUIRE( capture.events[5].nFrameIndex == 1u );

    stat_value_t counter{};
    REQUIRE( Cy_StatsGetByName( "draw.calls", &counter ) );
    REQUIRE( counter.type == stat_value_type_t::I64 );
    REQUIRE( counter.i64Value == 15 );

    const profile_state_t state = Cy_ProfileGetState();
    REQUIRE( state.nFrameIndex == 1u );
    REQUIRE( state.nEmittedEventCount == 6u );
    REQUIRE( state.nDroppedReentrantEventCount == 0u );
    REQUIRE( state.isEnabled );
    REQUIRE( state.hasSink );
}

TEST_CASE( "Profile suppresses recursive sink emission", "[CypherCommon][Tier0][Profile]" )
{
    Cy_StatsClearRegistry();
    profile_capture_t capture{};
    capture.emitNestedCounter = CY_TRUE;
    Cy_ProfileSetSink( CaptureProfileEvent, &capture );
    Cy_ProfileResetState();
    Cy_ProfileSetEnabled( CY_TRUE );

    const profile_zone_desc_t zone{
        "outer",
        "Test",
        CY_SOURCE_LOCATION,
        PROFILE_FLAG_CPU
    };
    REQUIRE( Cy_ProfileBeginZone( &zone ) != CY_PROFILE_INVALID_TOKEN );

    const profile_state_t state = Cy_ProfileGetState();
    REQUIRE( capture.nCount == 1u );
    REQUIRE( state.nEmittedEventCount == 1u );
    REQUIRE( state.nDroppedReentrantEventCount == 1u );

    Cy_ProfileSetEnabled( CY_FALSE );
    Cy_ProfileSetSink( nullptr );
}
