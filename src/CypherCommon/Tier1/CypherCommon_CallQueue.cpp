//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CallQueue.cpp
//  Purpose: Implements an allocator-backed FIFO queue of deferred callback records.
//  Details: The implementation reuses the canonical allocator-backed ring queue,
//           preserves order during cancellation, and isolates reentrant drains.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CallQueue.h"
#include "CypherCommon_Queue.h"

#include <new>

namespace cypher::common
{

struct call_queue_t {
    queue_t<call_queue_entry_t> entries{};
    const allocator_t *pAllocator{ nullptr };
    bool_t bDraining{ CY_FALSE };
};

namespace
{

CYPHER_NODISCARD bool_t CallQueue_IsValid(
    const call_queue_t *pQueue ) noexcept
{
    return pQueue != nullptr &&
           Allocator_IsValid( pQueue->pAllocator ) &&
           Queue_IsValid( &pQueue->entries ) &&
           pQueue->entries.pAllocator == pQueue->pAllocator;
}

} // namespace

call_queue_t *CallQueue_Create(
    const allocator_t *pAllocator,
    usize nInitialCapacity ) noexcept
{
    const bool_t bValidAllocator = Allocator_IsValid( pAllocator );
    CY_ASSERT_MSG(
        bValidAllocator,
        "CallQueue_Create requires a valid allocator." );
    if ( !bValidAllocator ) {
        return nullptr;
    }

    void *pStorage = Allocator_Allocate(
        pAllocator,
        sizeof( call_queue_t ),
        alignof( call_queue_t ) );
    if ( pStorage == nullptr ) {
        return nullptr;
    }

    call_queue_t *pQueue = ::new ( pStorage ) call_queue_t{};
    if ( !Queue_Init(
             &pQueue->entries,
             pAllocator,
             nInitialCapacity ) ) {
        pQueue->~call_queue_t();
        Allocator_Free(
            pAllocator,
            pStorage,
            sizeof( call_queue_t ),
            alignof( call_queue_t ) );
        return nullptr;
    }

    pQueue->pAllocator = pAllocator;
    return pQueue;
}

void CallQueue_Destroy( call_queue_t *pQueue ) noexcept
{
    if ( pQueue == nullptr ) {
        return;
    }

    const bool_t bValidQueue = CallQueue_IsValid( pQueue );
    const bool_t bCanDestroy = bValidQueue && !pQueue->bDraining;
    CY_ASSERT_MSG( bValidQueue, "CallQueue_Destroy requires a valid queue." );
    CY_ASSERT_MSG(
        bCanDestroy,
        "CallQueue cannot be destroyed while a callback is executing." );
    if ( !bCanDestroy ) {
        return;
    }

    const allocator_t *pAllocator = pQueue->pAllocator;
    pQueue->~call_queue_t();
    Allocator_Free(
        pAllocator,
        pQueue,
        sizeof( call_queue_t ),
        alignof( call_queue_t ) );
}

void CallQueue_Clear( call_queue_t *pQueue ) noexcept
{
    const bool_t bValidQueue = CallQueue_IsValid( pQueue );
    const bool_t bCanClear = bValidQueue && !pQueue->bDraining;
    CY_ASSERT_MSG( bValidQueue, "CallQueue_Clear requires a valid queue." );
    CY_ASSERT_MSG(
        bCanClear,
        "CallQueue cannot be cleared while a callback is executing." );
    if ( bCanClear ) {
        Queue_Clear( &pQueue->entries );
    }
}

bool_t CallQueue_Push(
    call_queue_t *pQueue,
    const call_queue_entry_t &entry ) noexcept
{
    const bool_t bValidQueue = CallQueue_IsValid( pQueue );
    const bool_t bValidEntry = entry.pfnCall != nullptr;
    CY_ASSERT_MSG( bValidQueue, "CallQueue_Push requires a valid queue." );
    CY_ASSERT_MSG(
        bValidEntry,
        "CallQueue_Push requires a callable entry." );
    return bValidQueue && bValidEntry
        ? Queue_Push( &pQueue->entries, entry )
        : CY_FALSE;
}

usize CallQueue_CancelTag( call_queue_t *pQueue, u64 nTag ) noexcept
{
    const bool_t bValidQueue = CallQueue_IsValid( pQueue );
    const bool_t bCanCancel = bValidQueue && !pQueue->bDraining;
    CY_ASSERT_MSG( bValidQueue, "CallQueue_CancelTag requires a valid queue." );
    CY_ASSERT_MSG(
        bCanCancel,
        "CallQueue cannot cancel calls while a callback is executing." );
    if ( !bCanCancel ) {
        return 0u;
    }

    const usize nOriginalCount = Queue_Count( &pQueue->entries );
    usize nCancelled = 0u;
    for ( usize iEntry = 0u; iEntry < nOriginalCount; ++iEntry ) {
        call_queue_entry_t entry{};
        const bool_t bPopped = Queue_Pop( &pQueue->entries, &entry );
        CY_ASSERT_MSG( bPopped, "CallQueue cancellation lost queue state." );
        if ( !bPopped ) {
            break;
        }

        if ( entry.nTag == nTag ) {
            ++nCancelled;
            continue;
        }

        const bool_t bRestored = Queue_PushMove(
            &pQueue->entries,
            static_cast<call_queue_entry_t &&>( entry ) );
        CY_ASSERT_MSG(
            bRestored,
            "CallQueue cancellation failed to restore a retained call." );
        if ( !bRestored ) {
            break;
        }
    }
    return nCancelled;
}

usize CallQueue_Drain(
    call_queue_t *pQueue,
    usize nMaxCalls ) noexcept
{
    const bool_t bValidQueue = CallQueue_IsValid( pQueue );
    const bool_t bCanDrain = bValidQueue && !pQueue->bDraining;
    CY_ASSERT_MSG( bValidQueue, "CallQueue_Drain requires a valid queue." );
    CY_ASSERT_MSG( bCanDrain, "CallQueue_Drain does not permit nested drains." );
    if ( !bCanDrain || nMaxCalls == 0u ) {
        return 0u;
    }

    const usize nQueued = Queue_Count( &pQueue->entries );
    const usize nDrainLimit =
        nMaxCalls == CY_INVALID_SIZE || nMaxCalls > nQueued
            ? nQueued
            : nMaxCalls;

    pQueue->bDraining = CY_TRUE;
    usize nExecuted = 0u;
    for ( ; nExecuted < nDrainLimit; ++nExecuted ) {
        call_queue_entry_t entry{};
        if ( !Queue_Pop( &pQueue->entries, &entry ) ) {
            break;
        }
        entry.pfnCall( entry.pUserData );
    }
    pQueue->bDraining = CY_FALSE;
    return nExecuted;
}

usize CallQueue_Count( const call_queue_t *pQueue ) noexcept
{
    const bool_t bValidQueue = CallQueue_IsValid( pQueue );
    CY_ASSERT_MSG( bValidQueue, "CallQueue_Count requires a valid queue." );
    return bValidQueue ? Queue_Count( &pQueue->entries ) : 0u;
}

} // namespace cypher::common
