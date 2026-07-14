//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Event.h
//  Purpose: Declares CypherCommon Tier0 Event synchronization support.
//  Details: Events let worker threads sleep until another thread signals a state
//           change. They are used below future job queues, async IO, and shutdown
//           coordination.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_EVENT_H
#define CYPHER_COMMON_TIER0_EVENT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Event

Manual-reset and auto-reset event primitive backed by standard condition
variables for portable Tier0 waiting.
================
*/

#include "CypherCommon_BaseTypes.h"

#include <condition_variable>
#include <mutex>

namespace cypher::common
{

enum class cy_event_reset_mode_t : u8 {
    Manual = 0u,
    Auto = 1u
};

struct cy_event_t {
    mutable std::mutex nativeMutex;
    std::condition_variable nativeCondition;
    bool_t bInitialized = CY_FALSE;
    bool_t bSignaled = CY_FALSE;
    cy_event_reset_mode_t resetMode = cy_event_reset_mode_t::Manual;
};

// Initializes an event with reset policy and initial signal state.
bool_t Cy_EventInit( cy_event_t *pEvent, cy_event_reset_mode_t resetMode, bool_t bInitiallySignaled );

// Shuts down an event and wakes any waiters so they can leave.
void Cy_EventShutdown( cy_event_t *pEvent );

// Returns whether the event is initialized.
bool_t Cy_EventIsInitialized( const cy_event_t *pEvent );

// Signals the event. Manual-reset wakes all waiters; auto-reset wakes one.
void Cy_EventSignal( cy_event_t *pEvent );

// Clears the signaled state.
void Cy_EventReset( cy_event_t *pEvent );

// Waits until the event is signaled or shut down.
bool_t Cy_EventWait( cy_event_t *pEvent );

// Waits until signaled, shut down, or timeout expires.
bool_t Cy_EventWaitTimeoutMs( cy_event_t *pEvent, u32 nMilliseconds );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_EVENT_H
