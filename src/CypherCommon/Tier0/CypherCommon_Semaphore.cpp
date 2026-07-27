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
namespace
{

void SemaphoreWaiterLeftLocked( cy_semaphore_t *pSemaphore ) noexcept
{
    --pSemaphore->nWaiterCount;
    if ( !pSemaphore->isInitialized && pSemaphore->nWaiterCount == 0u ) {
        pSemaphore->nativeCondition.notify_all();
    }
}

cy_wait_result_t SemaphoreConsumeLocked( cy_semaphore_t *pSemaphore ) noexcept
{
    if ( !pSemaphore->isInitialized ) {
        return cy_wait_result_t::Shutdown;
    }
    if ( pSemaphore->nCount == 0u ) {
        return cy_wait_result_t::Timeout;
    }
    --pSemaphore->nCount;
    return cy_wait_result_t::Success;
}

} // namespace

bool_t Cy_SemaphoreInit(
    cy_semaphore_t *pSemaphore,
    u32 nInitialCount,
    u32 nMaxCount ) noexcept
{
    if ( pSemaphore == nullptr || nMaxCount == 0u || nInitialCount > nMaxCount ) {
        return CY_FALSE;
    }

    std::lock_guard<std::mutex> lock( pSemaphore->nativeMutex );
    if ( pSemaphore->isInitialized || pSemaphore->nWaiterCount != 0u ) {
        return CY_FALSE;
    }
    pSemaphore->nCount = nInitialCount;
    pSemaphore->nMaxCount = nMaxCount;
    pSemaphore->isInitialized = CY_TRUE;
    return CY_TRUE;
}

bool_t Cy_SemaphoreShutdown( cy_semaphore_t *pSemaphore ) noexcept
{
    if ( pSemaphore == nullptr ) {
        return CY_FALSE;
    }

    std::unique_lock<std::mutex> lock( pSemaphore->nativeMutex );
    if ( !pSemaphore->isInitialized ) {
        return CY_FALSE;
    }

    pSemaphore->isInitialized = CY_FALSE;
    pSemaphore->nCount = 0u;
    pSemaphore->nativeCondition.notify_all();
    pSemaphore->nativeCondition.wait( lock, [&]() {
        return pSemaphore->nWaiterCount == 0u;
    } );
    return CY_TRUE;
}

bool_t Cy_SemaphoreIsInitialized(
    const cy_semaphore_t *pSemaphore ) noexcept
{
    if ( pSemaphore == nullptr ) {
        return CY_FALSE;
    }

    std::lock_guard<std::mutex> lock( pSemaphore->nativeMutex );
    return pSemaphore->isInitialized;
}

bool_t Cy_SemaphorePost(
    cy_semaphore_t *pSemaphore,
    u32 nCount ) noexcept
{
    if ( pSemaphore == nullptr || nCount == 0u ) {
        return CY_FALSE;
    }

    u32 nWakeCount = 0u;
    {
        std::lock_guard<std::mutex> lock( pSemaphore->nativeMutex );
        if ( !pSemaphore->isInitialized ) {
            return CY_FALSE;
        }

        if ( nCount > pSemaphore->nMaxCount - pSemaphore->nCount ) {
            return CY_FALSE;
        }

        pSemaphore->nCount += nCount;
        nWakeCount = nCount < pSemaphore->nWaiterCount
            ? nCount
            : pSemaphore->nWaiterCount;
    }

    if ( nWakeCount == 1u ) {
        pSemaphore->nativeCondition.notify_one();
    } else if ( nWakeCount > 1u ) {
        for ( u32 nWakeIndex = 0u; nWakeIndex < nWakeCount; ++nWakeIndex ) {
            pSemaphore->nativeCondition.notify_one();
        }
    }

    return CY_TRUE;
}

cy_wait_result_t Cy_SemaphoreWaitResult(
    cy_semaphore_t *pSemaphore ) noexcept
{
    if ( pSemaphore == nullptr ) {
        return cy_wait_result_t::Invalid;
    }

    std::unique_lock<std::mutex> lock( pSemaphore->nativeMutex );
    if ( !pSemaphore->isInitialized ) {
        return cy_wait_result_t::Shutdown;
    }

    ++pSemaphore->nWaiterCount;
    try {
        pSemaphore->nativeCondition.wait( lock, [&]() {
            return !pSemaphore->isInitialized || pSemaphore->nCount > 0u;
        } );
    } catch ( ... ) {
        SemaphoreWaiterLeftLocked( pSemaphore );
        return cy_wait_result_t::Invalid;
    }

    const cy_wait_result_t result = SemaphoreConsumeLocked( pSemaphore );
    SemaphoreWaiterLeftLocked( pSemaphore );
    return result;
}

bool_t Cy_SemaphoreWait( cy_semaphore_t *pSemaphore ) noexcept
{
    return Cy_SemaphoreWaitResult( pSemaphore ) ==
           cy_wait_result_t::Success;
}

bool_t Cy_SemaphoreTryWait( cy_semaphore_t *pSemaphore ) noexcept
{
    if ( pSemaphore == nullptr ) {
        return CY_FALSE;
    }

    std::lock_guard<std::mutex> lock( pSemaphore->nativeMutex );
    return SemaphoreConsumeLocked( pSemaphore ) ==
           cy_wait_result_t::Success;
}

cy_wait_result_t Cy_SemaphoreWaitTimeoutMsResult(
    cy_semaphore_t *pSemaphore,
    u32 nMilliseconds ) noexcept
{
    if ( pSemaphore == nullptr ) {
        return cy_wait_result_t::Invalid;
    }

    std::unique_lock<std::mutex> lock( pSemaphore->nativeMutex );
    if ( !pSemaphore->isInitialized ) {
        return cy_wait_result_t::Shutdown;
    }

    ++pSemaphore->nWaiterCount;
    bool_t isReady = CY_FALSE;
    try {
        isReady = pSemaphore->nativeCondition.wait_for(
            lock,
            std::chrono::milliseconds( nMilliseconds ),
            [&]() {
                return !pSemaphore->isInitialized || pSemaphore->nCount > 0u;
            } );
    } catch ( ... ) {
        SemaphoreWaiterLeftLocked( pSemaphore );
        return cy_wait_result_t::Invalid;
    }

    const cy_wait_result_t result = isReady
        ? SemaphoreConsumeLocked( pSemaphore )
        : cy_wait_result_t::Timeout;
    SemaphoreWaiterLeftLocked( pSemaphore );
    return result;
}

bool_t Cy_SemaphoreWaitTimeoutMs(
    cy_semaphore_t *pSemaphore,
    u32 nMilliseconds ) noexcept
{
    return Cy_SemaphoreWaitTimeoutMsResult( pSemaphore, nMilliseconds ) ==
           cy_wait_result_t::Success;
}

u32 Cy_SemaphoreGetCount( const cy_semaphore_t *pSemaphore ) noexcept
{
    if ( pSemaphore == nullptr ) {
        return 0u;
    }

    std::lock_guard<std::mutex> lock( pSemaphore->nativeMutex );
    return pSemaphore->nCount;
}

u32 Cy_SemaphoreGetWaiterCount(
    const cy_semaphore_t *pSemaphore ) noexcept
{
    if ( pSemaphore == nullptr ) {
        return 0u;
    }
    std::lock_guard<std::mutex> lock( pSemaphore->nativeMutex );
    return pSemaphore->nWaiterCount;
}

} // namespace cypher::common
