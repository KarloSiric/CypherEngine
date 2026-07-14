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

#include "CypherCommon_BaseTypes.h"

#include <mutex>

namespace cypher::common
{

struct cy_mutex_t {
    std::mutex native;
    bool_t bInitialized = CY_FALSE;
};

struct cy_recursive_mutex_t {
    std::recursive_mutex native;
    bool_t bInitialized = CY_FALSE;
};

// Marks a mutex as ready for use.
bool_t Cy_MutexInit( cy_mutex_t *pMutex );

// Marks a mutex as shut down. Call only when no thread can own it.
void Cy_MutexShutdown( cy_mutex_t *pMutex );

// Returns whether the mutex has been initialized.
bool_t Cy_MutexIsInitialized( const cy_mutex_t *pMutex );

// Blocks until the calling thread owns the mutex.
void Cy_MutexLock( cy_mutex_t *pMutex );

// Attempts to lock without blocking.
bool_t Cy_MutexTryLock( cy_mutex_t *pMutex );

// Releases a mutex owned by the calling thread.
void Cy_MutexUnlock( cy_mutex_t *pMutex );

// Marks a recursive mutex as ready for use.
bool_t Cy_RecursiveMutexInit( cy_recursive_mutex_t *pMutex );

// Marks a recursive mutex as shut down. Call only when no thread can own it.
void Cy_RecursiveMutexShutdown( cy_recursive_mutex_t *pMutex );

// Returns whether the recursive mutex has been initialized.
bool_t Cy_RecursiveMutexIsInitialized( const cy_recursive_mutex_t *pMutex );

// Blocks until the calling thread owns the recursive mutex.
void Cy_RecursiveMutexLock( cy_recursive_mutex_t *pMutex );

// Attempts to lock a recursive mutex without blocking.
bool_t Cy_RecursiveMutexTryLock( cy_recursive_mutex_t *pMutex );

// Releases one recursive lock level owned by the calling thread.
void Cy_RecursiveMutexUnlock( cy_recursive_mutex_t *pMutex );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_MUTEX_H
