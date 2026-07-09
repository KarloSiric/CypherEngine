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

bool_t EventConsumeSignalLocked( cy_event_t *pEvent )
{
    if ( !pEvent->bInitialized || !pEvent->bSignaled ) {
        return CY_FALSE;
    }

    if ( pEvent->resetMode == cy_event_reset_mode_t::Auto ) {
        pEvent->bSignaled = CY_FALSE;
    }

    return CY_TRUE;
}

} // namespace

bool_t Cy_EventInit( cy_event_t *pEvent, cy_event_reset_mode_t resetMode, bool_t bInitiallySignaled )
{
    if ( pEvent == nullptr ) {
        return CY_FALSE;
    }

    std::lock_guard<std::mutex> lock( pEvent->nativeMutex );
    pEvent->resetMode = resetMode;
    pEvent->bSignaled = bInitiallySignaled;
    pEvent->bInitialized = CY_TRUE;
    return CY_TRUE;
}

void Cy_EventShutdown( cy_event_t *pEvent )
{
    if ( pEvent == nullptr ) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock( pEvent->nativeMutex );
        pEvent->bInitialized = CY_FALSE;
        pEvent->bSignaled = CY_TRUE;
    }

    pEvent->nativeCondition.notify_all();
}

bool_t Cy_EventIsInitialized( const cy_event_t *pEvent )
{
    if ( pEvent == nullptr ) {
        return CY_FALSE;
    }

    std::lock_guard<std::mutex> lock( pEvent->nativeMutex );
    return pEvent->bInitialized;
}

void Cy_EventSignal( cy_event_t *pEvent )
{
    if ( pEvent == nullptr ) {
        return;
    }

    cy_event_reset_mode_t resetMode = cy_event_reset_mode_t::Manual;
    {
        std::lock_guard<std::mutex> lock( pEvent->nativeMutex );
        if ( !pEvent->bInitialized ) {
            return;
        }

        pEvent->bSignaled = CY_TRUE;
        resetMode = pEvent->resetMode;
    }

    if ( resetMode == cy_event_reset_mode_t::Auto ) {
        pEvent->nativeCondition.notify_one();
    } else {
        pEvent->nativeCondition.notify_all();
    }
}

void Cy_EventReset( cy_event_t *pEvent )
{
    if ( pEvent == nullptr ) {
        return;
    }

    std::lock_guard<std::mutex> lock( pEvent->nativeMutex );
    if ( pEvent->bInitialized ) {
        pEvent->bSignaled = CY_FALSE;
    }
}

bool_t Cy_EventWait( cy_event_t *pEvent )
{
    if ( pEvent == nullptr ) {
        return CY_FALSE;
    }

    std::unique_lock<std::mutex> lock( pEvent->nativeMutex );
    pEvent->nativeCondition.wait( lock, [&]() {
        return !pEvent->bInitialized || pEvent->bSignaled;
    } );

    return EventConsumeSignalLocked( pEvent );
}

bool_t Cy_EventWaitTimeoutMs( cy_event_t *pEvent, u32 nMilliseconds )
{
    if ( pEvent == nullptr ) {
        return CY_FALSE;
    }

    std::unique_lock<std::mutex> lock( pEvent->nativeMutex );
    const bool_t bReady = pEvent->nativeCondition.wait_for( lock, std::chrono::milliseconds( nMilliseconds ), [&]() {
        return !pEvent->bInitialized || pEvent->bSignaled;
    } );

    if ( !bReady ) {
        return CY_FALSE;
    }

    return EventConsumeSignalLocked( pEvent );
}

} // namespace cypher::common
