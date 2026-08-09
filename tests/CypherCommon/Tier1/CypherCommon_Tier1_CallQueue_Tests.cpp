//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_CallQueue_Tests.cpp
//  Purpose: Tests deferred callback queue behavior.
//  Details: FIFO ordering, bounded drains, tag cancellation, callback enqueueing,
//           reentrancy protection, and allocator failures cover ownership boundaries.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CallQueue.h"
#include "CypherCommon_Assert.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct record_call_t {
    u32 *pValues;
    usize *pCount;
    u32 value;
};

struct enqueue_call_t {
    call_queue_t *pQueue;
    call_queue_entry_t entry;
    bool_t bPushed;
};

struct nested_drain_call_t {
    call_queue_t *pQueue;
    usize nNestedResult;
};

struct mutate_during_drain_call_t {
    call_queue_t *pQueue;
    usize nCancelled;
};

u32 g_callQueueAssertCount = 0u;

void RecordCall( void *pUserData ) noexcept
{
    record_call_t &call = *static_cast<record_call_t *>( pUserData );
    call.pValues[*call.pCount] = call.value;
    ++*call.pCount;
}

void EnqueueCall( void *pUserData ) noexcept
{
    enqueue_call_t &call = *static_cast<enqueue_call_t *>( pUserData );
    call.bPushed = CallQueue_Push( call.pQueue, call.entry );
}

void NestedDrainCall( void *pUserData ) noexcept
{
    nested_drain_call_t &call =
        *static_cast<nested_drain_call_t *>( pUserData );
    call.nNestedResult = CallQueue_Drain( call.pQueue );
}

void MutateDuringDrainCall( void *pUserData ) noexcept
{
    mutate_during_drain_call_t &call =
        *static_cast<mutate_during_drain_call_t *>( pUserData );
    CallQueue_Clear( call.pQueue );
    call.nCancelled = CallQueue_CancelTag( call.pQueue, 99u );
}

assert_action_t CaptureCallQueueAssert( const assert_info_t & ) noexcept
{
    ++g_callQueueAssertCount;
    return assert_action_t::Continue;
}

void *FailCallQueueAllocation( void *, usize, usize ) noexcept
{
    return nullptr;
}

} // namespace

TEST_CASE( "CallQueue drains callbacks in FIFO order with a fixed limit",
           "[CypherCommon][Tier1][CallQueue]" )
{
    call_queue_t *pQueue = CallQueue_Create( Allocator_GetSystem(), 2u );
    REQUIRE( pQueue != nullptr );

    u32 values[4]{};
    usize nValues = 0u;
    record_call_t calls[]{
        { values, &nValues, 10u },
        { values, &nValues, 20u },
        { values, &nValues, 30u },
        { values, &nValues, 40u }
    };
    for ( record_call_t &call : calls ) {
        REQUIRE( CallQueue_Push( pQueue, { RecordCall, &call, 1u } ) );
    }

    REQUIRE( CallQueue_Drain( pQueue, 2u ) == 2u );
    REQUIRE( nValues == 2u );
    REQUIRE( values[0] == 10u );
    REQUIRE( values[1] == 20u );
    REQUIRE( CallQueue_Count( pQueue ) == 2u );

    REQUIRE( CallQueue_Drain( pQueue ) == 2u );
    REQUIRE( values[2] == 30u );
    REQUIRE( values[3] == 40u );
    REQUIRE( CallQueue_Count( pQueue ) == 0u );
    CallQueue_Destroy( pQueue );
}

TEST_CASE( "CallQueue cancellation preserves retained callback order",
           "[CypherCommon][Tier1][CallQueue]" )
{
    call_queue_t *pQueue = CallQueue_Create( Allocator_GetSystem(), 4u );
    REQUIRE( pQueue != nullptr );

    u32 values[4]{};
    usize nValues = 0u;
    record_call_t calls[]{
        { values, &nValues, 1u },
        { values, &nValues, 2u },
        { values, &nValues, 3u },
        { values, &nValues, 4u }
    };
    REQUIRE( CallQueue_Push( pQueue, { RecordCall, &calls[0], 7u } ) );
    REQUIRE( CallQueue_Push( pQueue, { RecordCall, &calls[1], 9u } ) );
    REQUIRE( CallQueue_Push( pQueue, { RecordCall, &calls[2], 7u } ) );
    REQUIRE( CallQueue_Push( pQueue, { RecordCall, &calls[3], 11u } ) );

    REQUIRE( CallQueue_CancelTag( pQueue, 7u ) == 2u );
    REQUIRE( CallQueue_Drain( pQueue ) == 2u );
    REQUIRE( values[0] == 2u );
    REQUIRE( values[1] == 4u );
    CallQueue_Destroy( pQueue );
}

TEST_CASE( "CallQueue leaves callback-enqueued work for the next drain",
           "[CypherCommon][Tier1][CallQueue]" )
{
    call_queue_t *pQueue = CallQueue_Create( Allocator_GetSystem(), 1u );
    REQUIRE( pQueue != nullptr );

    u32 values[1]{};
    usize nValues = 0u;
    record_call_t record{ values, &nValues, 42u };
    enqueue_call_t enqueue{
        pQueue,
        { RecordCall, &record, 0u },
        CY_FALSE
    };
    REQUIRE( CallQueue_Push( pQueue, { EnqueueCall, &enqueue, 0u } ) );
    REQUIRE( CallQueue_Drain( pQueue ) == 1u );
    REQUIRE( enqueue.bPushed );
    REQUIRE( nValues == 0u );
    REQUIRE( CallQueue_Count( pQueue ) == 1u );
    REQUIRE( CallQueue_Drain( pQueue ) == 1u );
    REQUIRE( values[0] == 42u );
    CallQueue_Destroy( pQueue );
}

TEST_CASE( "CallQueue rejects nested draining and malformed entries",
           "[CypherCommon][Tier1][CallQueue]" )
{
    call_queue_t *pQueue = CallQueue_Create( Allocator_GetSystem() );
    REQUIRE( pQueue != nullptr );

    g_callQueueAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureCallQueueAssert );

    REQUIRE_FALSE( CallQueue_Push( pQueue, {} ) );
    nested_drain_call_t nested{ pQueue, CY_INVALID_SIZE };
    REQUIRE( CallQueue_Push( pQueue, { NestedDrainCall, &nested, 0u } ) );
    REQUIRE( CallQueue_Drain( pQueue ) == 1u );
    REQUIRE( nested.nNestedResult == 0u );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_callQueueAssertCount ==
        2u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
    CallQueue_Destroy( pQueue );
}

TEST_CASE( "CallQueue creation reports allocator failure transactionally",
           "[CypherCommon][Tier1][CallQueue]" )
{
    allocator_t allocator = *Allocator_GetSystem();
    allocator.pfnAllocate = FailCallQueueAllocation;
    REQUIRE( CallQueue_Create( &allocator ) == nullptr );
}

TEST_CASE( "CallQueue rejects destructive mutation during callback dispatch",
           "[CypherCommon][Tier1][CallQueue]" )
{
    call_queue_t *pQueue = CallQueue_Create( Allocator_GetSystem() );
    REQUIRE( pQueue != nullptr );

    g_callQueueAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureCallQueueAssert );

    mutate_during_drain_call_t mutation{ pQueue, CY_INVALID_SIZE };
    u32 values[1]{};
    usize nValues = 0u;
    record_call_t record{ values, &nValues, 73u };
    REQUIRE( CallQueue_Push(
        pQueue,
        { MutateDuringDrainCall, &mutation, 0u } ) );
    REQUIRE( CallQueue_Push( pQueue, { RecordCall, &record, 99u } ) );

    REQUIRE( CallQueue_Drain( pQueue ) == 2u );
    REQUIRE( mutation.nCancelled == 0u );
    REQUIRE( nValues == 1u );
    REQUIRE( values[0] == 73u );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_callQueueAssertCount ==
        2u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
    CallQueue_Destroy( pQueue );
}
