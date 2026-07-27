//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Event.cpp
//  Purpose: Implements CypherCommon Tier0 Event synchronization.
//  Details: This file provides portable event waits for worker wakeups, runtime
//           shutdown, async IO completion, and future job system coordination.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Event.h"

#include <chrono>

namespace cypher::common
{
namespace
{

cy_wait_result_t EventConsumeSignalLocked( cy_event_t *pEvent ) noexcept
{
    if ( !pEvent->isInitialized ) {
        return cy_wait_result_t::Shutdown;
    }
    if ( !pEvent->isSignaled ) {
        return cy_wait_result_t::Timeout;
    }

    if ( pEvent->resetMode == cy_event_reset_mode_t::Auto ) {
        pEvent->isSignaled = CY_FALSE;
    }

    return cy_wait_result_t::Success;
}

void EventWaiterLeftLocked( cy_event_t *pEvent ) noexcept
{
    --pEvent->nWaiterCount;
    if ( !pEvent->isInitialized && pEvent->nWaiterCount == 0u ) {
        pEvent->nativeCondition.notify_all();
    }
}

} // namespace

bool_t Cy_EventInit(
    cy_event_t *pEvent,
    cy_event_reset_mode_t resetMode,
    bool_t isInitiallySignaled ) noexcept
{
    if ( pEvent == nullptr ||
         ( resetMode != cy_event_reset_mode_t::Auto &&
           resetMode != cy_event_reset_mode_t::Manual ) ) {
        return CY_FALSE;
    }

    std::lock_guard<std::mutex> lock( pEvent->nativeMutex );
    if ( pEvent->isInitialized || pEvent->nWaiterCount != 0u ) {
        return CY_FALSE;
    }
    pEvent->resetMode = resetMode;
    pEvent->isSignaled = isInitiallySignaled;
    pEvent->isInitialized = CY_TRUE;
    return CY_TRUE;
}

bool_t Cy_EventShutdown( cy_event_t *pEvent ) noexcept
{
    if ( pEvent == nullptr ) {
        return CY_FALSE;
    }

    std::unique_lock<std::mutex> lock( pEvent->nativeMutex );
    if ( !pEvent->isInitialized ) {
        return CY_FALSE;
    }

    pEvent->isInitialized = CY_FALSE;
    pEvent->isSignaled = CY_FALSE;
    pEvent->nativeCondition.notify_all();
    pEvent->nativeCondition.wait( lock, [&]() {
        return pEvent->nWaiterCount == 0u;
    } );
    return CY_TRUE;
}

bool_t Cy_EventIsInitialized( const cy_event_t *pEvent ) noexcept
{
    if ( pEvent == nullptr ) {
        return CY_FALSE;
    }

    std::lock_guard<std::mutex> lock( pEvent->nativeMutex );
    return pEvent->isInitialized;
}

bool_t Cy_EventSignal( cy_event_t *pEvent ) noexcept
{
    if ( pEvent == nullptr ) {
        return CY_FALSE;
    }

    cy_event_reset_mode_t resetMode = cy_event_reset_mode_t::Manual;
    {
        std::lock_guard<std::mutex> lock( pEvent->nativeMutex );
        if ( !pEvent->isInitialized ) {
            return CY_FALSE;
        }

        pEvent->isSignaled = CY_TRUE;
        resetMode = pEvent->resetMode;
    }

    if ( resetMode == cy_event_reset_mode_t::Auto ) {
        pEvent->nativeCondition.notify_one();
    } else {
        pEvent->nativeCondition.notify_all();
    }
    return CY_TRUE;
}

bool_t Cy_EventReset( cy_event_t *pEvent ) noexcept
{
    if ( pEvent == nullptr ) {
        return CY_FALSE;
    }

    std::lock_guard<std::mutex> lock( pEvent->nativeMutex );
    if ( !pEvent->isInitialized ) {
        return CY_FALSE;
    }
    pEvent->isSignaled = CY_FALSE;
    return CY_TRUE;
}

cy_wait_result_t Cy_EventWaitResult( cy_event_t *pEvent ) noexcept
{
    if ( pEvent == nullptr ) {
        return cy_wait_result_t::Invalid;
    }

    std::unique_lock<std::mutex> lock( pEvent->nativeMutex );
    if ( !pEvent->isInitialized ) {
        return cy_wait_result_t::Shutdown;
    }

    ++pEvent->nWaiterCount;
    try {
        pEvent->nativeCondition.wait( lock, [&]() {
            return !pEvent->isInitialized || pEvent->isSignaled;
        } );
    } catch ( ... ) {
        EventWaiterLeftLocked( pEvent );
        return cy_wait_result_t::Invalid;
    }

    const cy_wait_result_t result = EventConsumeSignalLocked( pEvent );
    EventWaiterLeftLocked( pEvent );
    return result;
}

cy_wait_result_t Cy_EventWaitTimeoutMsResult(
    cy_event_t *pEvent,
    u32 nMilliseconds ) noexcept
{
    if ( pEvent == nullptr ) {
        return cy_wait_result_t::Invalid;
    }

    std::unique_lock<std::mutex> lock( pEvent->nativeMutex );
    if ( !pEvent->isInitialized ) {
        return cy_wait_result_t::Shutdown;
    }

    ++pEvent->nWaiterCount;
    bool_t isReady = CY_FALSE;
    try {
        isReady = pEvent->nativeCondition.wait_for(
            lock,
            std::chrono::milliseconds( nMilliseconds ),
            [&]() {
                return !pEvent->isInitialized || pEvent->isSignaled;
            } );
    } catch ( ... ) {
        EventWaiterLeftLocked( pEvent );
        return cy_wait_result_t::Invalid;
    }

    const cy_wait_result_t result = isReady
        ? EventConsumeSignalLocked( pEvent )
        : cy_wait_result_t::Timeout;
    EventWaiterLeftLocked( pEvent );
    return result;
}

bool_t Cy_EventWait( cy_event_t *pEvent ) noexcept
{
    return Cy_EventWaitResult( pEvent ) == cy_wait_result_t::Success;
}

bool_t Cy_EventWaitTimeoutMs(
    cy_event_t *pEvent,
    u32 nMilliseconds ) noexcept
{
    return Cy_EventWaitTimeoutMsResult( pEvent, nMilliseconds ) ==
           cy_wait_result_t::Success;
}

bool_t Cy_EventIsSignaled( const cy_event_t *pEvent ) noexcept
{
    if ( pEvent == nullptr ) {
        return CY_FALSE;
    }
    std::lock_guard<std::mutex> lock( pEvent->nativeMutex );
    return pEvent->isInitialized && pEvent->isSignaled;
}

u32 Cy_EventGetWaiterCount( const cy_event_t *pEvent ) noexcept
{
    if ( pEvent == nullptr ) {
        return 0u;
    }
    std::lock_guard<std::mutex> lock( pEvent->nativeMutex );
    return pEvent->nWaiterCount;
}

} // namespace cypher::common
