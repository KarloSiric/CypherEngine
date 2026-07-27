//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Semaphore.h
//  Purpose: Declares CypherCommon Tier0 Semaphore synchronization support.
//  Details: Semaphores count available work/resources and let worker threads sleep
//           until a producer posts one or more units.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_SEMAPHORE_H
#define CYPHER_COMMON_TIER0_SEMAPHORE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Semaphore

Counting wait primitive for work queues, async request queues, and resource
availability tracking.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Thread.h"

#include <condition_variable>
#include <mutex>

namespace cypher::common
{

struct cy_semaphore_t {
    mutable std::mutex nativeMutex;
    std::condition_variable nativeCondition;
    u32 nCount = 0u;
    u32 nMaxCount = CY_U32_MAX;
    u32 nWaiterCount = 0u;
    bool_t isInitialized = CY_FALSE;
};

// Initializes a counting semaphore.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_SemaphoreInit(
    cy_semaphore_t *pSemaphore,
    u32 nInitialCount,
    u32 nMaxCount ) noexcept;

// Wakes blocked waiters and waits until they leave the semaphore.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_SemaphoreShutdown(
    cy_semaphore_t *pSemaphore ) noexcept;

// Returns whether the semaphore has been initialized.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_SemaphoreIsInitialized(
    const cy_semaphore_t *pSemaphore ) noexcept;

// Posts count units and wakes waiting threads.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_SemaphorePost(
    cy_semaphore_t *pSemaphore,
    u32 nCount = 1u ) noexcept;

// Waits until one unit is available or the semaphore is shut down.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_SemaphoreWait(
    cy_semaphore_t *pSemaphore ) noexcept;

// Attempts to consume one unit without blocking.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_SemaphoreTryWait(
    cy_semaphore_t *pSemaphore ) noexcept;

// Waits until one unit is available, shutdown happens, or timeout expires.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_SemaphoreWaitTimeoutMs(
    cy_semaphore_t *pSemaphore,
    u32 nMilliseconds ) noexcept;

// Detailed wait result that distinguishes acquire, timeout, shutdown, and misuse.
[[nodiscard]] CYPHER_COMMON_API cy_wait_result_t Cy_SemaphoreWaitResult(
    cy_semaphore_t *pSemaphore ) noexcept;

// Detailed timed wait result.
[[nodiscard]] CYPHER_COMMON_API cy_wait_result_t Cy_SemaphoreWaitTimeoutMsResult(
    cy_semaphore_t *pSemaphore,
    u32 nMilliseconds ) noexcept;

// Returns the current count for diagnostics.
[[nodiscard]] CYPHER_COMMON_API u32 Cy_SemaphoreGetCount(
    const cy_semaphore_t *pSemaphore ) noexcept;

// Returns the number of threads currently blocked in semaphore waits.
[[nodiscard]] CYPHER_COMMON_API u32 Cy_SemaphoreGetWaiterCount(
    const cy_semaphore_t *pSemaphore ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_SEMAPHORE_H
