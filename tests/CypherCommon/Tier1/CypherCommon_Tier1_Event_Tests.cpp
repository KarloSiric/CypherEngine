//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Event_Tests.cpp
//  Purpose: Tests synchronous event dispatch and mutation rules.
//  Details: Covers stable priority order, payload borrowing, one-shot listeners,
//           unsubscribe/subscribe/clear during emit, nesting rejection, and failure.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_Event.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

constexpr event_id_t TEST_EVENT = 0xC1A0u;

struct record_event_t {
    u32 nValue;
    u32 *pOrder;
    usize *pCount;
    u64 nExpectedSender;
    byte nExpectedByte;
};

struct unsubscribe_event_t {
    event_bus_t *pBus;
    event_subscription_t target;
    usize cCalls;
};

struct subscribe_event_t {
    event_bus_t *pBus;
    record_event_t *pRecord;
    event_subscription_t subscription;
    usize cCalls;
};

struct clear_event_t {
    event_bus_t *pBus;
    usize cCalls;
};

struct nested_event_t {
    event_bus_t *pBus;
    bool_t bNestedResult;
    usize cNestedCalls;
};

struct event_allocator_state_t {
    bool_t bFailAllocations{ CY_FALSE };
};

u32 g_eventAssertCount = 0u;

void RecordEvent(
    event_id_t,
    const event_payload_t &payload,
    void *pUserData ) noexcept
{
    record_event_t &record = *static_cast<record_event_t *>( pUserData );
    record.pOrder[*record.pCount] = record.nValue;
    ++*record.pCount;
    if ( record.nExpectedSender != 0u ) {
        CY_ASSERT( payload.nSenderId == record.nExpectedSender );
        CY_ASSERT( payload.data.cbSize == 1u );
        CY_ASSERT( payload.data.pData[0] == record.nExpectedByte );
    }
}

void UnsubscribeEvent(
    event_id_t,
    const event_payload_t &,
    void *pUserData ) noexcept
{
    unsubscribe_event_t &event =
        *static_cast<unsubscribe_event_t *>( pUserData );
    ++event.cCalls;
    static_cast<void>( EventBus_Unsubscribe( event.pBus, event.target ) );
}

void SubscribeEvent(
    event_id_t,
    const event_payload_t &,
    void *pUserData ) noexcept
{
    subscribe_event_t &event =
        *static_cast<subscribe_event_t *>( pUserData );
    ++event.cCalls;
    if ( !Cy_Handle32IsValid( event.subscription ) ) {
        event.subscription = EventBus_Subscribe(
            event.pBus,
            TEST_EVENT,
            -10,
            EVENT_SUBSCRIPTION_FLAG_NONE,
            RecordEvent,
            event.pRecord );
    }
}

void ClearEvent(
    event_id_t,
    const event_payload_t &,
    void *pUserData ) noexcept
{
    clear_event_t &event = *static_cast<clear_event_t *>( pUserData );
    ++event.cCalls;
    EventBus_Clear( event.pBus );
}

void NestedEvent(
    event_id_t eventId,
    const event_payload_t &payload,
    void *pUserData ) noexcept
{
    nested_event_t &event = *static_cast<nested_event_t *>( pUserData );
    event.bNestedResult = EventBus_TryEmit(
        event.pBus,
        eventId,
        payload,
        &event.cNestedCalls );
}

void NoOpEvent( event_id_t, const event_payload_t &, void * ) noexcept
{
}

assert_action_t CaptureEventAssert( const assert_info_t & ) noexcept
{
    ++g_eventAssertCount;
    return assert_action_t::Continue;
}

void *EventAllocate(
    void *pUserData,
    usize cbSize,
    usize nAlignment ) noexcept
{
    auto *pState = static_cast<event_allocator_state_t *>( pUserData );
    return pState->bFailAllocations
        ? nullptr
        : Allocator_Allocate( Allocator_GetSystem(), cbSize, nAlignment );
}

void EventFree(
    void *,
    void *pMemory,
    usize cbSize,
    usize nAlignment ) noexcept
{
    Allocator_Free( Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

} // namespace

TEST_CASE( "EventBus dispatches by priority with stable equal-priority order",
           "[CypherCommon][Tier1][Event]" )
{
    event_bus_t *pBus = EventBus_Create( { Allocator_GetSystem(), 4u } );
    REQUIRE( pBus != nullptr );
    REQUIRE( EventBus_IsValid( pBus ) );
    REQUIRE_FALSE( EventBus_IsValid( nullptr ) );

    u32 order[6]{};
    usize nOrder = 0u;
    record_event_t low{ 1u, order, &nOrder, 77u, 0xABu };
    record_event_t highFirst{ 2u, order, &nOrder, 77u, 0xABu };
    record_event_t highSecond{ 3u, order, &nOrder, 77u, 0xABu };
    REQUIRE( Cy_Handle32IsValid( EventBus_Subscribe(
        pBus,
        TEST_EVENT,
        0,
        EVENT_SUBSCRIPTION_FLAG_NONE,
        RecordEvent,
        &low ) ) );
    REQUIRE( Cy_Handle32IsValid( EventBus_Subscribe(
        pBus,
        TEST_EVENT,
        10,
        EVENT_SUBSCRIPTION_FLAG_ONCE,
        RecordEvent,
        &highFirst ) ) );
    REQUIRE( Cy_Handle32IsValid( EventBus_Subscribe(
        pBus,
        TEST_EVENT,
        10,
        EVENT_SUBSCRIPTION_FLAG_NONE,
        RecordEvent,
        &highSecond ) ) );

    const byte payloadByte = 0xABu;
    const event_payload_t payload{
        BinaryBlock_FromData( &payloadByte, 1u ),
        77u
    };
    REQUIRE( EventBus_Emit( pBus, TEST_EVENT, payload ) == 3u );
    REQUIRE( order[0] == 2u );
    REQUIRE( order[1] == 3u );
    REQUIRE( order[2] == 1u );
    REQUIRE( EventBus_SubscriptionCount( pBus ) == 2u );

    REQUIRE( EventBus_Emit( pBus, TEST_EVENT, payload ) == 2u );
    REQUIRE( order[3] == 3u );
    REQUIRE( order[4] == 1u );
    EventBus_Destroy( pBus );
}

TEST_CASE( "EventBus unsubscribe during emit cancels a pending callback",
           "[CypherCommon][Tier1][Event]" )
{
    event_bus_t *pBus = EventBus_Create( { Allocator_GetSystem(), 2u } );
    REQUIRE( pBus != nullptr );

    u32 order[1]{};
    usize nOrder = 0u;
    record_event_t low{ 9u, order, &nOrder, 0u, 0u };
    const event_subscription_t lowHandle = EventBus_Subscribe(
        pBus,
        TEST_EVENT,
        0,
        EVENT_SUBSCRIPTION_FLAG_NONE,
        RecordEvent,
        &low );
    unsubscribe_event_t high{ pBus, lowHandle, 0u };
    REQUIRE( Cy_Handle32IsValid( EventBus_Subscribe(
        pBus,
        TEST_EVENT,
        10,
        EVENT_SUBSCRIPTION_FLAG_NONE,
        UnsubscribeEvent,
        &high ) ) );

    REQUIRE( EventBus_Emit( pBus, TEST_EVENT, {} ) == 1u );
    REQUIRE( high.cCalls == 1u );
    REQUIRE( nOrder == 0u );
    REQUIRE_FALSE( EventBus_IsSubscribed( pBus, lowHandle ) );
    EventBus_Destroy( pBus );
}

TEST_CASE( "EventBus defers callback subscriptions until the next emission",
           "[CypherCommon][Tier1][Event]" )
{
    event_bus_t *pBus = EventBus_Create( { Allocator_GetSystem(), 2u } );
    REQUIRE( pBus != nullptr );

    u32 order[1]{};
    usize nOrder = 0u;
    record_event_t deferred{ 42u, order, &nOrder, 0u, 0u };
    subscribe_event_t subscribe{
        pBus,
        &deferred,
        CY_EVENT_SUBSCRIPTION_INVALID,
        0u
    };
    REQUIRE( Cy_Handle32IsValid( EventBus_Subscribe(
        pBus,
        TEST_EVENT,
        10,
        EVENT_SUBSCRIPTION_FLAG_NONE,
        SubscribeEvent,
        &subscribe ) ) );

    REQUIRE( EventBus_Emit( pBus, TEST_EVENT, {} ) == 1u );
    REQUIRE( nOrder == 0u );
    REQUIRE( Cy_Handle32IsValid( subscribe.subscription ) );
    REQUIRE( EventBus_Emit( pBus, TEST_EVENT, {} ) == 2u );
    REQUIRE( nOrder == 1u );
    REQUIRE( order[0] == 42u );
    EventBus_Destroy( pBus );
}

TEST_CASE( "EventBus clear during emit cancels all remaining callbacks",
           "[CypherCommon][Tier1][Event]" )
{
    event_bus_t *pBus = EventBus_Create( { Allocator_GetSystem(), 2u } );
    REQUIRE( pBus != nullptr );
    clear_event_t clear{ pBus, 0u };
    u32 order[1]{};
    usize nOrder = 0u;
    record_event_t low{ 7u, order, &nOrder, 0u, 0u };

    REQUIRE( Cy_Handle32IsValid( EventBus_Subscribe(
        pBus, TEST_EVENT, 10, 0u, ClearEvent, &clear ) ) );
    REQUIRE( Cy_Handle32IsValid( EventBus_Subscribe(
        pBus, TEST_EVENT, 0, 0u, RecordEvent, &low ) ) );
    REQUIRE( EventBus_Emit( pBus, TEST_EVENT, {} ) == 1u );
    REQUIRE( clear.cCalls == 1u );
    REQUIRE( nOrder == 0u );
    REQUIRE( EventBus_SubscriptionCount( pBus ) == 0u );
    EventBus_Destroy( pBus );
}

TEST_CASE( "EventBus rejects nested emission without corrupting dispatch",
           "[CypherCommon][Tier1][Event]" )
{
    event_bus_t *pBus = EventBus_Create( { Allocator_GetSystem(), 1u } );
    REQUIRE( pBus != nullptr );
    nested_event_t nested{ pBus, CY_TRUE, CY_INVALID_SIZE };
    REQUIRE( Cy_Handle32IsValid( EventBus_Subscribe(
        pBus, TEST_EVENT, 0, 0u, NestedEvent, &nested ) ) );

    g_eventAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureEventAssert );
    REQUIRE( EventBus_Emit( pBus, TEST_EVENT, {} ) == 1u );
    Cy_AssertSetHandler( pPreviousHandler );

    REQUIRE_FALSE( nested.bNestedResult );
    REQUIRE( nested.cNestedCalls == 0u );
    REQUIRE(
        g_eventAssertCount ==
        static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
    EventBus_Destroy( pBus );
}

TEST_CASE( "EventBus subscription growth failure preserves existing listeners",
           "[CypherCommon][Tier1][Event]" )
{
    event_allocator_state_t state{};
    const allocator_t allocator{
        EventAllocate,
        nullptr,
        EventFree,
        &state
    };
    event_bus_t *pBus = EventBus_Create( { &allocator, 1u } );
    REQUIRE( pBus != nullptr );
    const event_subscription_t first = EventBus_Subscribe(
        pBus, TEST_EVENT, 0, 0u, NoOpEvent, nullptr );
    REQUIRE( Cy_Handle32IsValid( first ) );

    state.bFailAllocations = CY_TRUE;
    const event_subscription_t failed = EventBus_Subscribe(
        pBus, TEST_EVENT, 1, 0u, NoOpEvent, nullptr );
    REQUIRE_FALSE( Cy_Handle32IsValid( failed ) );
    REQUIRE( EventBus_SubscriptionCount( pBus ) == 1u );
    REQUIRE( EventBus_IsSubscribed( pBus, first ) );

    state.bFailAllocations = CY_FALSE;
    EventBus_Destroy( pBus );
}
