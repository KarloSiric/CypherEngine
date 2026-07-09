//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Semaphore.cpp
//  Purpose: Implements CypherCommon Tier0 Semaphore synchronization.
//  Details: This file provides portable counting waits used by future work queues,
//           async IO request queues, and producer/consumer systems.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Semaphore.h"

#include <chrono>

namespace cypher::common
{

bool_t Cy_SemaphoreInit( cy_semaphore_t *pSemaphore, u32 nInitialCount, u32 nMaxCount )
{
    if ( pSemaphore == nullptr || nMaxCount == 0u || nInitialCount > nMaxCount ) {
        return CY_FALSE;
    }

    std::lock_guard<std::mutex> lock( pSemaphore->nativeMutex );
    pSemaphore->nCount = nInitialCount;
    pSemaphore->nMaxCount = nMaxCount;
    pSemaphore->bInitialized = CY_TRUE;
    return CY_TRUE;
}

void Cy_SemaphoreShutdown( cy_semaphore_t *pSemaphore )
{
    if ( pSemaphore == nullptr ) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock( pSemaphore->nativeMutex );
        pSemaphore->bInitialized = CY_FALSE;
    }

    pSemaphore->nativeCondition.notify_all();
}

bool_t Cy_SemaphoreIsInitialized( const cy_semaphore_t *pSemaphore )
{
    if ( pSemaphore == nullptr ) {
        return CY_FALSE;
    }

    std::lock_guard<std::mutex> lock( pSemaphore->nativeMutex );
    return pSemaphore->bInitialized;
}

bool_t Cy_SemaphorePost( cy_semaphore_t *pSemaphore, u32 nCount )
{
    if ( pSemaphore == nullptr || nCount == 0u ) {
        return CY_FALSE;
    }

    {
        std::lock_guard<std::mutex> lock( pSemaphore->nativeMutex );
        if ( !pSemaphore->bInitialized ) {
            return CY_FALSE;
        }

        if ( nCount > pSemaphore->nMaxCount - pSemaphore->nCount ) {
            return CY_FALSE;
        }

        pSemaphore->nCount += nCount;
    }

    if ( nCount == 1u ) {
        pSemaphore->nativeCondition.notify_one();
    } else {
        pSemaphore->nativeCondition.notify_all();
    }

    return CY_TRUE;
}

bool_t Cy_SemaphoreWait( cy_semaphore_t *pSemaphore )
{
    if ( pSemaphore == nullptr ) {
        return CY_FALSE;
    }

    std::unique_lock<std::mutex> lock( pSemaphore->nativeMutex );
    pSemaphore->nativeCondition.wait( lock, [&]() {
        return !pSemaphore->bInitialized || pSemaphore->nCount > 0u;
    } );

    if ( !pSemaphore->bInitialized || pSemaphore->nCount == 0u ) {
        return CY_FALSE;
    }

    --pSemaphore->nCount;
    return CY_TRUE;
}

bool_t Cy_SemaphoreTryWait( cy_semaphore_t *pSemaphore )
{
    if ( pSemaphore == nullptr ) {
        return CY_FALSE;
    }

    std::lock_guard<std::mutex> lock( pSemaphore->nativeMutex );
    if ( !pSemaphore->bInitialized || pSemaphore->nCount == 0u ) {
        return CY_FALSE;
    }

    --pSemaphore->nCount;
    return CY_TRUE;
}

bool_t Cy_SemaphoreWaitTimeoutMs( cy_semaphore_t *pSemaphore, u32 nMilliseconds )
{
    if ( pSemaphore == nullptr ) {
        return CY_FALSE;
    }

    std::unique_lock<std::mutex> lock( pSemaphore->nativeMutex );
    const bool_t bReady = pSemaphore->nativeCondition.wait_for( lock, std::chrono::milliseconds( nMilliseconds ), [&]() {
        return !pSemaphore->bInitialized || pSemaphore->nCount > 0u;
    } );

    if ( !bReady || !pSemaphore->bInitialized || pSemaphore->nCount == 0u ) {
        return CY_FALSE;
    }

    --pSemaphore->nCount;
    return CY_TRUE;
}

u32 Cy_SemaphoreGetCount( cy_semaphore_t *pSemaphore )
{
    if ( pSemaphore == nullptr ) {
        return 0u;
    }

    std::lock_guard<std::mutex> lock( pSemaphore->nativeMutex );
    return pSemaphore->nCount;
}

} // namespace cypher::common
