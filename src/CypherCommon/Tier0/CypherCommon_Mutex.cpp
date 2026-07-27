//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Mutex.cpp
//  Purpose: Implements CypherCommon Tier0 mutex primitives.
//  Details: This file provides small, portable mutual exclusion wrappers used by
//           diagnostics, resource tables, async systems, and future containers.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Mutex.h"

namespace cypher::common
{

bool_t Cy_MutexInit( cy_mutex_t *pMutex ) noexcept
{
    if ( pMutex == nullptr ) {
        return CY_FALSE;
    }

    bool_t wasInitialized = CY_FALSE;
    if ( !pMutex->isInitialized.compare_exchange_strong(
             wasInitialized,
             CY_TRUE,
             std::memory_order_release,
             std::memory_order_relaxed ) ) {
        return CY_FALSE;
    }

    pMutex->nOwnerThreadId.store(
        CY_THREAD_INVALID_ID,
        std::memory_order_relaxed );
    return CY_TRUE;
}

bool_t Cy_MutexShutdown( cy_mutex_t *pMutex ) noexcept
{
    if ( !Cy_MutexIsInitialized( pMutex ) ) {
        return CY_FALSE;
    }
    if ( pMutex->nOwnerThreadId.load( std::memory_order_acquire ) !=
         CY_THREAD_INVALID_ID ) {
        return CY_FALSE;
    }

    try {
        if ( !pMutex->native.try_lock() ) {
            return CY_FALSE;
        }
    } catch ( ... ) {
        return CY_FALSE;
    }

    bool_t wasInitialized = CY_TRUE;
    const bool_t didShutdown = pMutex->isInitialized.compare_exchange_strong(
        wasInitialized,
        CY_FALSE,
        std::memory_order_acq_rel,
        std::memory_order_acquire );
    pMutex->native.unlock();
    return didShutdown;
}

bool_t Cy_MutexIsInitialized( const cy_mutex_t *pMutex ) noexcept
{
    return pMutex != nullptr &&
           pMutex->isInitialized.load( std::memory_order_acquire );
}

bool_t Cy_MutexLock( cy_mutex_t *pMutex ) noexcept
{
    if ( !Cy_MutexIsInitialized( pMutex ) ) {
        return CY_FALSE;
    }

    const thread_id_t nCurrentThreadId = Cy_ThreadGetCurrentId();
    if ( pMutex->nOwnerThreadId.load( std::memory_order_acquire ) ==
         nCurrentThreadId ) {
        return CY_FALSE;
    }

    try {
        pMutex->native.lock();
    } catch ( ... ) {
        return CY_FALSE;
    }
    if ( !Cy_MutexIsInitialized( pMutex ) ) {
        pMutex->native.unlock();
        return CY_FALSE;
    }
    pMutex->nOwnerThreadId.store( nCurrentThreadId, std::memory_order_release );
    return CY_TRUE;
}

bool_t Cy_MutexTryLock( cy_mutex_t *pMutex ) noexcept
{
    if ( !Cy_MutexIsInitialized( pMutex ) ) {
        return CY_FALSE;
    }

    const thread_id_t nCurrentThreadId = Cy_ThreadGetCurrentId();
    if ( pMutex->nOwnerThreadId.load( std::memory_order_acquire ) ==
         nCurrentThreadId ) {
        return CY_FALSE;
    }

    try {
        if ( !pMutex->native.try_lock() ) {
            return CY_FALSE;
        }
    } catch ( ... ) {
        return CY_FALSE;
    }

    if ( !Cy_MutexIsInitialized( pMutex ) ) {
        pMutex->native.unlock();
        return CY_FALSE;
    }
    pMutex->nOwnerThreadId.store( nCurrentThreadId, std::memory_order_release );
    return CY_TRUE;
}

bool_t Cy_MutexUnlock( cy_mutex_t *pMutex ) noexcept
{
    if ( !Cy_MutexIsInitialized( pMutex ) ||
         pMutex->nOwnerThreadId.load( std::memory_order_acquire ) !=
             Cy_ThreadGetCurrentId() ) {
        return CY_FALSE;
    }

    pMutex->nOwnerThreadId.store(
        CY_THREAD_INVALID_ID,
        std::memory_order_release );
    try {
        pMutex->native.unlock();
    } catch ( ... ) {
        pMutex->nOwnerThreadId.store(
            Cy_ThreadGetCurrentId(),
            std::memory_order_release );
        return CY_FALSE;
    }
    return CY_TRUE;
}

bool_t Cy_MutexIsOwnedByCurrentThread( const cy_mutex_t *pMutex ) noexcept
{
    return Cy_MutexIsInitialized( pMutex ) &&
           pMutex->nOwnerThreadId.load( std::memory_order_acquire ) ==
               Cy_ThreadGetCurrentId();
}

bool_t Cy_RecursiveMutexInit( cy_recursive_mutex_t *pMutex ) noexcept
{
    if ( pMutex == nullptr ) {
        return CY_FALSE;
    }

    bool_t wasInitialized = CY_FALSE;
    if ( !pMutex->isInitialized.compare_exchange_strong(
             wasInitialized,
             CY_TRUE,
             std::memory_order_release,
             std::memory_order_relaxed ) ) {
        return CY_FALSE;
    }

    pMutex->nOwnerThreadId.store(
        CY_THREAD_INVALID_ID,
        std::memory_order_relaxed );
    pMutex->nRecursionDepth.store( 0u, std::memory_order_relaxed );
    return CY_TRUE;
}

bool_t Cy_RecursiveMutexShutdown( cy_recursive_mutex_t *pMutex ) noexcept
{
    if ( !Cy_RecursiveMutexIsInitialized( pMutex ) ) {
        return CY_FALSE;
    }
    if ( pMutex->nOwnerThreadId.load( std::memory_order_acquire ) !=
             CY_THREAD_INVALID_ID ||
         pMutex->nRecursionDepth.load( std::memory_order_acquire ) != 0u ) {
        return CY_FALSE;
    }

    try {
        if ( !pMutex->native.try_lock() ) {
            return CY_FALSE;
        }
    } catch ( ... ) {
        return CY_FALSE;
    }

    bool_t wasInitialized = CY_TRUE;
    const bool_t didShutdown = pMutex->isInitialized.compare_exchange_strong(
        wasInitialized,
        CY_FALSE,
        std::memory_order_acq_rel,
        std::memory_order_acquire );
    pMutex->native.unlock();
    return didShutdown;
}

bool_t Cy_RecursiveMutexIsInitialized(
    const cy_recursive_mutex_t *pMutex ) noexcept
{
    return pMutex != nullptr &&
           pMutex->isInitialized.load( std::memory_order_acquire );
}

bool_t Cy_RecursiveMutexLock( cy_recursive_mutex_t *pMutex ) noexcept
{
    if ( !Cy_RecursiveMutexIsInitialized( pMutex ) ) {
        return CY_FALSE;
    }

    try {
        pMutex->native.lock();
    } catch ( ... ) {
        return CY_FALSE;
    }
    if ( !Cy_RecursiveMutexIsInitialized( pMutex ) ) {
        pMutex->native.unlock();
        return CY_FALSE;
    }

    const thread_id_t nCurrentThreadId = Cy_ThreadGetCurrentId();
    if ( pMutex->nOwnerThreadId.load( std::memory_order_acquire ) ==
         nCurrentThreadId ) {
        pMutex->nRecursionDepth.fetch_add( 1u, std::memory_order_relaxed );
    } else {
        pMutex->nOwnerThreadId.store(
            nCurrentThreadId,
            std::memory_order_release );
        pMutex->nRecursionDepth.store( 1u, std::memory_order_release );
    }
    return CY_TRUE;
}

bool_t Cy_RecursiveMutexTryLock( cy_recursive_mutex_t *pMutex ) noexcept
{
    if ( !Cy_RecursiveMutexIsInitialized( pMutex ) ) {
        return CY_FALSE;
    }

    try {
        if ( !pMutex->native.try_lock() ) {
            return CY_FALSE;
        }
    } catch ( ... ) {
        return CY_FALSE;
    }

    if ( !Cy_RecursiveMutexIsInitialized( pMutex ) ) {
        pMutex->native.unlock();
        return CY_FALSE;
    }
    const thread_id_t nCurrentThreadId = Cy_ThreadGetCurrentId();
    if ( pMutex->nOwnerThreadId.load( std::memory_order_acquire ) ==
         nCurrentThreadId ) {
        pMutex->nRecursionDepth.fetch_add( 1u, std::memory_order_relaxed );
    } else {
        pMutex->nOwnerThreadId.store(
            nCurrentThreadId,
            std::memory_order_release );
        pMutex->nRecursionDepth.store( 1u, std::memory_order_release );
    }
    return CY_TRUE;
}

bool_t Cy_RecursiveMutexUnlock( cy_recursive_mutex_t *pMutex ) noexcept
{
    if ( !Cy_RecursiveMutexIsInitialized( pMutex ) ||
         pMutex->nOwnerThreadId.load( std::memory_order_acquire ) !=
             Cy_ThreadGetCurrentId() ) {
        return CY_FALSE;
    }

    const u32 nDepth =
        pMutex->nRecursionDepth.load( std::memory_order_acquire );
    if ( nDepth == 0u ) {
        return CY_FALSE;
    }

    if ( nDepth == 1u ) {
        pMutex->nRecursionDepth.store( 0u, std::memory_order_release );
        pMutex->nOwnerThreadId.store(
            CY_THREAD_INVALID_ID,
            std::memory_order_release );
    } else {
        pMutex->nRecursionDepth.store( nDepth - 1u, std::memory_order_release );
    }

    try {
        pMutex->native.unlock();
    } catch ( ... ) {
        return CY_FALSE;
    }
    return CY_TRUE;
}

bool_t Cy_RecursiveMutexIsOwnedByCurrentThread(
    const cy_recursive_mutex_t *pMutex ) noexcept
{
    return Cy_RecursiveMutexIsInitialized( pMutex ) &&
           pMutex->nRecursionDepth.load( std::memory_order_acquire ) != 0u &&
           pMutex->nOwnerThreadId.load( std::memory_order_acquire ) ==
               Cy_ThreadGetCurrentId();
}

} // namespace cypher::common
