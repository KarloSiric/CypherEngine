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

#include "CypherCommon_BaseTypes.h"

#include <condition_variable>
#include <mutex>

namespace cypher::common
{

struct cy_semaphore_t {
    mutable std::mutex nativeMutex;
    std::condition_variable nativeCondition;
    u32 nCount = 0u;
    u32 nMaxCount = CY_U32_MAX;
    bool_t bInitialized = CY_FALSE;
};

// Initializes a counting semaphore.
bool_t Cy_SemaphoreInit( cy_semaphore_t *pSemaphore, u32 nInitialCount, u32 nMaxCount );

// Shuts down a semaphore and wakes waiters so they can leave.
void Cy_SemaphoreShutdown( cy_semaphore_t *pSemaphore );

// Returns whether the semaphore has been initialized.
bool_t Cy_SemaphoreIsInitialized( const cy_semaphore_t *pSemaphore );

// Posts count units and wakes waiting threads.
bool_t Cy_SemaphorePost( cy_semaphore_t *pSemaphore, u32 nCount = 1u );

// Waits until one unit is available or the semaphore is shut down.
bool_t Cy_SemaphoreWait( cy_semaphore_t *pSemaphore );

// Attempts to consume one unit without blocking.
bool_t Cy_SemaphoreTryWait( cy_semaphore_t *pSemaphore );

// Waits until one unit is available, shutdown happens, or timeout expires.
bool_t Cy_SemaphoreWaitTimeoutMs( cy_semaphore_t *pSemaphore, u32 nMilliseconds );

// Returns the current count for diagnostics.
u32 Cy_SemaphoreGetCount( cy_semaphore_t *pSemaphore );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_SEMAPHORE_H
