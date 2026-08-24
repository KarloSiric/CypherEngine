//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Mutex.h
//  Purpose: Wraps non-recursive and recursive mutexes with lifecycle diagnostics.
//  Details: Ownership fields are diagnostic mirrors; the native mutex remains the
//           synchronization authority.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_MUTEX_H
#define CYPHER_COMMON_TIER0_MUTEX_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Mutex

Low-level mutual exclusion primitives. Use atomics for tiny counters and flags;
use mutexes when a larger invariant or compound data structure must be protected.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Thread.h"

#include <atomic>
#include <mutex>

namespace cypher::common
{

struct cy_mutex_t {
    std::mutex native;                                               // Native exclusion primitive.
    std::atomic<bool_t> isInitialized{ CY_FALSE };                   // API lifecycle gate.
    std::atomic<thread_id_t> nOwnerThreadId{ CY_THREAD_INVALID_ID }; // Diagnostic owner mirror.
};

struct cy_recursive_mutex_t {
    std::recursive_mutex native;                                     // Native recursive exclusion primitive.
    std::atomic<bool_t> isInitialized{ CY_FALSE };                   // API lifecycle gate.
    std::atomic<thread_id_t> nOwnerThreadId{ CY_THREAD_INVALID_ID }; // Diagnostic owner mirror.
    std::atomic<u32> nRecursionDepth{ 0u };                          // Locks held by the current owner.
};

// Marks a mutex as ready for use.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MutexInit( cy_mutex_t *pMutex ) noexcept;

// Marks a mutex as shut down. Call only when no thread can own it.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MutexShutdown( cy_mutex_t *pMutex ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MutexIsInitialized(
    const cy_mutex_t *pMutex ) noexcept;

// Blocks until the calling thread owns the mutex; false means invalid lifecycle use.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MutexLock( cy_mutex_t *pMutex ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MutexTryLock(
    cy_mutex_t *pMutex ) noexcept;

// Releases a mutex owned by the calling thread.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MutexUnlock( cy_mutex_t *pMutex ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MutexIsOwnedByCurrentThread(
    const cy_mutex_t *pMutex ) noexcept;

// Marks a recursive mutex as ready for use.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_RecursiveMutexInit(
    cy_recursive_mutex_t *pMutex ) noexcept;

// Marks a recursive mutex as shut down. Call only when no thread can own it.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_RecursiveMutexShutdown(
    cy_recursive_mutex_t *pMutex ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_RecursiveMutexIsInitialized(
    const cy_recursive_mutex_t *pMutex ) noexcept;

// Blocks until the calling thread owns the recursive mutex.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_RecursiveMutexLock(
    cy_recursive_mutex_t *pMutex ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_RecursiveMutexTryLock(
    cy_recursive_mutex_t *pMutex ) noexcept;

// Releases one recursive lock level owned by the calling thread.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_RecursiveMutexUnlock(
    cy_recursive_mutex_t *pMutex ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_RecursiveMutexIsOwnedByCurrentThread(
    const cy_recursive_mutex_t *pMutex ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_MUTEX_H
