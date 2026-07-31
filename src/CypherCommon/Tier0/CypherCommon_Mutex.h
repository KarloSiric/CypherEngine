//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Mutex.h
//  Purpose: Declares CypherCommon Tier0 Mutex support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
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
    std::mutex native;
    std::atomic<bool_t> isInitialized{ CY_FALSE };
    std::atomic<thread_id_t> nOwnerThreadId{ CY_THREAD_INVALID_ID };
};

struct cy_recursive_mutex_t {
    std::recursive_mutex native;
    std::atomic<bool_t> isInitialized{ CY_FALSE };
    std::atomic<thread_id_t> nOwnerThreadId{ CY_THREAD_INVALID_ID };
    std::atomic<u32> nRecursionDepth{ 0u };
};

// Marks a mutex as ready for use.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MutexInit( cy_mutex_t *pMutex ) noexcept;

// Marks a mutex as shut down. Call only when no thread can own it.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MutexShutdown( cy_mutex_t *pMutex ) noexcept;

// Returns whether the mutex has been initialized.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MutexIsInitialized(
    const cy_mutex_t *pMutex ) noexcept;

// Blocks until the calling thread owns the mutex; false means invalid lifecycle use.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MutexLock( cy_mutex_t *pMutex ) noexcept;

// Attempts to lock without blocking.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MutexTryLock(
    cy_mutex_t *pMutex ) noexcept;

// Releases a mutex owned by the calling thread.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MutexUnlock( cy_mutex_t *pMutex ) noexcept;

// Returns true when the calling thread currently owns the mutex.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MutexIsOwnedByCurrentThread(
    const cy_mutex_t *pMutex ) noexcept;

// Marks a recursive mutex as ready for use.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_RecursiveMutexInit(
    cy_recursive_mutex_t *pMutex ) noexcept;

// Marks a recursive mutex as shut down. Call only when no thread can own it.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_RecursiveMutexShutdown(
    cy_recursive_mutex_t *pMutex ) noexcept;

// Returns whether the recursive mutex has been initialized.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_RecursiveMutexIsInitialized(
    const cy_recursive_mutex_t *pMutex ) noexcept;

// Blocks until the calling thread owns the recursive mutex.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_RecursiveMutexLock(
    cy_recursive_mutex_t *pMutex ) noexcept;

// Attempts to lock a recursive mutex without blocking.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_RecursiveMutexTryLock(
    cy_recursive_mutex_t *pMutex ) noexcept;

// Releases one recursive lock level owned by the calling thread.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_RecursiveMutexUnlock(
    cy_recursive_mutex_t *pMutex ) noexcept;

// Returns true when the calling thread owns at least one recursive lock level.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_RecursiveMutexIsOwnedByCurrentThread(
    const cy_recursive_mutex_t *pMutex ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_MUTEX_H
