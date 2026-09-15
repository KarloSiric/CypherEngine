//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherSystem/CypherSystem_EventQueue_Tests.cpp
//  Purpose: Tests the fixed-capacity CypherSystem event queue.
//  Details: These checks protect FIFO order, empty polling, explicit clearing,
//           and the retain-newest overflow policy used by native event pumps.
//
//  History:
//  - Created by Karlo Siric on 2026-08-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSystem_Public.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::engine::sys;

namespace
{

sys_event_t MakeKeyEvent( const cypher::engine::common::u64 timestamp )
{
    sys_event_t event{};
    event.type = sys_event_type_t::KEY;
    event.timestampNanoseconds = timestamp;
    event.payload.key.windowId = 1u;
    event.payload.key.key = sys_key_t::SPACE;
    event.payload.key.action = sys_input_action_t::PRESSED;
    event.payload.key.modifiers = SYS_KEYMODIFIER_NONE;
    return event;
}

} // namespace

TEST_CASE( "System event queue preserves FIFO order", "[CypherSystem][EventQueue]" )
{
    Sys_ClearEvents();

    REQUIRE( Sys_QueueEvent( MakeKeyEvent( 10u ) ) );
    REQUIRE( Sys_QueueEvent( MakeKeyEvent( 20u ) ) );
    REQUIRE( Sys_QueueEvent( MakeKeyEvent( 30u ) ) );
    REQUIRE( Sys_EventCount() == 3u );

    sys_event_t event{};
    REQUIRE( Sys_PollEvent( event ) );
    REQUIRE( event.timestampNanoseconds == 10u );
    REQUIRE( Sys_PollEvent( event ) );
    REQUIRE( event.timestampNanoseconds == 20u );
    REQUIRE( Sys_PollEvent( event ) );
    REQUIRE( event.timestampNanoseconds == 30u );
    REQUIRE( Sys_EventCount() == 0u );
}

TEST_CASE( "System event queue clears output when polling an empty queue", "[CypherSystem][EventQueue]" )
{
    Sys_ClearEvents();

    sys_event_t event = MakeKeyEvent( 42u );
    REQUIRE_FALSE( Sys_PollEvent( event ) );
    REQUIRE( event.type == sys_event_type_t::NONE );
    REQUIRE( event.timestampNanoseconds == 0u );
}

TEST_CASE( "System event queue rejects malformed normalized events", "[CypherSystem][EventQueue]" )
{
    Sys_ClearEvents();
    const cypher::engine::common::u64 droppedBefore = Sys_DroppedEventCount();

    sys_event_t event{};
    REQUIRE_FALSE( Sys_QueueEvent( event ) );

    event.type = sys_event_type_t::COUNT;
    REQUIRE_FALSE( Sys_QueueEvent( event ) );

    event = MakeKeyEvent( 1u );
    event.payload.key.key = sys_key_t::COUNT;
    REQUIRE_FALSE( Sys_QueueEvent( event ) );

    event = MakeKeyEvent( 2u );
    event.payload.key.modifiers = CYPHER_BIT32( 31 );
    REQUIRE_FALSE( Sys_QueueEvent( event ) );

    event = {};
    event.type = sys_event_type_t::TEXT_INPUT;
    event.payload.textInput.windowId = 1u;
    event.payload.textInput.byteCount = 1u;
    event.payload.textInput.utf8[0] = 'x';
    event.payload.textInput.utf8[1] = 'x'; // Missing terminator at byteCount.
    REQUIRE_FALSE( Sys_QueueEvent( event ) );

    REQUIRE( Sys_EventCount() == 0u );
    REQUIRE( Sys_DroppedEventCount() == droppedBefore );
}

TEST_CASE( "System event queue retains newest events after overflow", "[CypherSystem][EventQueue]" )
{
    Sys_ClearEvents();
    const cypher::engine::common::u64 droppedBefore = Sys_DroppedEventCount();

    for ( cypher::engine::common::u32 eventIndex = 0u;
          eventIndex < SYS_EVENT_QUEUE_CAPACITY;
          ++eventIndex ) {
        REQUIRE( Sys_QueueEvent( MakeKeyEvent( eventIndex ) ) );
    }

    REQUIRE_FALSE( Sys_QueueEvent( MakeKeyEvent( SYS_EVENT_QUEUE_CAPACITY ) ) );
    REQUIRE( Sys_EventCount() == SYS_EVENT_QUEUE_CAPACITY );
    REQUIRE( Sys_DroppedEventCount() == droppedBefore + 1u );

    sys_event_t event{};
    REQUIRE( Sys_PollEvent( event ) );
    REQUIRE( event.timestampNanoseconds == 1u );

    cypher::engine::common::u64 lastTimestamp = event.timestampNanoseconds;
    while ( Sys_PollEvent( event ) ) {
        lastTimestamp = event.timestampNanoseconds;
    }
    REQUIRE( lastTimestamp == SYS_EVENT_QUEUE_CAPACITY );
    REQUIRE( Sys_EventCount() == 0u );
    REQUIRE( event.type == sys_event_type_t::NONE );
}

TEST_CASE( "System event queue discards pending events when cleared", "[CypherSystem][EventQueue]" )
{
    Sys_ClearEvents();
    REQUIRE( Sys_QueueEvent( MakeKeyEvent( 1u ) ) );
    REQUIRE( Sys_QueueEvent( MakeKeyEvent( 2u ) ) );

    Sys_ClearEvents();
    REQUIRE( Sys_EventCount() == 0u );

    sys_event_t event{};
    REQUIRE_FALSE( Sys_PollEvent( event ) );
}
