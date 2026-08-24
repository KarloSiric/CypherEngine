//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Event.cpp
//  Purpose: Implements the synchronous application event bus.
//  Details: A generational table owns subscription records while a stable ordered
//           handle list and reusable snapshot make callback mutation deterministic.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Event.h"
#include "CypherCommon_HandleTable.h"
#include "CypherCommon_Vector.h"

#include <new>

namespace cypher::common
{

namespace
{

struct event_subscription_record_t {
    event_id_t eventId{ 0u };                  // Event channel accepted by this record.
    i32 nPriority{ 0 };                        // Higher priorities run first.
    flags32_t flags{ EVENT_SUBSCRIPTION_FLAG_NONE }; // Lifetime and dispatch policy.
    event_callback_t pfnCallback{ nullptr };   // Synchronous subscriber entry point.
    void *pUserData{ nullptr };                // Opaque callback context.
    u64 nSequence{ 0u };                       // FIFO tie-breaker within one priority.
};

constexpr flags32_t EVENT_SUBSCRIPTION_KNOWN_FLAGS =
    EVENT_SUBSCRIPTION_FLAG_ONCE;

bool_t EventBus_RebaseSequences( event_bus_t *pBus ) noexcept;

} // namespace

struct event_bus_t {
    handle_table_t<event_subscription_record_t> subscriptions{}; // Stable generational records.
    vector_t<event_subscription_t> order{};       // Priority-sorted live handles.
    vector_t<event_subscription_t> dispatchSnapshot{}; // Frozen order for current emission.
    const allocator_t *pAllocator{ nullptr };     // Shared owner for all bus storage.
    u64 nNextSequence{ 0u };                      // Monotonic registration order.
    bool_t bEmitting{ CY_FALSE };                 // Rejects nested dispatch and destruction.
};

namespace
{

bool_t EventBus_RebaseSequences( event_bus_t *pBus ) noexcept
{
    // Sequence exhaustion is practically unreachable, but rebasing preserves
    // current stable order without allowing the counter to wrap.
    for ( usize iOrder = 0u; iOrder < pBus->order.nCount; ++iOrder ) {
        event_subscription_record_t *pRecord = HandleTable_Get(
            &pBus->subscriptions,
            pBus->order.pData[iOrder] );
        const bool_t bLiveRecord = pRecord != nullptr;
        CY_ASSERT_MSG(
            bLiveRecord,
            "EventBus ordered list contains a stale subscription." );
        if ( !bLiveRecord ) {
            return CY_FALSE;
        }
        pRecord->nSequence = static_cast<u64>( iOrder );
    }
    pBus->nNextSequence = static_cast<u64>( pBus->order.nCount );
    return CY_TRUE;
}

usize EventBus_FindInsertionIndex(
    const event_bus_t &bus,
    const event_subscription_record_t &record ) noexcept
{
    for ( usize iOrder = 0u; iOrder < bus.order.nCount; ++iOrder ) {
        const event_subscription_record_t *pExisting = HandleTable_Get(
            &bus.subscriptions,
            bus.order.pData[iOrder] );
        CY_ASSERT_MSG(
            pExisting != nullptr,
            "EventBus ordered list contains a stale subscription." );
        if ( pExisting == nullptr ) {
            return iOrder;
        }
        if ( record.nPriority > pExisting->nPriority ) {
            return iOrder;
        }
        if ( record.nPriority == pExisting->nPriority &&
             record.nSequence < pExisting->nSequence ) {
            return iOrder;
        }
    }
    return bus.order.nCount;
}

usize EventBus_FindOrderedHandle(
    const event_bus_t &bus,
    event_subscription_t subscription ) noexcept
{
    for ( usize iOrder = 0u; iOrder < bus.order.nCount; ++iOrder ) {
        if ( bus.order.pData[iOrder].value == subscription.value ) {
            return iOrder;
        }
    }
    return CY_INVALID_SIZE;
}

} // namespace

event_bus_t *EventBus_Create( const event_bus_desc_t &desc ) noexcept
{
    const bool_t bValidAllocator = Allocator_IsValid( desc.pAllocator );
    const bool_t bValidCapacity =
        desc.nInitialSubscriptions <= CY_HANDLE_TABLE_MAX_CAPACITY;
    CY_ASSERT_MSG(
        bValidAllocator,
        "EventBus_Create requires a valid allocator." );
    CY_ASSERT_MSG(
        bValidCapacity,
        "EventBus initial capacity exceeds the compact subscription range." );
    if ( !bValidAllocator || !bValidCapacity ) {
        return nullptr;
    }

    void *pStorage = Allocator_Allocate(
        desc.pAllocator,
        sizeof( event_bus_t ),
        alignof( event_bus_t ) );
    if ( pStorage == nullptr ) {
        return nullptr;
    }

    event_bus_t *pBus = ::new ( pStorage ) event_bus_t{};
    const bool_t bInitialized =
        HandleTable_Init(
            &pBus->subscriptions,
            desc.pAllocator,
            desc.nInitialSubscriptions ) &&
        Vector_Init(
            &pBus->order,
            desc.pAllocator,
            desc.nInitialSubscriptions ) &&
        Vector_Init(
            &pBus->dispatchSnapshot,
            desc.pAllocator,
            desc.nInitialSubscriptions );
    if ( !bInitialized ) {
        pBus->~event_bus_t();
        Allocator_Free(
            desc.pAllocator,
            pStorage,
            sizeof( event_bus_t ),
            alignof( event_bus_t ) );
        return nullptr;
    }

    pBus->pAllocator = desc.pAllocator;
    return pBus;
}

void EventBus_Destroy( event_bus_t *pBus ) noexcept
{
    if ( pBus == nullptr ) {
        return;
    }

    const bool_t bValidBus = EventBus_IsValid( pBus );
    const bool_t bCanDestroy = bValidBus && !pBus->bEmitting;
    CY_ASSERT_MSG( bValidBus, "EventBus_Destroy requires a valid bus." );
    CY_ASSERT_MSG(
        bCanDestroy,
        "EventBus cannot be destroyed while a callback is executing." );
    if ( !bCanDestroy ) {
        return;
    }

    const allocator_t *pAllocator = pBus->pAllocator;
    pBus->~event_bus_t();
    Allocator_Free(
        pAllocator,
        pBus,
        sizeof( event_bus_t ),
        alignof( event_bus_t ) );
}

void EventBus_Clear( event_bus_t *pBus ) noexcept
{
    const bool_t bValidBus = EventBus_IsValid( pBus );
    CY_ASSERT_MSG( bValidBus, "EventBus_Clear requires a valid bus." );
    if ( !bValidBus ) {
        return;
    }

    HandleTable_Clear( &pBus->subscriptions );
    Vector_Clear( &pBus->order );
    if ( !pBus->bEmitting ) {
        Vector_Clear( &pBus->dispatchSnapshot );
    }
}

bool_t EventBus_IsValid( const event_bus_t *pBus ) noexcept
{
    return pBus != nullptr &&
           Allocator_IsValid( pBus->pAllocator ) &&
           HandleTable_IsValid( &pBus->subscriptions ) &&
           Vector_IsValid( &pBus->order ) &&
           Vector_IsValid( &pBus->dispatchSnapshot ) &&
           pBus->subscriptions.pAllocator == pBus->pAllocator &&
           pBus->order.pAllocator == pBus->pAllocator &&
           pBus->dispatchSnapshot.pAllocator == pBus->pAllocator &&
           HandleTable_Count( &pBus->subscriptions ) == pBus->order.nCount;
}

event_subscription_t EventBus_Subscribe(
    event_bus_t *pBus,
    event_id_t eventId,
    i32 nPriority,
    flags32_t flags,
    event_callback_t pfnCallback,
    void *pUserData ) noexcept
{
    const bool_t bValidBus = EventBus_IsValid( pBus );
    const bool_t bValidCallback = pfnCallback != nullptr;
    const bool_t bValidFlags =
        ( flags & ~EVENT_SUBSCRIPTION_KNOWN_FLAGS ) == 0u;
    CY_ASSERT_MSG( bValidBus, "EventBus_Subscribe requires a valid bus." );
    CY_ASSERT_MSG(
        bValidCallback,
        "EventBus_Subscribe requires a callback." );
    CY_ASSERT_MSG(
        bValidFlags,
        "EventBus_Subscribe received unknown flags." );
    if ( !bValidBus || !bValidCallback || !bValidFlags ) {
        return CY_EVENT_SUBSCRIPTION_INVALID;
    }

    if ( pBus->nNextSequence == CY_U64_MAX &&
         !EventBus_RebaseSequences( pBus ) ) {
        return CY_EVENT_SUBSCRIPTION_INVALID;
    }

    const event_subscription_record_t record{
        eventId,
        nPriority,
        flags,
        pfnCallback,
        pUserData,
        pBus->nNextSequence
    };
    const event_subscription_t subscription = HandleTable_Insert(
        &pBus->subscriptions,
        record );
    if ( !Cy_Handle32IsValid( subscription ) ) {
        return CY_EVENT_SUBSCRIPTION_INVALID;
    }

    const usize iInsertion = EventBus_FindInsertionIndex( *pBus, record );
    if ( !Vector_Insert( &pBus->order, iInsertion, subscription ) ) {
        static_cast<void>(
            HandleTable_Remove( &pBus->subscriptions, subscription ) );
        return CY_EVENT_SUBSCRIPTION_INVALID;
    }

    ++pBus->nNextSequence;
    return subscription;
}

bool_t EventBus_Unsubscribe(
    event_bus_t *pBus,
    event_subscription_t subscription ) noexcept
{
    const bool_t bValidBus = EventBus_IsValid( pBus );
    CY_ASSERT_MSG( bValidBus, "EventBus_Unsubscribe requires a valid bus." );
    if ( !bValidBus ||
         !HandleTable_Contains( &pBus->subscriptions, subscription ) ) {
        return CY_FALSE;
    }

    const usize iOrder = EventBus_FindOrderedHandle( *pBus, subscription );
    const bool_t bOrdered = iOrder != CY_INVALID_SIZE;
    CY_ASSERT_MSG(
        bOrdered,
        "EventBus live subscription is missing from its ordered list." );
    if ( !bOrdered ) {
        return CY_FALSE;
    }

    Vector_Erase( &pBus->order, iOrder );
    return HandleTable_Remove( &pBus->subscriptions, subscription );
}

bool_t EventBus_IsSubscribed(
    const event_bus_t *pBus,
    event_subscription_t subscription ) noexcept
{
    const bool_t bValidBus = EventBus_IsValid( pBus );
    CY_ASSERT_MSG( bValidBus, "EventBus_IsSubscribed requires a valid bus." );
    return bValidBus &&
           HandleTable_Contains( &pBus->subscriptions, subscription );
}

usize EventBus_SubscriptionCount( const event_bus_t *pBus ) noexcept
{
    const bool_t bValidBus = EventBus_IsValid( pBus );
    CY_ASSERT_MSG(
        bValidBus,
        "EventBus_SubscriptionCount requires a valid bus." );
    return bValidBus ? HandleTable_Count( &pBus->subscriptions ) : 0u;
}

bool_t EventBus_TryEmit(
    event_bus_t *pBus,
    event_id_t eventId,
    const event_payload_t &payload,
    usize *pInvokedOut ) noexcept
{
    if ( pInvokedOut != nullptr ) {
        *pInvokedOut = 0u;
    }
    const bool_t bValidBus = EventBus_IsValid( pBus );
    const bool_t bValidPayload = BinaryBlock_IsValid( payload.data );
    const bool_t bValidOutput = pInvokedOut != nullptr;
    const bool_t bCanEmit = bValidBus && !pBus->bEmitting;
    CY_ASSERT_MSG( bValidBus, "EventBus_TryEmit requires a valid bus." );
    CY_ASSERT_MSG(
        bValidPayload,
        "EventBus_TryEmit requires a valid payload range." );
    CY_ASSERT_MSG(
        bValidOutput,
        "EventBus_TryEmit requires an output count." );
    CY_ASSERT_MSG( bCanEmit, "EventBus does not permit nested emission." );
    if ( !bValidBus || !bValidPayload || !bValidOutput || !bCanEmit ) {
        return CY_FALSE;
    }

    // Snapshot the handles so callbacks may subscribe or unsubscribe without
    // invalidating this dispatch traversal. Removed handles fail generation lookup.
    Vector_Clear( &pBus->dispatchSnapshot );
    if ( !Vector_Append(
             &pBus->dispatchSnapshot,
             span_t<const event_subscription_t>{
                 pBus->order.pData,
                 pBus->order.nCount } ) ) {
        return CY_FALSE;
    }

    pBus->bEmitting = CY_TRUE;
    usize nInvoked = 0u;
    for ( usize iDispatch = 0u;
          iDispatch < pBus->dispatchSnapshot.nCount;
          ++iDispatch ) {
        const event_subscription_t subscription =
            pBus->dispatchSnapshot.pData[iDispatch];
        event_subscription_record_t *pRecord = HandleTable_Get(
            &pBus->subscriptions,
            subscription );
        if ( pRecord == nullptr || pRecord->eventId != eventId ) {
            continue;
        }

        // Copy callback state before invocation because user code may remove its
        // own subscription and invalidate pRecord.
        const event_callback_t pfnCallback = pRecord->pfnCallback;
        void *pUserData = pRecord->pUserData;
        const flags32_t flags = pRecord->flags;
        pfnCallback( eventId, payload, pUserData );
        ++nInvoked;

        if ( ( flags & EVENT_SUBSCRIPTION_FLAG_ONCE ) != 0u ) {
            static_cast<void>(
                EventBus_Unsubscribe( pBus, subscription ) );
        }
    }
    pBus->bEmitting = CY_FALSE;
    Vector_Clear( &pBus->dispatchSnapshot );
    *pInvokedOut = nInvoked;
    return CY_TRUE;
}

usize EventBus_Emit(
    event_bus_t *pBus,
    event_id_t eventId,
    const event_payload_t &payload ) noexcept
{
    usize nInvoked = 0u;
    static_cast<void>( EventBus_TryEmit(
        pBus,
        eventId,
        payload,
        &nInvoked ) );
    return nInvoked;
}

} // namespace cypher::common
