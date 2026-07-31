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

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Thread.h"

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
    bool_t isInitialized = CY_FALSE;
    bool_t isSignaled = CY_FALSE;
    u32 nWaiterCount = 0u;
    cy_event_reset_mode_t resetMode = cy_event_reset_mode_t::Manual;
};

// Initializes an event with reset policy and initial signal state.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_EventInit(
    cy_event_t *pEvent,
    cy_event_reset_mode_t resetMode,
    bool_t isInitiallySignaled ) noexcept;

// Wakes blocked waiters and does not return until they have left this event.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_EventShutdown( cy_event_t *pEvent ) noexcept;

// Returns whether the event is initialized.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_EventIsInitialized(
    const cy_event_t *pEvent ) noexcept;

// Signals the event. Manual-reset wakes all waiters; auto-reset wakes one.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_EventSignal( cy_event_t *pEvent ) noexcept;

// Clears the signaled state.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_EventReset( cy_event_t *pEvent ) noexcept;

// Waits until the event is signaled or shut down.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_EventWait(
    cy_event_t *pEvent ) noexcept;

// Waits until signaled, shut down, or timeout expires.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_EventWaitTimeoutMs(
    cy_event_t *pEvent,
    u32 nMilliseconds ) noexcept;

// Detailed wait result that distinguishes signal, timeout, shutdown, and misuse.
CYPHER_NODISCARD CYPHER_COMMON_API cy_wait_result_t Cy_EventWaitResult(
    cy_event_t *pEvent ) noexcept;

// Detailed timed wait result.
CYPHER_NODISCARD CYPHER_COMMON_API cy_wait_result_t Cy_EventWaitTimeoutMsResult(
    cy_event_t *pEvent,
    u32 nMilliseconds ) noexcept;

// Returns the current signal state for diagnostics.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_EventIsSignaled(
    const cy_event_t *pEvent ) noexcept;

// Returns the number of threads currently blocked in event waits.
CYPHER_NODISCARD CYPHER_COMMON_API u32 Cy_EventGetWaiterCount(
    const cy_event_t *pEvent ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_EVENT_H
